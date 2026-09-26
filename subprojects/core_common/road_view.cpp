#include "core_common/road_view.h"

#include <cmath>
#include <cstddef>
#include <utility>

#include "core_common/geometry.h"

namespace core {

std::vector<RoadView> RoadViews(const RoadTable& roads) {
  std::vector<RoadView> views;
  views.reserve(roads.rows.size());
  for (std::size_t row = 0; row < roads.rows.size(); ++row) {
    const RoadRow& road = roads.rows[row];
    RoadView view;
    view.road = roads.row_ids[row];
    view.kind = road.kind;
    view.surface = road.surface;
    view.origin = road.origin;
    view.map_road = road.map_road;
    view.removable = road.removable;
    view.traffic_word = road.traffic_word;
    view.axis.reserve(road.axis.size());
    float walked_m = 0.0F;
    for (std::size_t index = 0; index < road.axis.size(); ++index) {
      if (index > 0) {
        // The very sum RoadAxisLength and the graph's chainage make
        // (road_graph.cpp), so `s` here and a piece's chainage there agree
        // to the bit.
        const float dx = road.axis[index - 1].position.x - road.axis[index].position.x;
        const float dy = road.axis[index - 1].position.y - road.axis[index].position.y;
        walked_m += std::sqrt((dx * dx) + (dy * dy));
      }
      view.axis.push_back(RoadAxisPoint{
          .position = road.axis[index].position, .s_m = walked_m, .mark = road.axis[index].mark});
    }
    view.wear_pct.reserve(road.stretches.size());
    for (const RoadStretch& stretch : road.stretches) {
      view.wear_pct.push_back(stretch.wear_pct);
    }
    views.push_back(std::move(view));
  }
  return views;
}

}  // namespace core
