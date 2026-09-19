// The quiet night trades of Epoch I (core_residents/night_trade.h).

#include "night_trade.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/day_window.h"
#include "core_common/emit_event.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_residents/residents_system.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// The world_params.csv keys, in the order of the knob list in the parse.
/// `night_watchman_theft_cut` left it on 2026-09-18 (the leak is closed or
/// open, not cut) and stands in kNightTradeKnownKeys below, known and not
/// read, until the base drops the row.
constexpr std::array<std::string_view, 22> kNightTradeWorldParamKeys = {
    "night_distillers_max",        "night_fisher_age_from_years", "night_fisher_age_to_years",
    "night_hunter_age_from_years", "night_hunter_age_to_years",   "night_moon_day_in_month",
    "night_trade_hour_out",        "night_trade_hour_back",       "night_fishing_min_mean_celsius",
    "night_hunt_reach_min_m",      "night_hunt_reach_max_m",      "night_fishing_catch_kg",
    "night_hunt_catch_kg",         "night_hunt_success_chance",   "night_distiller_raw_kg",
    "store_leak_complaint_kg",     "night_sober_keeper_max",      "samogon_reach_m",
    "distiller_replace_months",    "samogon_buy_kg_drinks",       "samogon_buy_kg_abuses",
    "samogon_lights_out_hour",
};

/// Every key the night trades answer for: those read, and the one retired.
constexpr std::array<std::string_view, kNightTradeWorldParamKeys.size() + 1> kNightTradeKnownKeys =
    [] {
      std::array<std::string_view, kNightTradeWorldParamKeys.size() + 1> keys{};
      for (std::size_t index = 0; index < kNightTradeWorldParamKeys.size(); ++index) {
        keys[index] = kNightTradeWorldParamKeys[index];
      }
      keys.back() = "night_watchman_theft_cut";  // RETIRED 2026-09-18, known and not read
      return keys;
    }();

/// The raw material a distiller takes, in the order he takes it (crime design
/// §7: grain, potato, sugar).
constexpr std::array<std::string_view, 6> kRawMaterialKeys = {
    "rye", "wheat", "barley", "oat", "potato", "sugar"};

/// The oldest age a band may name, and the farthest a reach may be: past
/// either the cell is a typo. The map is twelve kilometres a side.
constexpr float kOldestYears = 120.0F;
constexpr float kFarthestMetres = 20000.0F;

/// The heaviest night's catch a row may name, kilograms.
constexpr float kHeaviestCatchKg = 1000.0F;

/// The adult age of a trade's keeper: the design says "взрослые мужчины".
constexpr float kAdultYears = 18.0F;

/// The counter-hash salt of the evening sale's hour: its own draw, so the
/// sale moves no other number in the campaign.
constexpr std::uint64_t kSaleHourSalt = 0x5A6E;

bool Whole(float value) {
  return std::floor(value) == value;
}

/// A pantry takes a catch the way the family exchange's pantry does
/// (family_exchange.cpp): dense by resource, grown on demand.
void AddCatchToPantry(FamilyRow& family, ResourceId resource, Grams amount) {
  if (resource.value == kInvalidDefIdValue || amount <= 0) {
    return;
  }
  if (family.pantry.size() <= resource.value) {
    family.pantry.resize(resource.value + 1U, 0);
  }
  family.pantry[resource.value] += amount;
}

/// Neither Komsomol nor Party: who a night trade may be handed to.
bool OutsideTheOrganizations(const ResidentRow& person) {
  return person.social_status != SocialStatus::kKomsomol &&
         person.social_status != SocialStatus::kParty;
}

std::uint32_t CountKeepers(const WorldState& current, NightTrade trade) {
  std::uint32_t count = 0;
  for (const ResidentRow& person : current.residents.rows) {
    count += person.night_trade == trade ? 1U : 0U;
  }
  return count;
}

