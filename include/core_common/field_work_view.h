/// @file
/// @brief The field-work coordinator's read of one field (#2003; ISession::FieldWorkOf;
///        manual/design/economy/field-work.md): the phase's progress SHARE and its frozen total,
///        the reaping's laid grams and the heap, the seed on the field, and WHICH residents
///        work it today.
///
/// The presentation places the brigade from the progress SHARE, never from a sum over time
/// (ue's rule for the coordinator), so a time skip and a load land it in the same place. Every
/// total here is FROZEN when its phase opens: a total that moved mid-phase would make the
/// brigade jump (the reaping's harvest_work_days, land_state.h; the sowing's since save 223).
/// @threading SINGLE_THREADED
/// A value type, built between steps on the sim thread.

#ifndef CORE_COMMON_FIELD_WORK_VIEW_H_
#define CORE_COMMON_FIELD_WORK_VIEW_H_

#include <vector>

#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One field's work today. `measured` false for a field that does not exist; every
///        other member is then at its default.
struct FieldWorkView {
  bool measured = false;
  FieldId field;
  FieldPhase phase = FieldPhase::kIdle;
  CropId crop;

  // -- the reaping (kHarvest; land_state.h) ----------------------------------
  /// The share of this reaping already laid into the heap, 0-1. Nought outside a reaping.
  float harvest_laid_share = 0.0F;
  /// The reaping's whole work in norm man-days, frozen when it opened. Nought outside a
  /// reaping.
  float harvest_work_days = 0.0F;
  /// Grams the reaping has laid so far.
  Grams harvest_laid_grams = 0;

  // -- the heap at the field, waiting for its carts --------------------------
  Grams heap_grams = 0;      ///< FieldRow::reaped_grams
  ResourceId heap_resource;  ///< What lies there; invalid exactly when heap_grams is 0.

  // -- the sowing (kSowing) ---------------------------------------------------
  /// The sowing's whole work in norm man-days, frozen when it opened (save 223). Nought
  /// outside a sowing.
  float sowing_work_days = 0.0F;
  /// The share of the sowing done, 0-1. Nought outside a sowing.
  float sowing_done_share = 0.0F;
  /// THE SEED ON THE FIELD, grams. STUB: area x the crop's seed norm x sowing_done_share.
  /// The core books the seed out of the stores when the sowing CLOSES (field_work.cpp,
  /// FinishSowing) and holds no seed «delivered to the field» in between; this is the
  /// presentation's estimate of what the sowers have put in. RETURNS to a stock when a seed
  /// haul (stores -> field) exists.
  Grams seed_on_field_grams = 0;

  // -- the hands ---------------------------------------------------------------
  /// The residents whose assignment targets this field this step (the field kinds and the
  /// haul off its heap), in row order. Its size is FieldSignals::residents_working.
  std::vector<ResidentId> residents;
};

}  // namespace core

#endif  // CORE_COMMON_FIELD_WORK_VIEW_H_
