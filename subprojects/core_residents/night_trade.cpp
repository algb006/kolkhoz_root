// The quiet night trades of Epoch I (core_residents/night_trade.h).

#include "night_trade.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
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
constexpr std::array<std::string_view, 14> kNightTradeWorldParamKeys = {
    "night_distillers_max",
    "night_fisher_age_from_years",
    "night_fisher_age_to_years",
    "night_hunter_age_from_years",
    "night_hunter_age_to_years",
    "night_moon_day_in_month",
    "night_trade_hour_out",
    "night_trade_hour_back",
    "night_fishing_min_mean_celsius",
    "night_hunt_reach_min_m",
    "night_hunt_reach_max_m",
    "night_fishing_catch_kg",
    "night_hunt_catch_kg",
    "night_hunt_success_chance"};

/// The oldest age a band may name, and the farthest a reach may be: past
/// either the cell is a typo. The map is twelve kilometres a side.
constexpr float kOldestYears = 120.0F;
constexpr float kFarthestMetres = 20000.0F;

/// The heaviest night's catch a row may name, kilograms.
constexpr float kHeaviestCatchKg = 1000.0F;

/// The adult age of a trade's keeper: the design says "взрослые мужчины".
constexpr float kAdultYears = 18.0F;

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

/// Where a resident's yard is: his family's house, or where it stood.
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

std::span<const std::string_view> NightTradeWorldParamKeys() {
  return kNightTradeWorldParamKeys;
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
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
    if (!Whole(distillers) || !Whole(moon) || !Whole(out) || !Whole(back)) {
      error = "world_params: a night trade count, day or hour is not a whole number";
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

bool IsMoonlitNight(const NightTradeConfig& config, SimDay day) {
  return day % kDaysPerMonth == config.moon_day_in_month;
}

void AssignNightTrades(const NightTradeConfig& config, float life_speedup, WorldState& current) {
  // Distillers, up to the most the village keeps, weighted by drinking.
  for (std::uint32_t kept = CountKeepers(current, NightTrade::kDistiller);
       kept < config.distillers_max;
       ++kept) {
    const std::vector<std::uint32_t> men =
        FreeMen(current, life_speedup, kAdultYears, kOldestYears, FamilyId{});
    std::vector<float> weights;
    weights.reserve(men.size());
    for (const std::uint32_t row : men) {
      weights.push_back(1.0F + current.residents.rows[row].alcoholism);
    }
    const std::uint32_t chosen = DrawWeighted(current.rng, men, weights);
    if (chosen == kNoRow) {
      break;
    }
    current.residents.rows[chosen].night_trade = NightTrade::kDistiller;
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
  AssignNightTrades(config, life_speedup, world);
  return true;
}

}  // namespace core
