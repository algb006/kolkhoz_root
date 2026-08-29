/// @file
/// @brief FieldRow — the per-field state: land, crop, phase, fertility.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::fields under the double-buffer discipline: the
/// field is a unit of parallelism of the production phase (phase 4), so a
/// worker owns whole field rows; structural transitions — sowing, harvest,
/// loss, contour changes — happen only in the sequential production
/// decisions sub-step.
///
/// Design sources: farming design §2 (fertility 0-100 per field), §4-§5
/// (windows, temperature thresholds, the six phases), §6 (snow is the one
/// total loss), §7 (three-year rotation), §8 (manure is plowed in).
///
/// Fertility scale: 0-100, 50 is the neutral soil factor (yield x1.0), so
/// the soil multiplier is fertility / 50 — the reference runs' starting
/// multiplier 1.3 is fertility 65. The scale choice is the core's
/// (manual/64-land-model.md); what changes fertility is the design's.
///
/// Phase durations are labor-driven by design (§5). Until the labor system
/// exists (stage 5), transitions are instant at the calendar windows —
/// work_days_remaining is the STUB hook labor will start filling.

#ifndef CORE_COMMON_LAND_STATE_H_
#define CORE_COMMON_LAND_STATE_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief The six field phases (farming design §5). One phase at a time;
/// they cycle and never skip.
enum class FieldPhase : std::uint8_t {
  kIdle = 0,  ///< Stubble, fallow or waiting. Contour edits allowed here only.
  kPlowing,   ///< Manure is plowed in here, never spread separately.
  kHarrowing,
  kSowing,   ///< Consumes ordinary grain/potatoes of the crop (§7).
  kGrowing,  ///< No field work; winter crops sit out the winter here.
  kHarvest,  ///< The heaviest phase of the year.
};

/// @brief One field. Plain data.
struct FieldRow {
  /// Center of the contour. The shape itself is presentation/routing data
  /// (project phase 2); the core needs only a location.
  Vec2 center;

  float area_ga = 0.0F;

  /// Soil fertility, 0-100; 50 is the neutral yield factor (see @file).
  Metric fertility = 50.0F;

  FieldPhase phase = FieldPhase::kIdle;

  /// The crop currently in the ground (sown, growing or being harvested);
  /// invalid when idle or fallow.
  CropId crop;

  /// The three-year rotation the player (or genesis) assigned: what to sow
  /// this year, next year, the year after. Invalid = fallow that year.
  CropId rotation_year0;

  CropId rotation_year1;

  CropId rotation_year2;

  /// The crop harvested last year and how many years in a row it repeated —
  /// the rotation memory behind the repeat penalty (§7).
  CropId last_crop;

  std::uint8_t repeat_years = 0;

  /// Manure was plowed in for the current cycle (§8): one fertility bonus
  /// at the year's close, then the flag resets.
  std::uint8_t manure_applied = 0;

  /// Growth-season weather stress, 0..1 accumulated daily while growing
  /// (drought and waterlogging, §6); scales the harvest down, never to zero.
  float weather_stress = 0.0F;

  /// STUB: game days of work left in the current working phase. The labor
  /// system (stage 5) computes and drains this; until then it stays 0 and
  /// working phases pass instantly.
  float work_days_remaining = 0.0F;
};

/// @brief The fields table type used by WorldState.
using FieldTable = StateTable<FieldId, FieldRow>;

}  // namespace core

#endif  // CORE_COMMON_LAND_STATE_H_
