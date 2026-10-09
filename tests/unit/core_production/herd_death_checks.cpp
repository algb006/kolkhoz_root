// The checks of a starved herd's death severity (herd_death_checks.h).

#include "herd_death_checks.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/event_state.h"
#include "core_common/herd_state.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "district_limit.h"
#include "herd_life.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// The severity of the kHerdDied one starving day says for a herd of ten,
/// kolkhoz or household; kEventSeverityCount when no death was said.
core::EventSeverity DeathSeverity(bool household) {
  core::ProductionConfig config;
  config.livestock.resize(1);
  config.farming.unfed_death_after_days = 0;
  config.farming.unfed_death_percent_per_day = 50.0F;
  core::WorldState world;
  core::HerdRow herd;
  herd.adult_count = 10;
  herd.unfed_days = 3;
  herd.household_owned = household ? 1U : 0U;
  core::YearLedger book;
  core::RunHungerDeaths(config, config.livestock[0], herd, core::HerdId{7}, world, book);
  for (const core::SimEvent& event : world.step_events) {
    if (event.kind == core::EventKind::kHerdDied) {
      return event.severity;
    }
  }
  return core::EventSeverity::kEventSeverityCount;
}

/// The heads hatched in one day of the season by a family's ten hens, the
/// family in a house or in a barrack (0.37.205; RunBirths).
std::uint16_t HatchedInADay(bool in_barrack) {
  core::ProductionConfig config;
  config.livestock.resize(1);
  core::LivestockDef& hen = config.livestock[0];
  hen.births_per_game_year = 4.0F;
  hen.litter_heads = 1.0F;
  hen.household_cap_heads = 12.0F;
  config.farming.birth_from_month = 0;
  config.farming.birth_to_month = 0;
  core::WorldState world;
  core::FamilyRow family;
  family.in_barrack = in_barrack ? 1U : 0U;
  core::HerdRow herd;
  herd.household = core::AppendRow(world.families, family);
  herd.household_owned = 1;
  herd.adult_count = 10;
  core::YearLedger book;
  core::RunBirths(
      config, hen, core::LivestockKindId{0}, herd, core::HerdId{7}, true, false, world, book);
  return herd.juvenile_count;
}

/// THE SHIPPED CHICK LOT CAN BE ORDERED, AND ONLY INTO A CHICKEN FARM WITH
/// ROOM (0.37.207). Until then limit_lot_livestock.csv left its head count
/// empty: the loader skipped the lot and every order for it was refused
/// kRuleForbids — a village could buy no hens at all (slice 9 of the static
/// pass). With boss's 50 the lot is a lot; the door's own rule (district_
/// limit.cpp: the kind's home unit must hold the whole lot) then decides.
int CheckTheShippedChickLot() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "chick lot: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ITable* const lots = tables->FindTable("limit_catalog");
  const core::ITable* const unit_types = tables->FindTable("unit_types");
  const std::uint32_t lot_row =
      lots == nullptr ? core::kNoTableRow : lots->FindRowByKey("chick_lot");
  const std::uint32_t farm_row =
      unit_types == nullptr ? core::kNoTableRow : unit_types->FindRowByKey("chicken_farm");
  if (Expect(lot_row != core::kNoTableRow && farm_row != core::kNoTableRow &&
                 lot_row < config.limit.lots.size(),
             "chick lot: the shipped tables name the lot and the chicken farm") != 0) {
    return failures + 1;
  }
  const core::LimitLotId lot{static_cast<std::uint16_t>(lot_row)};
  failures += Expect(
      core::LotOrderable(config.limit, lot, core::Epoch::kOne) == core::OrderRefusal::kNone &&
          config.limit.lots[lot_row].head_count == 50,
      "chick lot: the shipped lot is fifty heads and may be ordered in Epoch I");
  const auto village = [farm_row](bool with_farm) {
    core::WorldState world;
    world.epoch = core::Epoch::kOne;
    world.limit.points = 100;
    for (int family = 0; family < 60; ++family) {
      core::AppendRow(world.families, core::FamilyRow{});
    }
    if (with_farm) {
      core::UnitRow farm;
      farm.type = core::UnitTypeId{static_cast<std::uint16_t>(farm_row)};
      farm.level = 1;
      core::AppendRow(world.units, farm);
    }
    return world;
  };
  core::OrderRow buy;
  buy.kind = core::OrderKind::kOrderLimitLot;
  buy.lot = lot;
  core::WorldState farmless = village(false);
  failures +=
      Expect(core::OrderLimitLot(config, farmless, buy) == core::OrderRefusal::kNoRoomForStock &&
                 farmless.limit.points == 100 && farmless.livestock_arrivals.rows.empty(),
             "chick lot: with no chicken farm the order is refused for want of room and "
             "costs nothing");
  core::WorldState with_farm = village(true);
  failures +=
      Expect(core::OrderLimitLot(config, with_farm, buy) == core::OrderRefusal::kNone &&
                 with_farm.limit.points < 100 && with_farm.livestock_arrivals.rows.size() == 1,
             "chick lot: with a level-1 chicken farm standing empty the fifty are sold and "
             "on their way");
  return failures;
}

}  // namespace

int CheckAStarvedKolkhozHerdDoesNotInterruptAgain() {
  int failures = 0;
  failures += CheckTheShippedChickLot();
  // The barrack's flock (0.37.205): the same hens hatch in a yard and not in
  // a barrack. The yard's count is the instrument's own proof — a rate of
  // nought would pass the barrack's line with nothing measured.
  const std::uint16_t in_yard = HatchedInADay(false);
  failures += Expect(in_yard == 10,
                     "barrack flock: ten hens of a yard hatch ten in a day of this "
                     "season (the check's own ground)");
  failures += Expect(HatchedInADay(true) == 0,
                     "barrack flock: a family's hens do not breed in a barrack — never more than "
                     "the family brought");
  failures += Expect(DeathSeverity(false) == core::EventSeverity::kNotable,
                     "herd death: a kolkhoz herd's death of hunger is notable — its episode "
                     "interrupted once, by kHerdWentHungry");
  failures += Expect(DeathSeverity(true) == core::EventSeverity::kInterrupting,
                     "herd death: a household's herd's death of hunger still interrupts");
  return failures;
}
