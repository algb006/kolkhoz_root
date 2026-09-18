// The district's plan in the simulation (core_production/district_plan.h).
// Moved whole out of production_system.cpp (boss, parcel 332).

#include "district_plan.h"

#include <cmath>
#include <cstdint>
#include <string>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "district_visit.h"
#include "herd_system.h"
#include "stock_ops.h"

namespace core {

/// The year's delivery: what the plan asked for leaves the stores and is
/// recorded as delivered. A shortfall is a shortfall now — it used to be
/// "simply a smaller delivery" because the district had no mechanics, and
/// JudgePlan below is those mechanics arriving.
void DeliverPlan(const ProductionConfig& config, WorldState& current) {
  // THE TURN SHIPS WHAT IS STILL OWED, not the whole figure again: whatever
  // the chairman shipped earlier by order (kDeliverPlan) is already in
  // `delivered`. «Держать до срока» is this — the default, the deadline.
  if (current.plan.delivered.size() < current.plan.due.size()) {
    current.plan.delivered.resize(current.plan.due.size(), 0);
  }
  for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
    const Grams owed = current.plan.due[index] - current.plan.delivered[index];
    if (owed <= 0) {
      continue;
    }
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    const Grams taken = TakeFromStorage(current, config, resource, owed);
    current.plan.delivered[index] += taken;
    AddLedgerAmount(current.ledger.current.delivered, resource, taken);
  }
}

OrderRefusal DeliverPlanNow(const ProductionConfig& config,
                            WorldState& current,
                            ResourceId only,
                            Grams amount) {
  if (current.plan.announced == 0 || current.plan.due.empty()) {
    return OrderRefusal::kNoPlanYet;
  }
  if (current.plan.delivered.size() < current.plan.due.size()) {
    current.plan.delivered.resize(current.plan.due.size(), 0);
  }
  Grams shipped = 0;
  for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
    if (only.value != kInvalidDefIdValue && index != only.value) {
      continue;
    }
    // A POSITION IS WHAT THE DISTRICT ASKED FOR: a resource with no figure is
    // not over-delivered, it is delivered to nobody.
    if (current.plan.due[index] <= 0) {
      continue;
    }
    const Grams owed = current.plan.due[index] - current.plan.delivered[index];
    const Grams wanted = amount > 0 ? amount : owed;
    if (wanted <= 0) {
      continue;
    }
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    const Grams taken = TakeFromStorage(current, config, resource, wanted);
    current.plan.delivered[index] += taken;
    AddLedgerAmount(current.ledger.current.delivered, resource, taken);
    shipped += taken;
  }
  // NOTHING SHIPPED IS A REFUSAL AND NOT A DONE: nothing owed, or nothing of
  // it in the stores. A "done" that moved no gram would teach the chairman
  // that the button works when it did nothing.
  return shipped > 0 ? OrderRefusal::kNone : OrderRefusal::kRuleForbids;
}

/// @brief Was every position DELIVERED (PositionDelivered, the met share)?
/// The limit's premium. It asked 100 % until 2026-09-19, apart from the
/// share that decides failure and trial (boss, parcel 211); boss seq 103
/// joined the two — «сдано в полном объёме» and «выполнен» are one thing to
/// the district. It still differs from PlanWasMet in one case: a plan of
/// nothing earns no premium, there was nothing to deliver.
bool PlanFullyDelivered(const ProductionConfig& config, const WorldState& current) {
  bool asked = false;
  for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
    const Grams due = current.plan.due[index];
    if (due == 0) {
      continue;
    }
    asked = true;
    const Grams delivered =
        index < current.plan.delivered.size() ? current.plan.delivered[index] : 0;
    if (!PositionDelivered(config, due, delivered)) {
      return false;
    }
  }
  return asked;
}

bool PositionDelivered(const ProductionConfig& config, Grams due, Grams delivered) {
  if (due <= 0) {
    return true;
  }
  // THE SHARE IN FLOAT, AS THE KNOB IS. In double, 0.99F widens to
  // 0.9900000095 and exactly 99 % of a figure came out short (the unit test
  // caught it, 2026-09-19); a float quotient rounds to the same float the
  // table's 0.99 was read into.
  const float share = static_cast<float>(delivered) / static_cast<float>(due);
  return share >= config.plan_met_share;
}