/// Picks one row out of `candidates` by lot from the world's stream, each
/// weighted by `weights`; kNoRow when there is nobody.
std::uint32_t DrawWeighted(RngState& rng,
                           const std::vector<std::uint32_t>& candidates,
                           const std::vector<float>& weights) {
  float total = 0.0F;
  for (const float weight : weights) {
    total += weight;
  }
  if (candidates.empty() || !(total > 0.0F)) {
    return kNoRow;
  }
  float point = NextRandomUnitFloat(rng) * total;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    if (point < weights[index]) {
      return candidates[index];
    }
    point -= weights[index];
  }
  return candidates.back();
}

/// The free adult men in an age band, outside the organizations and keeping
/// no trade — and, when `other_family` is valid, not of that yard.
std::vector<std::uint32_t> FreeMen(const WorldState& current,
                                   float life_speedup,
                                   float from_years,
                                   float to_years,
                                   FamilyId other_family) {
  std::vector<std::uint32_t> rows;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& person = current.residents.rows[row];
    const float age = BiologicalAgeYears(life_speedup, person.birth_day, current.calendar.day);
    if (person.sex != Sex::kMale || person.night_trade != NightTrade::kNone ||
        !OutsideTheOrganizations(person) || age < from_years || age >= to_years ||
        (other_family.value != kInvalidEntityIdValue &&
         person.family.value == other_family.value)) {
      continue;
    }
    rows.push_back(row);
  }
  return rows;
}

std::uint32_t DrawEqual(RngState& rng, const std::vector<std::uint32_t>& candidates) {
  return DrawWeighted(rng, candidates, std::vector<float>(candidates.size(), 1.0F));
}

void RecordOuting(WorldState& current,
                  std::uint32_t row,
                  Vec2 position,
                  const NightTradeConfig& config) {
  const ResidentRow& person = current.residents.rows[row];
  NightOutingRow outing;
  outing.resident = current.residents.row_ids[row];
  outing.trade = person.night_trade;
  outing.day = current.calendar.day;
  outing.position = position;
  outing.hour_out = config.hour_out;
  outing.hour_back = config.hour_back;
  AppendRow(current.night_outings, outing);
  SimEvent& event = EmitEvent(current, EventKind::kNightTradeOuting, EventSeverity::kRoutine);
  event.resident = outing.resident;
  event.family = person.family;
  event.amount = static_cast<std::int64_t>(person.night_trade);
}

/// The hour out: the night's rows and events.
void GoOut(const NightTradeConfig& config, WorldState& current) {
  current.night_outings = NightOutingTable{};
  const bool warm_water =
      current.weather.air_temperature_celsius >= config.fishing_min_mean_celsius;
  // Both fishers net the same spot, drawn once for the night.
  bool fishing_spot_drawn = false;
  Vec2 fishing_spot;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& person = current.residents.rows[row];
    Vec2 yard;
    if (person.night_trade == NightTrade::kNone || !YardOf(current, person, yard)) {
      continue;
    }
    switch (person.night_trade) {
      case NightTrade::kDistiller:
        RecordOuting(current, row, yard, config);  // at his own gate
        StealRawMaterial(config, current, row);    // and what he distils is the kolkhoz's
        break;
      case NightTrade::kNetFisher:
        if (!warm_water || config.fishing_spots.empty()) {
          break;
        }
        if (!fishing_spot_drawn) {
          const auto spot = static_cast<std::size_t>(
              NextRandomUnitFloat(current.rng) * static_cast<float>(config.fishing_spots.size()));
          fishing_spot = config.fishing_spots[spot < config.fishing_spots.size()
                                                  ? spot
                                                  : config.fishing_spots.size() - 1U];
          fishing_spot_drawn = true;
        }
        RecordOuting(current, row, fishing_spot, config);
        break;
      case NightTrade::kHunter: {
        std::vector<Vec2> in_reach;
        for (const TimberStandRow& stand : current.stands.rows) {
          const float distance = std::hypot(stand.position.x - yard.x, stand.position.y - yard.y);
          if (stand.kind == TimberStandKind::kForestOld && distance >= config.hunt_reach_min_m &&
              distance <= config.hunt_reach_max_m) {
            in_reach.push_back(stand.position);
          }
        }
        if (in_reach.empty()) {
          break;  // STUB: no old forest in reach — he stays home tonight
        }
        const auto square = static_cast<std::size_t>(NextRandomUnitFloat(current.rng) *
                                                     static_cast<float>(in_reach.size()));
        RecordOuting(current,
                     row,
                     in_reach[square < in_reach.size() ? square : in_reach.size() - 1U],
                     config);
        break;
      }
      default:
        break;
    }
  }
}

