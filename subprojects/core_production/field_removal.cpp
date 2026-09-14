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
    return OrderRefusal::kWrongLand;
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