float PlanOverfulfilGrainTonnes(const ProductionConfig& config, const WorldState& current) {
  if (!(config.limit.overfulfil_grain_kcal_per_gram > 0.0F)) {
    return 0.0F;
  }
  // A DEDUCTION AND NOT A GATE (boss seq 88, district §1 `609ef830`). Until
  // 2026-09-19 nothing counted unless every position was delivered in full,
  // and host measured what that costs: seed 5 was six kilograms of rye short
  // of a 1.116 t debt and lost 339 points of surplus with it. Now a position
  // short of its delivered share takes K tonnes of grain off the surplus for
  // every tonne it lacks — the plan's verdict is untouched and still failed.
  double over_grams = 0.0;
  double short_grams = 0.0;
  for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
    const Grams due = current.plan.due[index];
    if (due <= 0 || index >= config.food_kcal_per_gram.size()) {
      continue;
    }
    const Grams delivered =
        index < current.plan.delivered.size() ? current.plan.delivered[index] : Grams{0};
    const double in_grain = static_cast<double>(config.food_kcal_per_gram[index] /
                                                config.limit.overfulfil_grain_kcal_per_gram);
    if (delivered > due) {
      over_grams += static_cast<double>(delivered - due) * in_grain;
    } else if (!PositionDelivered(config, due, delivered)) {
      short_grams += static_cast<double>(due - delivered) * in_grain;
    }
  }
  const double net =
      over_grams - (static_cast<double>(config.limit.overfulfil_shortfall_factor) * short_grams);
  return net > 0.0 ? static_cast<float>(net / static_cast<double>(kGramsPerTonne)) : 0.0F;
}

/// @brief Was every position delivered to the share that counts as met?
///
/// EVERY POSITION AND NOT THE TOTAL. Grain is not potato: a settlement
/// that shipped double the oat and no wheat at all has not met a plan
/// that asked for both, and a tonnage summed across resources would say
/// it had. The design's word is "сорванный план", one plan, and a plan is
/// its positions.
///
/// A position the district asked nothing of is met by anything, including
/// nothing — which is why the zero case is tested for rather than divided
/// through.
bool PlanWasMet(const ProductionConfig& config, const WorldState& current) {
  for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
    const Grams due = current.plan.due[index];
    if (due == 0) {
      continue;
    }
    const Grams delivered =
        index < current.plan.delivered.size() ? current.plan.delivered[index] : 0;
    // ONE RULE FOR "DELIVERED" (boss seq 89): the verdict and the
    // overfulfilment's deduction ask the same question of a position.
    if (!PositionDelivered(config, due, delivered)) {
      return false;
    }
  }
  return true;
}

