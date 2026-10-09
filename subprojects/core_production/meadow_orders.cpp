// Production's half of marking a meadow (meadow_orders.h).
#include "meadow_orders.h"

#include "core_common/calendar.h"
#include "timber_planting.h"

namespace core {

MeadowMarkGround MeadowGroundOf(const ProductionConfig& config,
                                const WorldState& world,
                                bool with_raster,
                                std::vector<float>& stand_radii) {
  MeadowMarkGround ground;
  if (config.meadow_ground) {
    ground = config.meadow_ground(with_raster);
  }
  // The plots and the map's edge are this config's own, read from the same
  // definitions construction reads — and they stand with no map at all.
  ground.plot_radius_m = config.plot_radius_m;
  ground.map_side_m = config.map_side_m;
  stand_radii.clear();
  stand_radii.reserve(world.stands.rows.size());
  for (const TimberStandRow& stand : world.stands.rows) {
    stand_radii.push_back(StandContourRadiusM(config, stand));
  }
  ground.stand_radius_m = &stand_radii;
  ground.mow_days_per_ha =
      config.farming.meadow_mow_days_per_ha * static_cast<float>(kRealDaysPerGameDay);
  ground.dry_yield_kg_per_ha = config.farming.meadow_yield_kg_per_ha;
  ground.floodplain_yield_kg_per_ha = config.farming.meadow_floodplain_yield_kg_per_ha;
  return ground;
}

MeadowMarkAnswer PreviewMeadow(const ProductionConfig& config,
                               const WorldState& world,
                               Vec2 position,
                               float area_ha,
                               FieldId field) {
  std::vector<float> stand_radii;
  return PreviewMeadowMark(
      MeadowGroundOf(config, world, true, stand_radii), world, position, area_ha, field);
}

OrderRefusal OrderMarkMeadow(const ProductionConfig& config,
                             WorldState& current,
                             const OrderRow& order) {
  std::vector<float> stand_radii;
  return MarkMeadow(MeadowGroundOf(config, current, true, stand_radii), current, order);
}

}  // namespace core
