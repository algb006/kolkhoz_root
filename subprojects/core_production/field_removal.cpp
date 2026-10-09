// Taking a field off the map (field_removal.h).

#include "field_removal.h"

#include <cstdint>

#include "core_common/emit_event.h"
#include "core_common/land_state.h"
#include "core_common/state_table_ops.h"

namespace core {

OrderRefusal RemoveField(WorldState& current, const OrderRow& order) {
  const std::uint32_t row = FindRow(current.fields, order.field);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  const FieldRow& field = current.fields.rows[row];
  if (field.kind != LandKind::kArable) {
    // A MEADOW IS UNMARKED LIKE A FIELD since 0.37.211 (Livestock design §5:
    // «снять разметку — всегда и даром»; boss, 10 October 2026, ruling (e) —
    // the start's meadows too: with kMarkMeadow to mark it back the removal
    // is no longer a one-way loss). It was refused kWrongLand until then —
    // «a meadow is grass that grew there, not a contour anybody drew».
    // STANDING GRASS GOES WITH THE ROW («некошеный луг ничего не стоит и
    // ничего не даёт»), whatever the phase; MOWN HAY DOES NOT VANISH: while
    // it lies on the meadow waiting for the carts the removal is refused,
    // the rule a field with reaped grain has.
    if (field.reaped_grams > 0) {
      return OrderRefusal::kNotEmpty;
    }
    RemoveRow(current.fields, order.field);
    SimEvent& unmarked = EmitEvent(current, EventKind::kFieldRemoved);
    unmarked.field = order.field;
    unmarked.amount = 0;
    return OrderRefusal::kNone;
  }
  // BREAD STANDS: ripe and being reaped, or reaped and not yet carted. The
  // design lets a growing field go with what was put into it and refuses
  // only the harvest ("игра просто не даст удалить поле, на котором стоит
  // хлеб"), which in the core is these two states and no other.
  if (field.phase == FieldPhase::kHarvest || field.reaped_grams > 0) {
    return OrderRefusal::kNotEmpty;
  }
  const std::uint8_t reserve = field.start_reserve;
  RemoveRow(current.fields, order.field);
  SimEvent& removed = EmitEvent(current, EventKind::kFieldRemoved);
  removed.field = order.field;
  removed.amount = reserve;
  return OrderRefusal::kNone;
}

}  // namespace core