/// @brief The district names the year's norm in the spring: a share of
/// what the arable WORKED LAST YEAR should give at a normal yield
/// (boss's decision of 2026-09-12; district design §9).
///
/// OFF THE LAND AND NOT OFF THE REAPING, which is the whole repair. A
/// share of the reaping was owed only by a settlement that had already
/// cut it, so the plan could not be missed and a verdict on it could not
/// fail. A norm off the land stands whatever the weather does.
///
/// OFF WORKED LAND AND NOT OFF SOWN LAND, so that sowing less does not owe
/// less: undersowing is a way to FAIL a plan, not a way to shrink one.
///
/// AND OFF LAST YEAR'S WORKING SINCE 2026-09-13, which this block said for
/// a day was NOT what the code did — it is now. The figure comes off
/// PlanState::worked_ha_last_year, the largest worked area the closing year
/// held, so raising ground enters the plan the year AFTER it is broken
/// (district §9) and no order settled today can move today's norm.
///
/// THE POSITIONS ARE THE DISTRICT'S, not the chairman's crops: campaign.csv
/// names them and the share of the worked arable counted under each. A norm
/// priced off what he planted is a norm he sets.
void AnnouncePlan(const ProductionConfig& config, WorldState& current) {
  // THE RELEASES ARE NOT CLEARED HERE, and they were for one afternoon:
  // the edit that put the clearing into JudgePlan matched this line too,
  // because both functions open by assigning the plan away. The spring
  // clearing wiped a SEED fund opened in the hungry end of winter — on the
  // very day the sowing year begins, which is the day that release was
  // taken for. A scripted edit that finds a second anchor is silently
  // successful; this one was found by the delivery cycle's RACE pass.
  current.plan.due.assign(current.plan.due.size(), 0);
  current.plan.announced = 0;
  // WHAT WAS DELIVERED IS COUNTED FROM THE ANNOUNCEMENT ON (kDeliverPlan,
  // econ's audit M2, 2026-09-18): the chairman may ship any time between the
  // spring's figure and the year's turn, and the turn ships only what is
  // still owed. Cleared here, where the year's figure starts, and not at the
  // turn — last year's delivered stays readable through the winter, as it
  // always was.
  current.plan.delivered.assign(current.plan.delivered.size(), 0);
  if (!(config.plan_grain_share > 0.0F) || config.plan_positions.empty()) {
    return;  // no district in these tables: nothing is asked and nothing is judged
  }
  // THE AREA IS LAST YEAR'S WORKED ARABLE, AND THE POSITIONS ARE THE
  // DISTRICT'S. Both halves are repairs of the same defect, and the defect
  // was a button: until 2026-09-13 the norm was priced off the crop
  // standing in each field's year0 slot, so a fallow year — an empty slot —
  // cost nothing, and a chairman who laid every field to fallow, or who
  // withdrew every chain (the order book allows it on purpose, as the move
  // that forgives a layout mistake), owed the district NOTHING AT ALL: no
  // norm, no verdict, no failed year, no trial. The epoch's main pressure
  // switched off by a decision not to sow.
  //
  // District design §9 says the opposite in the paragraph the share comes
  // from, and these are its own words: "Норма идёт с обработанной земли, а
  // не с посеянной. Недосев — способ провалить план, а не уменьшить его"
  //
  // A norm computed from what the chairman planted is a norm the chairman
  // SETS. So neither half of the figure is his any more: the area is what
  // was worked in the year that closed (PlanState::worked_ha_last_year,
  // written at the turn) and the shares are the district's, computed off
  // the canon layout's 210 hectare-years and written in campaign.csv.
  // Taking LAST year's area also delivers §9's other line for free: raised
  // ground enters the plan the year AFTER it is broken.
  //
  // A FIRST DRAFT SPLIT THE AREA EVENLY between the positions, for want of
  // the shares, and the analysis measured what that costs: the district
  // asked for oats on a third of the arable every year while the canon
  // rotation sows them on 10.5 ha every third year, and the reference run
  // failed the plan in nineteen years of thirty and reached the trial
  // condition in its FOURTH. The instrument had gone from "cannot be
  // failed" straight through the middle to "cannot be met".
  // THE FIRST YEAR ASKS BY THE START STOCK (boss, parcel 399; district
  // design §9). Priced off the arable, the first norm asked the derelict 160
  // hectares for a harvest nobody could reap — winter rye cannot stand in the
  // first spring — and year 1 failed whatever the play: seeds 1933 and 1936
  // failed it on every run. The share is a STUB (campaign.csv).
  const bool first_year = current.calendar.day < kDaysPerYear;
  for (const ProductionConfig::PlanPosition& position : config.plan_positions) {
    if (position.crop.value >= config.crops.size()) {
      continue;
    }
    const CropDef& crop = config.crops[position.crop.value];
    if (crop.yield_kg_per_ha <= 0.0F || crop.resource.value == kInvalidDefIdValue) {
      continue;
    }
    if (first_year) {
      const Grams stock = AmountOf(config.start_stock, crop.resource);
      AddToStock(current.plan.due,
                 crop.resource,
                 static_cast<Grams>(
                     std::llround(static_cast<double>(stock) *
                                  static_cast<double>(config.first_plan_start_stock_share))));
      continue;
    }
    const float area = current.plan.worked_ha_last_year * position.area_share;
    AddToStock(current.plan.due,
               crop.resource,
               GramsFromKilograms(crop.yield_kg_per_ha * area * config.plan_grain_share));
  }
  NameAccumulationLimit(config, current, first_year);
  // ANNOUNCED EVEN WHEN THE FIGURE IS ZERO, and that is the whole point of
  // the byte: a settlement that worked no land last year is one the
  // district HAS spoken to and asked nothing of, which is not the same
  // state as a world with no district in its tables (world_state.h).
  current.plan.announced = 1;
}

