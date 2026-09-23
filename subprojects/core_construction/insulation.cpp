// The straw insulation of Epoch I (core_construction/insulation.h).

#include "insulation.h"

#include <cmath>
#include <cstdint>

#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/state_table_ops.h"
#include "site_supply.h"

namespace core {
namespace {

/// The straw the site's order froze (StartInsulation writes it into
/// `reserved`, and nothing else touches a kInsulating site's reserved line).
Grams FrozenStraw(const ConstructionConfig& config, const UnitRow& site) {
  return AmountOf(site.construction.reserved, config.straw_resource);
}

/// Opens the man-days once the straw is all on site.
void OpenLabour(UnitRow& site) {
  site.construction.labor_days_remaining = site.construction.labor_days_total;
}

}  // namespace

InsulationKind InsulationKindOf(const ConstructionConfig& config,
                                UnitTypeId type,
                                std::uint8_t level) {
  if (type.value >= config.types.size() || level == 0 ||
      level > config.types[type.value].levels.size()) {
    return InsulationKind::kNone;
  }
  const bool heated =
      type.value < config.type_has_heating.size() && config.type_has_heating[type.value] != 0;
  const bool for_animals =
      config.types[type.value].levels[level - 1].livestock_capacity_head > 0.0F;
  if (!heated && !for_animals) {
    return InsulationKind::kNone;
  }
  const std::vector<std::uint8_t>& housing = config.definitions.units.is_housing;
  if (type.value < housing.size() && housing[type.value] != 0) {
    return InsulationKind::kHousing;
  }
  return for_animals ? InsulationKind::kLivestock : InsulationKind::kHeated;
}

Grams InsulationStrawGrams(const ConstructionConfig& config, InsulationKind kind) {
  float tonnes = 0.0F;
  switch (kind) {
    case InsulationKind::kHousing:
      tonnes = config.insulation_housing_straw_t;
      break;
    case InsulationKind::kLivestock:
      tonnes = config.insulation_livestock_straw_t;
      break;
    case InsulationKind::kHeated:
      tonnes = config.insulation_heated_straw_t;
      break;
    case InsulationKind::kNone:
      break;
  }
  return static_cast<Grams>(
      std::llround(static_cast<double>(tonnes) * static_cast<double>(kGramsPerTonne)));
}

float InsulationLaborDays(const ConstructionConfig& config, InsulationKind kind) {
  switch (kind) {
    case InsulationKind::kHousing:
      return config.insulation_housing_labor_days;
    case InsulationKind::kLivestock:
      return config.insulation_livestock_labor_days;
    case InsulationKind::kHeated:
      return config.insulation_heated_labor_days;
    case InsulationKind::kNone:
      break;
  }
  return 0.0F;
}

OrderRefusal StartInsulation(const ConstructionConfig& config, WorldState& current, UnitId unit) {
  const std::uint32_t row = FindRow(current.units, unit);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  const UnitRow& site = current.units.rows[row];
  if (site.level == 0 || site.construction.phase != ConstructionPhase::kNone ||
      site.insulated != 0 || config.straw_resource.value == kInvalidDefIdValue) {
    return OrderRefusal::kRuleForbids;
  }
  const InsulationKind kind = InsulationKindOf(config, site.type, site.level);
  if (kind == InsulationKind::kNone) {
    return OrderRefusal::kRuleForbids;
  }
  const Grams need = InsulationStrawGrams(config, kind);
  // kStartBuild's rule: the village holds the straw in full at the word — the
  // site's own stock and what every other built unit may give.
  Grams held = AmountOf(site.stock, config.straw_resource);
  for (std::uint32_t other = 0; other < current.units.rows.size(); ++other) {
    if (other != row && current.units.rows[other].level > 0) {
      held += UnreservedOf(current.units.rows[other], config.straw_resource);
    }
  }
  if (held < need) {
    return OrderRefusal::kMaterialsShort;
  }

  UnitRow& opened = current.units.rows[row];
  opened.construction.phase = ConstructionPhase::kInsulating;
  opened.construction.target_level = opened.level;
  opened.construction.labor_days_total = InsulationLaborDays(config, kind);
  opened.construction.labor_days_remaining = 0.0F;  // opened with the straw
  const BuildLevel& step = config.types[opened.type.value].levels[opened.level - 1];
  opened.construction.max_crew = step.max_crew;
  // STRAW WORK AND NOT THE HOUSE'S CLASS: laying the straw coat on a
  // standing building is the winter's own job — it is done against the cold
  // — whatever the walls under it were built of. A reading, named: the
  // design's seasons table names masonry and earthworks, not insulation.
  opened.construction.winter_works = 1;
  opened.construction.reserved.clear();
  AddTo(opened.construction.reserved, config.straw_resource, need);
  if (AmountOf(opened.stock, config.straw_resource) >= need) {
    OpenLabour(opened);
    return OrderRefusal::kNone;
  }
  // Carried in the same tick, as a start's recipe is: the check above
  // guarantees nothing a day later otherwise.
  DeliverInsulation(config, current, row);
  return OrderRefusal::kNone;
}

void DeliverInsulation(const ConstructionConfig& config, WorldState& current, std::uint32_t row) {
  const UnitRow& site = current.units.rows[row];
  const Grams need = FrozenStraw(config, site);
  const Grams have = AmountOf(site.stock, config.straw_resource);
  if (have >= need) {
    return;  // in full already: its labour opened when the last of it came
  }
  const Grams taken = TakeFromStores(current, row, config.straw_resource, need - have);
  UnitRow& supplied = current.units.rows[row];
  AddTo(supplied.stock, config.straw_resource, taken);
  if (AmountOf(supplied.stock, config.straw_resource) >= need) {
    OpenLabour(supplied);
  }
}

bool InsulationDone(const ConstructionConfig& config,
                    const WorldState& current,
                    std::uint32_t row) {
  // Both halves, because the seam reads 0 twice in the job's life: before the
  // straw is in (nothing opened yet) and after the labour (all drained).
  const UnitRow& site = current.units.rows[row];
  return site.construction.phase == ConstructionPhase::kInsulating &&
         site.construction.labor_days_remaining <= 0.0F &&
         AmountOf(site.stock, config.straw_resource) >= FrozenStraw(config, site);
}

void CompleteInsulation(const ConstructionConfig& config, WorldState& current, std::uint32_t row) {
  UnitRow& site = current.units.rows[row];
  const Grams straw = FrozenStraw(config, site);
  AddTo(site.stock, config.straw_resource, -straw);
  AddLedgerAmount(current.ledger.current.built_in, config.straw_resource, straw);  // into the walls
  site.insulated = 1;
  site.construction = ConstructionState{};
  SimEvent& event = EmitEvent(current, EventKind::kUnitInsulated, EventSeverity::kNotable);
  event.unit = current.units.row_ids[row];
}

}  // namespace core
