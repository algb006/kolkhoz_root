// Where a field camp may be marked (field_camp.h).

#include "field_camp.h"

#include <cmath>
#include <numbers>

#include "core_common/land_state.h"

namespace core {

OrderRefusal FieldCampPlacementRefusal(const ConstructionConfig& config,
                                       const WorldState& world,
                                       Vec2 position) {
  constexpr float kSquareMetresPerHectare = 10000.0F;
  bool near_a_field = false;
  for (const FieldRow& field : world.fields.rows) {
    const float dx = position.x - field.center.x;
    const float dy = position.y - field.center.y;
    const float distance = std::sqrt((dx * dx) + (dy * dy));
    // THE CONTOUR IS A DISC OF THE FIELD'S AREA. The core keeps a field's
    // centre and hectares and no outline (land_state.h); the drawn outline is
    // the map's, and the layer refuses on it as it refuses on the slope.
    if (field.kind == LandKind::kArable) {
      const float radius =
          std::sqrt(field.area_ga * kSquareMetresPerHectare / std::numbers::pi_v<float>);
      if (distance < radius) {
        return OrderRefusal::kOnArable;
      }
    }
    near_a_field = near_a_field || distance <= config.field_camp_field_reach_m;
  }
  return near_a_field ? OrderRefusal::kNone : OrderRefusal::kTooFarFromFields;
}

}  // namespace core
