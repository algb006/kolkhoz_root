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

/// The straw on the site that the coat may count: the stock less what a works
/// put aside for the winter holds back (#715) - the clay warm rung's recipe
/// carries straw of its own (6 t on the cattle yard's rung 2), and that is
/// the spring's, not the coat's.
Grams StrawOnSite(const ConstructionConfig& config, const UnitRow& site) {
  const Grams held = AmountOf(site.stock, config.straw_resource);
  const Grams aside = site.suspended_works.has_value()
                          ? AmountOf(site.suspended_works->reserved, config.straw_resource)
                          : 0;
  return held > aside ? held - aside : 0;
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
  // WORKS OPEN ON THE UNIT FORBID THE COAT, AND SAY SO (boss, 11 October 2026,
  // form (a1)): a unit carries one construction block, and an upgrade marked,
  // carted to, under labour or stopped by the winter holds it — the straw would
  // overwrite its seam. Its own refusal, the one-active-task rule's, so the
  // player reads «a works stands on it» and not a bare «forbidden»: a pen whose
  // clay warm is open waits for spring; one not yet begun takes the straw.
  //
  // EXCEPT THE CLAY WARM RUNG ITSELF (#715; livestock design «Не успел»: the
  // coat «ставится ВМЕСТО глиняного… глиняное открывается весной»; boss, 12
  // October 2026). Until 0.37.239 the lamp's own advice (kInsulateStraw on
  // herd_freezing) was refused kConflictsWithActive in every village whose
  // pen stood with its warm rung open and winter-stopped. That works is put
  // aside whole (UnitRow::suspended_works) and the coat takes its place; any
  // other open works still refuses.
  const InsulationKind kind = InsulationKindOf(config, site.type, site.level);
  // THE LIVESTOCK COAT IS WITHDRAWN (#979; the human, 13 October 2026,
  // registry 316: «Мне не нравится обмазка… временные решения»; on its scope
  // «Да» - only the yards' coat goes, «Тёплый» of the houses stays). STUB
  // until 795 B's billet into the residents' yards, the winter's answer. A
  // coat a save opened before 0.37.264 still finishes, so the put-aside
  // clay (CoatPutsAsideOpenWorks, ReturnSuspended) keeps its code.
  if (kind == InsulationKind::kLivestock) {
    return OrderRefusal::kRuleForbids;
  }
  const ConstructionState& open = site.construction;
  // The works' own terms have one home (unit_state.h, CoatPutsAsideOpenWorks).
  const bool replaces_clay = kind == InsulationKind::kLivestock && CoatPutsAsideOpenWorks(site);
  if (open.phase != ConstructionPhase::kNone && !replaces_clay) {
    return OrderRefusal::kConflictsWithActive;
  }
  if (site.level == 0 || site.insulated != 0 || config.straw_resource.value == kInvalidDefIdValue) {
    return OrderRefusal::kRuleForbids;
  }
  if (kind == InsulationKind::kNone) {
    return OrderRefusal::kRuleForbids;
  }
  const Grams need = InsulationStrawGrams(config, kind);
  // kStartBuild's rule: the village holds the straw in full at the word — the
  // site's own stock and what every other built unit may give. The clay's
  // own straw on the site is the clay's (StrawOnSite once it is put aside).
  const Grams own = AmountOf(site.stock, config.straw_resource);
  const Grams clays = replaces_clay ? AmountOf(open.reserved, config.straw_resource) : 0;
  Grams held = own > clays ? own - clays : 0;
  for (std::uint32_t other = 0; other < current.units.rows.size(); ++other) {
    if (other != row && current.units.rows[other].level > 0) {
      held += UnreservedOf(current.units.rows[other], config.straw_resource);
    }
  }
  if (held < need) {
    return OrderRefusal::kMaterialsShort;
  }

  UnitRow& opened = current.units.rows[row];
  if (replaces_clay) {
    // Whole: its legs, what it has fixed and is carting, what it holds back
    // and its labour done so far - all of it the spring's (ReturnSuspended).
    opened.suspended_works = std::move(opened.construction);
    opened.construction = ConstructionState{};
  }
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
  if (StrawOnSite(config, opened) >= need) {
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
  const Grams have = StrawOnSite(config, site);
  if (have >= need) {
    return;  // in full already: its labour opened when the last of it came
  }
  // NOT CARTED, NAMED (routing stage V2, 0.37.204): a recipe and a repair's
  // parts are fixed and then carted by the groom (construction_system.cpp,
  // DeliverSite); the straw coat still arrives the tick it is taken. Its
  // «done» reads the seam at 0 with the straw on the unit (InsulationDone),
  // which a straw arriving hours before its labour would satisfy with no
  // work done — it needs its own «labour opened» mark first. STUB core.
  const Grams taken = TakeFromStores(current, row, config.straw_resource, need - have);
  UnitRow& supplied = current.units.rows[row];
  AddTo(supplied.stock, config.straw_resource, taken);
  if (StrawOnSite(config, supplied) >= need) {
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
         StrawOnSite(config, site) >= FrozenStraw(config, site);
}

void CompleteInsulation(const ConstructionConfig& config, WorldState& current, std::uint32_t row) {
  UnitRow& site = current.units.rows[row];
  const Grams straw = FrozenStraw(config, site);
  AddTo(site.stock, config.straw_resource, -straw);
  AddLedgerAmount(current.ledger.current.built_in, config.straw_resource, straw);  // into the walls
  AddBuiltInByType(
      current.ledger.current.built_in_by_type, site.type, config.straw_resource, straw);
  site.insulated = 1;
  FoldAwaitingIntoStock(site);
  site.construction = ConstructionState{};
  SimEvent& event = EmitEvent(current, EventKind::kUnitInsulated, EventSeverity::kNotable);
  event.unit = current.units.row_ids[row];
}

}  // namespace core