/// The hour back: what the night's outings bring home. At the return hour and
/// not the hour out, because a catch is in the pantry once it is carried
/// home — and because the events slot reads the pantries' change at hours 22
/// and 23 as the garden's and the meal's (ledger_state.h), so a catch booked
/// at 23 would be counted as eaten backwards.
void ComeBack(const NightTradeConfig& config, WorldState& current) {
  const Grams fish_each = GramsFromKilograms(config.fishing_catch_kg / 2.0F);
  const Grams game = GramsFromKilograms(config.hunt_catch_kg);
  for (const NightOutingRow& outing : current.night_outings.rows) {
    const std::uint32_t row = FindRow(current.residents, outing.resident);
    if (row == kNoRow) {
      continue;  // gone since: nothing is carried home
    }
    const std::uint32_t family_row = FindRow(current.families, current.residents.rows[row].family);
    if (family_row == kNoRow) {
      continue;
    }
    FamilyRow& family = current.families.rows[family_row];
    if (outing.trade == NightTrade::kNetFisher) {
      AddCatchToPantry(family, config.fish, fish_each);
      AddLedgerAmount(current.ledger.current.night_catch, config.fish, fish_each);
    } else if (outing.trade == NightTrade::kHunter &&
               NextRandomUnitFloat(current.rng) < config.hunt_success_chance) {
      AddCatchToPantry(family, config.meat, game);
      AddLedgerAmount(current.ledger.current.night_catch, config.meat, game);
    }
  }
}

}  // namespace

bool YardOf(const WorldState& current, const ResidentRow& person, Vec2& yard) {
  const std::uint32_t family_row = FindRow(current.families, person.family);
  if (family_row == kNoRow) {
    return false;
  }
  const FamilyRow& family = current.families.rows[family_row];
  const std::uint32_t house_row = FindRow(current.units, family.house);
  yard = house_row != kNoRow ? current.units.rows[house_row].position : family.lost_house_position;
  return true;
}

std::uint32_t SupplyMonthTag(SimDay day) {
  const Date date = DateFromDay(day);
  return (static_cast<std::uint32_t>(date.year) * kMonthsPerYear) +
         static_cast<std::uint32_t>(date.month) + 1U;
}

std::uint32_t NearestSuppliedDistiller(const NightTradeConfig& config,
                                       const WorldState& current,
                                       Vec2 yard,
                                       std::uint32_t tag) {
  std::uint32_t nearest = kNoRow;
  float best = config.samogon_reach_m;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& person = current.residents.rows[row];
    Vec2 his_yard;
    if (person.night_trade != NightTrade::kDistiller || person.distiller_supplied_month != tag ||
        tag == 0 || !YardOf(current, person, his_yard)) {
      continue;
    }
    const float distance = std::hypot(his_yard.x - yard.x, his_yard.y - yard.y);
    // At or within the reach; the nearer wins, and a tie keeps the lower row.
    if (distance <= best && (nearest == kNoRow || distance < best)) {
      best = distance;
      nearest = row;
    }
  }
  return nearest;
}

std::span<const std::string_view> NightTradeWorldParamKeys() {
  return kNightTradeKnownKeys;
}

