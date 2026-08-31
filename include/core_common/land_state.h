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
/// Phase durations are labor-driven by design (§5): work_days_remaining is
/// the seam between production and labor (manual/65-labor-model.md).
/// Production opens a working phase by setting it to the phase's norm
/// (area x man-days per hectare); the labor sub-step drains it with the
/// crew's hourly output; production advances the phase when it reaches 0.
/// The two modules never call each other — the state carries the contract.

#ifndef CORE_COMMON_LAND_STATE_H_
#define CORE_COMMON_LAND_STATE_H_

#include <cstdint>

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

/// @brief What kind of land the row is (boss answer to question Q6,
/// 2026-08-31). A MEADOW IS NOT A CROP: grass is mown where it grew, it is
/// not sown into a rotation, and it has no fertility to improve or exhaust.
/// Modelling it as a perennial crop field gave the run 200 hectares whose
/// fertility climbed from 55 to 100 and whose hay doubled over thirty years
/// with nobody doing anything (manual/balance/69-reconciliation.md §3 D5).
///
/// The two hay rates live in FarmingConfig, not in a crop row: the design
/// keeps them in prose until a land registry exists.
enum class LandKind : std::uint8_t {
  kArable = 0,        ///< Ploughed land: rotation, fertility, sowing, manure.
  kMeadow,            ///< Natural grassland, mown once a season.
  kFloodplainMeadow,  ///< The best grass of the farm; STUB until terrain zones.
  kDerelict,          ///< Arable nobody has raised: weeds and sod, no work until it is (phase 2).
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

  /// The share of the manure norm this field received for the current cycle,
  /// in PERCENT (§8): the bonus at the harvest — or at the year's turn for a
  /// bare fallow — scales by it, then it resets. It was a 0/1 flag until the
  /// fifth reconciliation pass: whole doses or nothing left the biggest field
  /// unmanured for thirty years.
  std::uint8_t manure_applied = 0;

  /// Arable land, meadow or derelict (see LandKind). A meadow ignores every
  /// field above it except `area_ga` and `phase`: no crop, no rotation, no
  /// fertility, no manure. Derelict land is arable that nobody has raised
  /// yet — the start's ninety hectares of weeds and sod: it keeps its
  /// fertility ("the land has rested", start canon §8) and gets no work of
  /// any kind until construction raises it. It is NOT rotation fallow, which
  /// is ploughed every year it stands (defect D11 taught the difference:
  /// ploughing the derelict cost 150 man-days a year nobody had asked for).
  /// The byte sits in the padding the row already had, so sizeof(FieldRow)
  /// is unchanged — but the SAVE STREAM grew by a byte per field, which the
  /// sizeof tripwire cannot see and VERSION_SAVE must (manual/67-save-format.md §7).
  LandKind kind = LandKind::kArable;

  /// Growth-season weather stress, 0..1 accumulated daily while growing
  /// (drought and waterlogging, §6); scales the harvest down, never to zero.
  float weather_stress = 0.0F;

  /// Game man-days of work left in the current working phase — the
  /// production/labor seam (see @file). Set by production at phase open,
  /// drained by labor's assigned crew, phase advances at 0. A phase set
  /// wired without the labor sub-step therefore never finishes a working
  /// phase — deliberately: work does not happen without workers.
  float work_days_remaining = 0.0F;
};

/// @brief The fields table type used by WorldState.
using FieldTable = StateTable<FieldId, FieldRow>;

}  // namespace core

#endif  // CORE_COMMON_LAND_STATE_H_
