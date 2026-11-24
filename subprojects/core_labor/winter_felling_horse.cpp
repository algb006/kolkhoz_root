// The winter horse for the fellers (winter_felling_horse.h; econ's #2072 rule, Alex #2401).

#include "winter_felling_horse.h"

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"
#include "core_common/unit_state.h"

namespace core {
namespace {

/// November (10) to February (1), month 0 = January, as the felling policy reads them.
bool InWinterHorseMonths(const WorldState& world) {
  const std::uint32_t month = (world.calendar.day % kDaysPerYear) / kDaysPerMonth;
  return month >= 10U || month <= 1U;
}

Grams LogsOf(const ResourceAmounts& amounts, ResourceId log) {
  return log.value < amounts.size() ? amounts[log.value] : Grams{0};
}

}  // namespace

bool WinterFellingHorseDay(const WorldState& world, const TimberCatalog& timber) {
  if (!InWinterHorseMonths(world) || timber.log_resource.value == kInvalidDefIdValue) {
    return false;
  }
  bool marked = false;
  Grams in_hand = 0;
  for (const TimberStandRow& stand : world.stands.rows) {
    marked = marked || (stand.marked_m3 > 0.0F && stand.work_days_remaining > 0.0F);
    in_hand += stand.load_grams;
  }
  if (!marked) {
    return false;
  }
  for (const UnitRow& unit : world.units.rows) {
    if (unit.level != 0) {
      in_hand += LogsOf(unit.stock, timber.log_resource);
    }
  }
  const Grams want = LogsOf(world.ledger.closed.built_in, timber.log_resource) +
                     LogsOf(world.ledger.closed.processed, timber.log_resource);
  return in_hand < want;
}

bool YieldsTheWinterHorse(const WorldState& world, const AssignmentJob& job) {
  if (job.kind != WorkKind::kHauling || job.stand.value != kInvalidEntityIdValue ||
      job.herd.value != kInvalidEntityIdValue) {
    return false;
  }
  if (job.unit.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(world.units, job.unit);
    if (row != kNoRow && RickSupplyStands(world.units.rows[row])) {
      return false;
    }
  }
  return true;
}

}  // namespace core