void NameAccumulationLimit(const ProductionConfig& config, WorldState& current, bool first_year) {
  current.plan.accumulation_limit.assign(current.plan.accumulation_limit.size(), 0);
  // THE FIRST YEAR HAS NO LIMIT, and that is a decision (boss seq 81): the
  // district sizes it off the book of a year gone, and a first year has
  // none — «год без памяти у района». Its plan is off the start stock anyway.
  if (first_year || !(config.limit.accumulation_share > 0.0F)) {
    return;
  }
  const YearLedger& book = current.ledger.closed;
  const auto at = [](const ResourceAmounts& column, ResourceId resource) {
    return resource.value < column.size() ? column[resource.value] : Grams{0};
  };
  // ON EVERY PRODUCE A PLAN CAN ASK FOR, not only this year's positions
  // (district §9: «на все ресурсы, которые могут попасть в план»): the
  // positions list is the district's roster, whatever this year's figure.
  for (const ProductionConfig::PlanPosition& position : config.plan_positions) {
    if (position.crop.value >= config.crops.size()) {
      continue;
    }
    const ResourceId produce = config.crops[position.crop.value].resource;
    if (produce.value == kInvalidDefIdValue || at(current.plan.accumulation_limit, produce) > 0) {
      continue;  // two positions of one produce share one limit
    }
    // ROOMY BY CONSTRUCTION (§9: «зимовка, семенной фонд и резерв плана
    // помещаются с запасом»): next year's seed of every field whose next slot
    // grows this produce, this year's figure, and what the village ate and
    // fed of it in the year gone — the wintering read off the book rather
    // than off a second formula of rations — times a share above one.
    Grams seed = 0;
    for (const FieldRow& field : current.fields.rows) {
      if (field.kind != LandKind::kArable || !HasRotation(field) ||
          field.rotation_year1.value >= config.crops.size()) {
        continue;
      }
      const CropDef& next = config.crops[field.rotation_year1.value];
      if (next.resource.value == produce.value) {
        seed += GramsFromKilograms(next.sowing_norm_kg_per_ha * field.area_ga);
      }
    }
    // AND THE TEAM'S YEAR OF OATS (boss seq 83). Without the fodder fund the
    // bare village, which hoards nothing, was seized 3.0 t of oats in its
    // second year and 7.9 t in its fourth (oat_balance, seed 1929): last
    // year's "fed" undercounts a growing team, and a limit that strikes
    // husbandry is not the design's («бьёт не по хозяйственности»). The
    // share stays; the base is made honest.
    const Grams base = seed + at(current.plan.due, produce) + at(book.eaten, produce) +
                       at(book.feed, produce) + FodderFundGrams(config, current, produce);
    const auto limit = static_cast<Grams>(std::llround(
        static_cast<double>(base) * static_cast<double>(config.limit.accumulation_share)));
    if (limit > 0) {
      AddToStock(current.plan.accumulation_limit, produce, limit);
    }
  }
}

/// @brief The arable the village actually worked this year, in hectares —
/// written at the year's turn for next spring's norm to be computed from.
///
/// WORKED MEANS A CHAIN WAS GIVEN, which is the same test every other
/// reader of worked land uses (HasRotation, land_state.h): the fallow
/// ploughing, the fertility recovery, the manure queue and both mean
/// fertility walks. Read at the TURN rather than in spring, so that what
/// the chairman does between January and the announcement cannot move the
/// figure — which is exactly what the old reading allowed.
float WorkedArableHa(const WorldState& current) {
  float worked_ha = 0.0F;
  for (const FieldRow& field : current.fields.rows) {
    if (field.kind == LandKind::kArable && HasRotation(field)) {
      worked_ha += field.area_ga;
    }
  }
  return worked_ha;
}

