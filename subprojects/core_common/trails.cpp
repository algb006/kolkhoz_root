/// @file
/// @brief Trodden paths' pure rules (core_common/trail_state.h; #2072).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <vector>

#include "core_common/trail_state.h"

namespace core {

namespace {

bool EndLess(TrailEnd a, TrailEnd b) {
  return std::tie(a.x, a.y) < std::tie(b.x, b.y);
}

bool EndEqual(TrailEnd a, TrailEnd b) {
  return a.x == b.x && a.y == b.y;
}

/// A leg's key: the lesser end first, so a leg walked either way is one row.
std::pair<TrailEnd, TrailEnd> KeyOf(Vec2 from, Vec2 to) {
  const TrailEnd one = SnapTrailEnd(from);
  const TrailEnd two = SnapTrailEnd(to);
  return EndLess(two, one) ? std::pair{two, one} : std::pair{one, two};
}

bool RowLess(const TrailRow& row, const std::pair<TrailEnd, TrailEnd>& key) {
  if (!EndEqual(row.a, key.first)) {
    return EndLess(row.a, key.first);
  }
  return EndLess(row.b, key.second);
}

std::vector<TrailRow>::const_iterator Lower(const std::vector<TrailRow>& rows,
                                            const std::pair<TrailEnd, TrailEnd>& key) {
  return std::lower_bound(rows.begin(), rows.end(), key, RowLess);
}

}  // namespace

TrailEnd SnapTrailEnd(Vec2 point) {
  return TrailEnd{.x = static_cast<std::int32_t>(std::lround(point.x / kTrailSnapMetres)),
                  .y = static_cast<std::int32_t>(std::lround(point.y / kTrailSnapMetres))};
}

Vec2 TrailEndPosition(TrailEnd end) {
  return Vec2{.x = static_cast<float>(end.x) * kTrailSnapMetres,
              .y = static_cast<float>(end.y) * kTrailSnapMetres};
}

float TrailStrength(const TrailRow& row) {
  return std::min(1.0F, row.earth + row.snow);
}

const TrailRow* FindTrail(const TrailTable& trails, Vec2 from, Vec2 to) {
  const auto key = KeyOf(from, to);
  const auto found = Lower(trails.rows, key);
  if (found == trails.rows.end() || !EndEqual(found->a, key.first) ||
      !EndEqual(found->b, key.second)) {
    return nullptr;
  }
  return &*found;
}

void TreadTrail(TrailTable& trails, Vec2 from, Vec2 to, bool over_snow, float gain) {
  const auto key = KeyOf(from, to);
  if (EndEqual(key.first, key.second) || !(gain > 0.0F)) {
    return;
  }
  const auto at = Lower(trails.rows, key);
  const auto offset = std::distance(trails.rows.cbegin(), at);
  auto row = trails.rows.begin() + offset;
  if (row == trails.rows.end() || !EndEqual(row->a, key.first) || !EndEqual(row->b, key.second)) {
    row = trails.rows.insert(row, TrailRow{.a = key.first, .b = key.second});
  }
  float& ground = over_snow ? row->snow : row->earth;
  ground = std::min(1.0F, ground + gain);
}

void DecayTrails(TrailTable& trails, const TrailRules& rules, bool snow_lies, bool snow_falls) {
  const bool snow_erased = snow_falls || !snow_lies;
  for (TrailRow& row : trails.rows) {
    if (snow_erased) {
      row.snow = 0.0F;
    }
    row.earth = std::max(0.0F, row.earth - rules.earth_decay_per_day);
    row.snow = std::max(0.0F, row.snow - rules.snow_decay_per_day);
  }
  // Rule 3: gone completely - no row is kept for a former trail.
  std::erase_if(trails.rows,
                [](const TrailRow& row) { return !(row.earth > 0.0F) && !(row.snow > 0.0F); });
}

float TrailLegWeight(const TrailTable& trails, Vec2 from, Vec2 to, float open_weight) {
  const TrailRow* const row = FindTrail(trails, from, to);
  if (row == nullptr) {
    return open_weight;
  }
  // Never below the full trail's weight, whatever the open ground's (rule 6 holds by
  // weight_full > 1.0, the laid path's).
  const float full = std::min(open_weight, trails.weight_full);
  return open_weight - (TrailStrength(*row) * (open_weight - full));
}

std::vector<TrailView> TrailViews(const TrailTable& trails, bool visible_only) {
  std::vector<TrailView> views;
  for (const TrailRow& row : trails.rows) {
    const float strength = TrailStrength(row);
    const bool visible = strength >= trails.visible_strength;
    if (visible_only && !visible) {
      continue;
    }
    views.push_back(TrailView{.from = TrailEndPosition(row.a),
                              .to = TrailEndPosition(row.b),
                              .strength = strength,
                              .over_snow = row.snow > row.earth,
                              .visible = visible});
  }
  return views;
}

TrailCount CountTrailRows(const TrailTable& trails) {
  TrailCount count;
  for (const TrailRow& row : trails.rows) {
    ++count.all;
    count.visible += TrailStrength(row) >= trails.visible_strength ? 1U : 0U;
  }
  return count;
}

}  // namespace core
