// Rain stops the sowing and the reaping (rain_stops_work.h).

#include "core_common/rain_stops_work.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace core {

namespace {

/// The dry share of the day of the year that `day` (any day count) falls on.
double DryShareOf(const RainDayShares& shares, double day) {
  const auto whole = static_cast<std::int64_t>(std::floor(day));
  const std::int64_t year = kDaysPerYear;
  const auto index = static_cast<std::size_t>(((whole % year) + year) % year);
  return 1.0 - std::clamp(static_cast<double>(shares[index]), 0.0, 1.0);
}

}  // namespace

bool RainStopsWork(Precipitation precipitation, WorkKind kind) {
  return precipitation == Precipitation::kRain &&
         (kind == WorkKind::kSowing || kind == WorkKind::kHarvest);
}

double DryDaysBetween(const RainDayShares& shares, double from, double to) {
  double dry = 0.0;
  double position = from;
  while (position < to) {
    const double day_end = std::floor(position) + 1.0;
    const double segment_end = std::min(day_end, to);
    dry += (segment_end - position) * DryShareOf(shares, position);
    position = segment_end;
  }
  return dry;
}

double CalendarPointAfterDryDays(const RainDayShares& shares, double from, double dry_days) {
  if (!(dry_days > 0.0)) {
    return from;
  }
  if (!std::isfinite(dry_days)) {
    return std::numeric_limits<double>::infinity();  // no pace: the walk would never end
  }
  double dry_per_year = 0.0;
  for (std::uint32_t day = 0; day < kDaysPerYear; ++day) {
    dry_per_year += DryShareOf(shares, static_cast<double>(day));
  }
  if (!(dry_per_year > 0.0)) {
    return std::numeric_limits<double>::infinity();
  }
  double position = from;
  double owed = dry_days;
  while (true) {
    const double day_end = std::floor(position) + 1.0;
    const double rate = DryShareOf(shares, position);
    const double segment = (day_end - position) * rate;
    if (rate > 0.0 && segment >= owed) {
      return position + (owed / rate);
    }
    owed -= segment;
    position = day_end;
  }
}

}  // namespace core