/// @brief The district's verdict on the year that has just been shipped,
/// and the next year's plan handed down in its place.
///
/// THE CORE'S HALF OF "ПОД СУД" IS THE CONDITION, not the court (epochs
/// design §8; boss, 2026-09-12: "твоё — условие и событие"). The counter
/// reaching its threshold raises kPlanTrialDue once, on the day it
/// reaches it, and the commission, the case and the courtroom belong to
/// the presentation.
///
/// A PLAN OF NOTHING IS NOT A MET PLAN. A world whose tables carry no
/// plan.csv — every unit test's world — is a world with no district, and
/// it must not accumulate a record of triumphs it was never asked for.
/// The verdict is simply not taken there, which is what kNone is for.
void JudgePlan(const ProductionConfig& config, WorldState& current) {
  bool asked = false;
  for (const Grams due : current.plan.due) {
    asked = asked || due > 0;
  }
  // THE YEAR'S PER CENT, FOR THE READINESS INDEX, and it is booked here
  // because this is where the year's delivery is judged — a second walk of
  // the same two vectors somewhere else would be the same number with two
  // homes. The mean over the POSITIONS the district asked for, each capped
  // at 100 so a doubled oat cannot buy a missing wheat, which is the same
  // law the index applies to its own components one storey up.
  //
  // KEYED OFF `announced` AND NOT OFF THE TONNAGE, unlike the verdict below.
  // A settlement that worked no land last year IS one the district spoke to
  // and asked nothing of, and its per cent does not exist — which is a
  // different fact from nought per cent, and the byte is what tells them
  // apart (world_state.h, PlanState::announced).
  if (current.plan.announced != 0) {
    float sum = 0.0F;
    std::uint32_t positions = 0;
    for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
      const Grams due = current.plan.due[index];
      if (due == 0) {
        continue;  // a position asked nothing: it is not a position of this plan
      }
      const Grams delivered =
          index < current.plan.delivered.size() ? current.plan.delivered[index] : 0;
      const float share = static_cast<float>(delivered) / static_cast<float>(due);
      sum += share > 1.0F ? 100.0F : share * 100.0F;
      ++positions;
    }
    // A PLAN OF NO POSITIONS HAS NO PER CENT. The district spoke and asked
    // for nothing; a mean over an empty set would be nought — the score of a
    // settlement that shipped none of what it owed — or a hundred, its
    // opposite, and both would be an answer where there is no question.
    current.ledger.current.plan_percent_known = positions > 0 ? 1U : 0U;
    current.ledger.current.plan_percent =
        positions > 0 ? sum / static_cast<float>(positions) : 0.0F;
  }
  if (asked) {
    const bool met = PlanWasMet(config, current);
    current.plan.last_verdict = met ? PlanVerdict::kMet : PlanVerdict::kFailed;
    current.plan.failed_years_in_a_row =
        met ? 0U : static_cast<std::uint8_t>(current.plan.failed_years_in_a_row + 1U);
    current.plan.met_years_in_a_row =
        met ? static_cast<std::uint8_t>(current.plan.met_years_in_a_row + 1U) : 0U;
    const float step = met ? config.plan_met_reputation : config.plan_failed_reputation;
    // Clamped to the metric's own scale, both ends. A reputation that
    // walked past 100 on a run of good years would make the fall back
    // through the bands take years of nothing happening — the band table
    // of district design §5 is read off this number, so the number has
    // to mean what the table says it means.
    const float moved = current.chairman.raikom_reputation + step;
    current.chairman.raikom_reputation =
        moved < kMetricMin ? kMetricMin : (moved > kMetricMax ? kMetricMax : moved);
    SimEvent& judged = EmitEvent(
        current, met ? EventKind::kPlanMet : EventKind::kPlanFailed, EventSeverity::kNotable);
    judged.amount = met ? current.plan.met_years_in_a_row : current.plan.failed_years_in_a_row;
    // ON THE DAY IT REACHES THE THRESHOLD AND NOT AFTERWARDS: the
    // equality rather than >= is what keeps a fourth failed year from
    // announcing the same news again. A condition that re-announces
    // itself every year is an alarm, and this is an event.
    if (current.plan.failed_years_in_a_row == config.plan_failed_years_to_trial) {
      // kInterrupting, and it is the only one of the three: a met year and
      // a failed year are news the player reads in his own time, while
      // the district deciding to take him to court is the thing a
      // fast-forward must not run past (time design §1).
      SimEvent& trial = EmitEvent(current, EventKind::kPlanTrialDue, EventSeverity::kInterrupting);
      trial.amount = current.plan.failed_years_in_a_row;
    }
    if (!met) {
      // And the raikom comes to ask about it, tomorrow (boss, parcel 324).
      CallPlanFailedVisit(current);
    }
  }
  // THE NEXT NORM IS NOT ANNOUNCED HERE, and what it is computed from has
  // ONE home: PlanState::worked_ha_last_year, written a few lines below.
  // Clearing the old figure is what the year's turn does — an undelivered
  // remainder is a failed year, not a debt carried forward, and the
  // district keeps no tab (district §9).
  //
  // AND THE "THE DISTRICT HAS SPOKEN" BYTE GOES WITH THE FIGURE IT
  // DESCRIBES. It was cleared only in AnnouncePlan for one round, which
  // left the first eight days of every year saying "a figure was named and
  // it asked for nothing" — the one state the byte exists to tell apart
  // from "no figure yet", wrong for a sixth of every year, and a save
  // taken in that window carried the lie across a load.
  //
  // THE FIGURE GOES INTO THE BOOK BEFORE IT GOES (M12, 2026-09-18): the
  // closing year's book keeps what was asked beside what was shipped, lost
  // for want of room and issued, so a failed position can say which of the
  // two took the grain. The book is still the closing year's here — the
  // rotation that closes it runs later in this very tick (world.cpp).
  current.ledger.current.plan_due = current.plan.due;
  current.plan.due.assign(current.plan.due.size(), 0);
  current.plan.announced = 0;
  // The unsealings go with the year they were an emergency of. Carried
  // over, they would quietly become a lower fund instead of a decision
  // somebody took on a particular hungry winter.
  for (ResourceAmounts& opened : current.unsealed.by_fund) {
    opened.assign(opened.size(), 0);
  }
}

