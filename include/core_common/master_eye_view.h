/// @file
/// @brief «Хозяйский глаз» as the office reads it (#636; role-lines §15,
///        «Ступени открывает сама метрика, перк — только раньше»): the
///        metric in years of attention, the rung it has opened by itself,
///        and the thresholds. The perk's half is the layer's: it shows rung n
///        when `rung` ≥ n OR the perk is bought — the core has no perks.
/// @threading SINGLE_THREADED
/// Plain data, built on the sim thread between steps and copied out.

#ifndef CORE_COMMON_MASTER_EYE_VIEW_H_
#define CORE_COMMON_MASTER_EYE_VIEW_H_

#include <array>
#include <cstddef>
#include <cstdint>

namespace core {

inline constexpr std::size_t kMasterEyeRungs = 5;

struct MasterEyeView {
  /// ChairmanState::master_eye: years of attention, 0 at the start.
  float years = 0.0F;

  /// The highest rung n (1..5) whose threshold `years` has reached; 0 none.
  std::uint8_t rung = 0;

  /// world_params eye_rung_1_years … eye_rung_5_years (STUB, econ's).
  std::array<float, kMasterEyeRungs> thresholds{};

  /// The avatar's eye_growth_factor (×1 with no avatar).
  float speed = 1.0F;
};

}  // namespace core

#endif  // CORE_COMMON_MASTER_EYE_VIEW_H_