bool ParseNightTradeConfig(const ITableSet& tables, NightTradeConfig& config, std::string& error) {
  if (const ITable* const world = tables.FindTable("world_params")) {
    auto distillers = static_cast<float>(config.distillers_max);
    auto moon = static_cast<float>(config.moon_day_in_month);
    auto out = static_cast<float>(config.hour_out);
    auto back = static_cast<float>(config.hour_back);
    const Range ages{.low = 0.0F, .high = kOldestYears};
    const Range hours{.low = 0.0F, .high = static_cast<float>(kTicksPerDay - 1U)};
    const Range reach{.low = 0.0F, .high = kFarthestMetres};
    const Range catch_kg{.low = 0.0F, .high = kHeaviestCatchKg};
    const std::array<ScalarKnob, kNightTradeWorldParamKeys.size()> knobs = {{
        {.key = kNightTradeWorldParamKeys[0],
         .value = &distillers,
         .range = {.low = 0.0F, .high = 20.0F}},
        {.key = kNightTradeWorldParamKeys[1],
         .value = &config.fisher_age_from_years,
         .range = ages},
        {.key = kNightTradeWorldParamKeys[2], .value = &config.fisher_age_to_years, .range = ages},
        {.key = kNightTradeWorldParamKeys[3],
         .value = &config.hunter_age_from_years,
         .range = ages},
        {.key = kNightTradeWorldParamKeys[4], .value = &config.hunter_age_to_years, .range = ages},
        {.key = kNightTradeWorldParamKeys[5],
         .value = &moon,
         .range = {.low = 0.0F, .high = static_cast<float>(kDaysPerMonth - 1U)}},
        {.key = kNightTradeWorldParamKeys[6], .value = &out, .range = hours},
        {.key = kNightTradeWorldParamKeys[7], .value = &back, .range = hours},
        {.key = kNightTradeWorldParamKeys[8],
         .value = &config.fishing_min_mean_celsius,
         .range = {.low = -50.0F, .high = 50.0F}},
        {.key = kNightTradeWorldParamKeys[9], .value = &config.hunt_reach_min_m, .range = reach},
        {.key = kNightTradeWorldParamKeys[10], .value = &config.hunt_reach_max_m, .range = reach},
        {.key = kNightTradeWorldParamKeys[11],
         .value = &config.fishing_catch_kg,
         .range = catch_kg},
        {.key = kNightTradeWorldParamKeys[12], .value = &config.hunt_catch_kg, .range = catch_kg},
        {.key = kNightTradeWorldParamKeys[13],
         .value = &config.hunt_success_chance,
         .range = {.low = 0.0F, .high = 1.0F}},
        {.key = kNightTradeWorldParamKeys[14],
         .value = &config.distiller_raw_kg,
         .range = catch_kg},
        {.key = kNightTradeWorldParamKeys[15],
         .value = &config.store_leak_complaint_kg,
         .range = {.low = 0.0F, .high = 100000.0F}},
        {.key = kNightTradeWorldParamKeys[16],
         .value = &config.sober_keeper_max,
         .range = {.low = 0.0F, .high = 100.0F}},
        {.key = kNightTradeWorldParamKeys[17], .value = &config.samogon_reach_m, .range = reach},
        {.key = kNightTradeWorldParamKeys[18],
         .value = &config.distiller_replace_months,
         .range = {.low = 0.0F, .high = 120.0F}},
        {.key = kNightTradeWorldParamKeys[19], .value = &config.buy_kg_drinks, .range = catch_kg},
        {.key = kNightTradeWorldParamKeys[20], .value = &config.buy_kg_abuses, .range = catch_kg},
        {.key = kNightTradeWorldParamKeys[21], .value = &config.lights_out_hour, .range = hours},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
    if (!Whole(distillers) || !Whole(moon) || !Whole(out) || !Whole(back) ||
        !Whole(config.distiller_replace_months) || !Whole(config.lights_out_hour)) {
      error = "world_params: a night trade count, day, month or hour is not a whole number";
      return false;
    }
    if (config.fisher_age_from_years >= config.fisher_age_to_years ||
        config.hunter_age_from_years >= config.hunter_age_to_years ||
        config.hunt_reach_min_m > config.hunt_reach_max_m) {
      error = "world_params: a night trade's age band or reach ends before it begins";
      return false;
    }
    if (out == back) {
      error = "world_params: night_trade_hour_out and night_trade_hour_back are the same hour";
      return false;
    }
    config.distillers_max = static_cast<std::uint32_t>(distillers);
    config.moon_day_in_month = static_cast<std::uint8_t>(moon);
    config.hour_out = static_cast<std::uint8_t>(out);
    config.hour_back = static_cast<std::uint8_t>(back);
  }
  if (const ITable* const resources = tables.FindTable("resources")) {
    config.fish = DefIdFromRow<ResourceIdTag>(resources->FindRowByKey("fish"));
    config.meat = DefIdFromRow<ResourceIdTag>(resources->FindRowByKey("meat"));
    config.raw_material.clear();
    for (const std::string_view key : kRawMaterialKeys) {
      const ResourceId raw = DefIdFromRow<ResourceIdTag>(resources->FindRowByKey(key));
      if (raw.value != kInvalidDefIdValue) {
        config.raw_material.push_back(raw);
      }
    }
  }
  // The posts' shifts, so the leak can ask whether a unit's watchman is at
  // his post tonight.
  if (const ITable* const professions = tables.FindTable("professions")) {
    config.storekeeper_post =
        DefIdFromRow<ProfessionIdTag>(professions->FindRowByKey("storekeeper"));
    const std::uint32_t shift_column = professions->FindColumn("shift");
    config.post_shift.assign(professions->RowCount(), PostShift::kWorkday);
    for (std::uint32_t row = 0; row < professions->RowCount(); ++row) {
      if (shift_column != kNoTableColumn &&
          !ParsePostShift(professions->CellText(row, shift_column), config.post_shift[row])) {
        config.post_shift[row] = PostShift::kWorkday;
      }
    }
  }
  config.fishing_spots.clear();
  if (const ITable* const spots = tables.FindTable("night_fishing_spots")) {
    const std::uint32_t x_column = spots->FindColumn("x_m");
    const std::uint32_t y_column = spots->FindColumn("y_m");
    if (x_column == kNoTableColumn || y_column == kNoTableColumn) {
      error = "night_fishing_spots: the x_m and y_m columns are missing";
      return false;
    }
    for (std::uint32_t row = 0; row < spots->RowCount(); ++row) {
      const std::optional<float> x = spots->CellReal(row, x_column);
      const std::optional<float> y = spots->CellReal(row, y_column);
      if (!x || !y || *x < 0.0F || *y < 0.0F || *x > kFarthestMetres || *y > kFarthestMetres) {
        error = "night_fishing_spots: row " + std::to_string(row) +
                " has no position on the map's numbers";
        return false;
      }
      config.fishing_spots.push_back(Vec2{.x = *x, .y = *y});
    }
  }
  return true;
}

bool StoreLeakClosed(const NightTradeConfig& config,
                     const WorldState& current,
                     std::uint32_t unit_row) {
  if (unit_row >= current.units.rows.size()) {
    return false;
  }
  // A MODULE IS KEPT BY ITS PARENT'S WATCH: the staff table posts the
  // watchman at the food yard, never at the granary on its plot, and the
  // grain lies in the granary — reading the unit's own posts alone left every
  // granary unguarded whoever stood at the gate (found by the runs' watchman,
  // 2026-09-15).
  const std::uint32_t here = current.units.row_ids[unit_row].value;
  const std::uint32_t parent = current.units.rows[unit_row].parent.value;
  bool sober_watch = false;
  bool drinking_keeper = false;
  for (const ResidentRow& person : current.residents.rows) {
    const std::uint32_t post_unit = person.post.unit.value;
    if (post_unit != here && (parent == kInvalidEntityIdValue || post_unit != parent)) {
      continue;
    }
    const std::uint32_t profession = person.post.profession.value;
    const bool sober = person.alcoholism <= config.sober_keeper_max;
    // A DRINKING WATCHMAN IS AS GOOD AS NONE (register 206; it was a 60 % cut
    // until 2026-09-18): the leak is closed or open, not trimmed.
    if (profession < config.post_shift.size() &&
        config.post_shift[profession] == PostShift::kNight && sober) {
      sober_watch = true;
    }
    // And a storekeeper, where the store has one, must be sober too.
    if (profession == config.storekeeper_post.value && !sober) {
      drinking_keeper = true;
    }
  }
  return sober_watch && !drinking_keeper;
}

Grams StealRawMaterial(const NightTradeConfig& config,
                       WorldState& current,
                       std::uint32_t distiller_row) {
  const Date date = DateFromDay(current.calendar.day);
  const std::uint32_t month_index = (static_cast<std::uint32_t>(date.year) * kMonthsPerYear) +
                                    static_cast<std::uint32_t>(date.month);
  if (current.night_theft.month_index != month_index) {
    current.night_theft.month_index = month_index;
    current.night_theft.stolen_this_month = 0;
  }
  Grams wanted = GramsFromKilograms(config.distiller_raw_kg);
  Grams taken = 0;
  for (const ResourceId raw : config.raw_material) {
    for (std::uint32_t unit_row = 0; unit_row < current.units.rows.size() && wanted > 0;
         ++unit_row) {
      const Grams free = UnreservedOf(current.units.rows[unit_row], raw);
      if (free <= 0 || StoreLeakClosed(config, current, unit_row)) {
        continue;  // nothing here, or a sober watch: he goes on to the next store
      }
      const Grams carried = free < wanted ? free : wanted;
      wanted -= carried;
      current.units.rows[unit_row].stock[raw.value] -= carried;
      taken += carried;
      AddLedgerAmount(current.ledger.current.stolen, raw, carried);
    }
  }
  // SUPPLIED THIS MONTH, and only when anything came: «самогонщик без сырья
  // этого месяца не продаёт» (register 206).
  if (taken > 0 && distiller_row < current.residents.rows.size()) {
    current.residents.rows[distiller_row].distiller_supplied_month = month_index + 1U;
  }
  current.night_theft.stolen_this_month += taken;
  // The village comes to complain once a campaign, when the month's loss
  // reaches the line. STUB: Epoch I has no constable's post, so nobody else
  // is there to go to.
  if (current.night_theft.complaint_raised == 0 &&
      current.night_theft.stolen_this_month >= GramsFromKilograms(config.store_leak_complaint_kg)) {
    current.night_theft.complaint_raised = 1;
    SimEvent& complaint =
        EmitEvent(current, EventKind::kStoreLeakComplaint, EventSeverity::kNotable);
    complaint.amount = current.night_theft.stolen_this_month;
  }
  return taken;
}

bool IsMoonlitNight(const NightTradeConfig& config, SimDay day) {
  return day % kDaysPerMonth == config.moon_day_in_month;
}

namespace {

/// One distiller drawn among the free men, weighted by drinking; false when
/// there is nobody to draw.
bool DrawDistiller(float life_speedup, WorldState& current) {
  const std::vector<std::uint32_t> men =
      FreeMen(current, life_speedup, kAdultYears, kOldestYears, FamilyId{});
  std::vector<float> weights;
  weights.reserve(men.size());
  for (const std::uint32_t row : men) {
    weights.push_back(1.0F + current.residents.rows[row].alcoholism);
  }
  const std::uint32_t chosen = DrawWeighted(current.rng, men, weights);
  if (chosen == kNoRow) {
    return false;
  }
  current.residents.rows[chosen].night_trade = NightTrade::kDistiller;
  return true;
}

}  // namespace

void AssignNightTrades(const NightTradeConfig& config,
                       float life_speedup,
                       WorldState& current,
                       bool with_distillers) {
  // Distillers, up to the most the village keeps, weighted by drinking — at
  // the start only. After it a distiller is replaced FROM THE LEAK, month by
  // month (TurnNightTheftMonth), and never at the year's turn: «закрыта —
  // никогда, и на переломе тоже» (register 206).
  for (std::uint32_t kept = CountKeepers(current, NightTrade::kDistiller);
       with_distillers && kept < config.distillers_max;
       ++kept) {
    if (!DrawDistiller(life_speedup, current)) {
      break;
    }
  }
  // The net fishers, a pair from two yards.
  for (std::uint32_t kept = CountKeepers(current, NightTrade::kNetFisher); kept < 2U; ++kept) {
    FamilyId partner_yard;
    for (const ResidentRow& person : current.residents.rows) {
      if (person.night_trade == NightTrade::kNetFisher) {
        partner_yard = person.family;
      }
    }
    const std::uint32_t chosen = DrawEqual(current.rng,
                                           FreeMen(current,
                                                   life_speedup,
                                                   config.fisher_age_from_years,
                                                   config.fisher_age_to_years,
                                                   partner_yard));
    if (chosen == kNoRow) {
      break;
    }
    current.residents.rows[chosen].night_trade = NightTrade::kNetFisher;
  }
  // The hunter, alone.
  if (CountKeepers(current, NightTrade::kHunter) == 0U) {
    const std::uint32_t chosen = DrawEqual(current.rng,
                                           FreeMen(current,
                                                   life_speedup,
                                                   config.hunter_age_from_years,
                                                   config.hunter_age_to_years,
                                                   FamilyId{}));
    if (chosen != kNoRow) {
      current.residents.rows[chosen].night_trade = NightTrade::kHunter;
    }
  }
}

void RunNightOutings(const NightTradeConfig& config, WorldState& current) {
  const std::uint32_t hour = HourFromTick(current.calendar.tick);
  // THE LEAK, SEEN EVERY NIGHT (register 206): a month is dry only if no
  // night of it found a store of grain or potato open. Asked at the hour out
  // of every night, moonlit or not — the watch stands or does not every
  // night, not only on the distillers' one.
  if (hour == config.hour_out && VillageLeakOpen(config, current)) {
    current.night_theft.leak_open_this_month = 1;
  }
  // THE EVENING SALE (register 206: «Продажа — вечером»): every supplied
  // distiller hands over at his gate once an evening, in an hour drawn from
  // sunset to lights-out. The scene's cue; what is paid moves at the month's
  // turn.
  const std::uint32_t tag = SupplyMonthTag(current.calendar.day);
  const std::uint32_t sunset = SunsetHour(current.weather.daylight_hours);
  const auto lights_out = static_cast<std::uint32_t>(config.lights_out_hour);
  const std::uint32_t span = lights_out > sunset ? lights_out - sunset : 1U;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& person = current.residents.rows[row];
    if (person.night_trade != NightTrade::kDistiller || person.distiller_supplied_month != tag) {
      continue;
    }
    const std::uint32_t id = current.residents.row_ids[row].value;
    const float draw =
        CounterHashUnitFloat(current.world_seed, current.calendar.day, id, kSaleHourSalt);
    const std::uint32_t sale_hour =
        sunset + std::min(static_cast<std::uint32_t>(draw * static_cast<float>(span)), span - 1U);
    if (hour == sale_hour) {
      SimEvent& sale = EmitEvent(current, EventKind::kSamogonSale, EventSeverity::kRoutine);
      sale.resident = current.residents.row_ids[row];
      sale.family = person.family;
    }
  }
  if (hour == config.hour_out && IsMoonlitNight(config, current.calendar.day)) {
    GoOut(config, current);
    return;
  }
  // The return belongs to the night that began on the moonlit day: on that
  // day when the hours do not cross midnight, on the next day when they do.
  const SimDay night_began = config.hour_back < config.hour_out && current.calendar.day > 0
                                 ? current.calendar.day - 1
                                 : current.calendar.day;
  if (hour == config.hour_back && IsMoonlitNight(config, night_began)) {
    ComeBack(config, current);
  }
}

bool ApplyStartNightTrades(const ITableSet& tables,
                           float life_speedup,
                           WorldState& world,
                           std::string& error) {
  NightTradeConfig config;
  if (!ParseNightTradeConfig(tables, config, error)) {
    return false;
  }
  AssignNightTrades(config, life_speedup, world, true);
  return true;
}

bool VillageLeakOpen(const NightTradeConfig& config, const WorldState& current) {
  for (std::uint32_t unit_row = 0; unit_row < current.units.rows.size(); ++unit_row) {
    const UnitRow& unit = current.units.rows[unit_row];
    const bool holds_raw = std::ranges::any_of(
        config.raw_material, [&unit](ResourceId raw) { return UnreservedOf(unit, raw) > 0; });
    if (holds_raw && !StoreLeakClosed(config, current, unit_row)) {
      return true;
    }
  }
  return false;
}

void TurnNightTheftMonth(const NightTradeConfig& config, float life_speedup, WorldState& current) {
  const SimDay day = current.calendar.day;
  if (day == 0 || day % kDaysPerMonth != 0) {
    return;
  }
  NightTheftTally& tally = current.night_theft;
  const std::uint32_t closed_tag = SupplyMonthTag(day - 1U);
  const bool leak_open = tally.leak_open_this_month != 0;
  // THE DRY MONTH (register 206): the leak closed on every day of it and no
  // distiller supplied in it. The fact that closes quest_e1_22 is this.
  const bool anybody_supplied =
      std::ranges::any_of(current.residents.rows, [closed_tag](const ResidentRow& person) {
        return person.night_trade == NightTrade::kDistiller &&
               person.distiller_supplied_month == closed_tag;
      });
  if (!leak_open && !anybody_supplied) {
    SimEvent& dry =
        EmitEvent(current, EventKind::kStoreLeakClosedDryMonth, EventSeverity::kNotable);
    dry.amount = static_cast<std::int64_t>(closed_tag);
  }
  // THE REPLACEMENT FROM THE LEAK (register 206): a vacancy is filled
  // `distiller_replace_months` after it was seen, in a month whose leak was
  // open; while the leak stays closed, never.
  const std::uint32_t kept = CountKeepers(current, NightTrade::kDistiller);
  if (kept >= config.distillers_max) {
    tally.distiller_short_since = 0;
  } else if (tally.distiller_short_since == 0) {
    tally.distiller_short_since = closed_tag;
  } else if (leak_open && static_cast<float>(closed_tag - tally.distiller_short_since) >=
                              config.distiller_replace_months) {
    for (std::uint32_t filled = kept; filled < config.distillers_max; ++filled) {
      if (!DrawDistiller(life_speedup, current)) {
        break;
      }
    }
    tally.distiller_short_since =
        CountKeepers(current, NightTrade::kDistiller) >= config.distillers_max ? 0U : closed_tag;
  }
  tally.leak_open_this_month = 0;
}

void ConsumeNightTradeOrders(WorldState& current) {
  for (OrderRow& order : current.orders.rows) {
    if (order.status != OrderStatus::kPending || order.kind != OrderKind::kTakeNightTrader) {
      continue;
    }
    const std::uint32_t row = FindRow(current.residents, order.resident);
    OrderRefusal refusal = OrderRefusal::kNone;
    if (row == kNoRow) {
      refusal = OrderRefusal::kNoSuchSubject;
    } else if (current.residents.rows[row].night_trade == NightTrade::kNone) {
      refusal = OrderRefusal::kNotEligible;
    } else {
      current.residents.rows[row].night_trade = NightTrade::kNone;
      // By id and after the scan: removal moves the rows behind it.
      std::vector<NightOutingId> out_tonight;
      for (std::uint32_t outing = 0; outing < current.night_outings.rows.size(); ++outing) {
        if (current.night_outings.rows[outing].resident.value == order.resident.value) {
          out_tonight.push_back(current.night_outings.row_ids[outing]);
        }
      }
      for (const NightOutingId id : out_tonight) {
        RemoveRow(current.night_outings, id);
      }
    }
    order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
    order.refusal = refusal;
  }
}

}  // namespace core