bool ParseFirstPlan(const ITableSet& tables, ProductionConfig& config, std::string& error) {
  if (const ITable* const campaign = tables.FindTable("campaign")) {
    float percent = config.first_plan_start_stock_share * 100.0F;
    if (!CellOrDefault(*campaign,
                       campaign->FindRowByKey("first_plan_start_stock_percent"),
                       campaign->FindColumn("value"),
                       Range{.low = 0.0F, .high = 100.0F},
                       percent,
                       percent,
                       error)) {
      error = "campaign: first_plan_start_stock_percent: " + error;
      return false;
    }
    config.first_plan_start_stock_share = percent / 100.0F;
  }
  const ITable* const stock = tables.FindTable("start_stock");
  const ITable* const resources = tables.FindTable("resources");
  if (stock == nullptr || resources == nullptr) {
    return true;  // no start stock: the first plan asks nothing
  }
  const std::uint32_t resource_col = stock->FindColumn("resource");
  const std::uint32_t amount_col = stock->FindColumn("amount");
  const std::uint32_t mass_col = stock->FindColumn("kg_per_unit");
  if (resource_col == kNoTableColumn) {
    return true;
  }
  for (std::uint32_t row = 0; row < stock->RowCount(); ++row) {
    const std::uint32_t resource_row = resources->FindRowByKey(stock->CellText(row, resource_col));
    if (resource_row == kNoTableRow) {
      continue;
    }
    float amount = 0.0F;
    float kilograms_each = 0.0F;
    if (!CellOrDefault(*stock, row, amount_col, Range::NonNegative(), 0.0F, amount, error) ||
        !CellOrDefault(*stock, row, mass_col, Range::NonNegative(), 0.0F, kilograms_each, error)) {
      error = "start_stock: row " + std::to_string(row) + ": " + error;
      return false;
    }
    AddToStock(config.start_stock,
               DefIdFromRow<ResourceIdTag>(resource_row),
               GramsFromKilograms(amount * kilograms_each));
  }
  return true;
}

}  // namespace core
