// Who is at a funeral today (core_common/funeral_reads.h; #1084).

#include "core_common/funeral_reads.h"

#include <cstdint>

#include "core_common/world_state.h"

namespace core {

std::uint32_t FuneralOfToday(const WorldState& world, ResidentId resident, bool& kin) {
  kin = false;
  const SimDay today = world.calendar.day;
  for (std::uint32_t row = 0; row < world.funerals.rows.size(); ++row) {
    const FuneralRow& funeral = world.funerals.rows[row];
    if (funeral.burial_day != today) {
      continue;
    }
    for (std::uint32_t place = 0; place < funeral.party.size(); ++place) {
      if (funeral.party[place] == resident) {
        kin = place < funeral.kin_count;
        return row;
      }
    }
  }
  return kNoRow;
}

float FuneralNeighbourHours(const FuneralRow& funeral, float walk_hours_per_km) {
  return 2.0F * funeral.road_km * walk_hours_per_km + kFuneralCeremonyHours;
}

}  // namespace core
