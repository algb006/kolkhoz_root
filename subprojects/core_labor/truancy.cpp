#include "truancy.h"

#include <algorithm>
#include <cstdint>

#include "core_common/random.h"
#include "core_common/resident_state.h"
#include "labor_day.h"

namespace core {
namespace {

/// Epoch I's alcoholism cap (crime §6; alcoholism.h): the chance's top end.
constexpr float kAlcoholismCap = 60.0F;

/// The drinker's own age (crime §6: «мужчины 16+»).
constexpr float kTruantFromYears = 16.0F;

/// The counter-hash salt of the truancy draw: its own stream, so a truant moves
/// no other draw in the world.
constexpr std::uint64_t kTruancyStream = 0x5452554E54ULL;  // "TRUNT"

}  // namespace

float TruancyChance(const LaborConfig& config, const WorldState& current, std::uint32_t row) {
  if (!config.truancy_on || row >= current.residents.rows.size()) {
    return 0.0F;
  }
  const ResidentRow& resident = current.residents.rows[row];
  if (resident.sex != Sex::kMale || resident.alcoholism < config.truancy_from_alcoholism ||
      BiologicalAgeYears(config, resident.birth_day, current.calendar.day) < kTruantFromYears) {
    return 0.0F;
  }
  const float span = kAlcoholismCap - config.truancy_from_alcoholism;
  const float over =
      span > 0.0F ? (resident.alcoholism - config.truancy_from_alcoholism) / span : 1.0F;
  return config.truancy_share_at_cap * std::clamp(over, 0.0F, 1.0F);
}

bool TruantToday(const LaborConfig& config, const WorldState& current, std::uint32_t row) {
  const float chance = TruancyChance(config, current, row);
  if (!(chance > 0.0F)) {
    return false;
  }
  return CounterHashUnitFloat(current.world_seed,
                              static_cast<std::uint64_t>(current.calendar.day),
                              current.residents.row_ids[row].value,
                              kTruancyStream) < chance;
}

}  // namespace core
