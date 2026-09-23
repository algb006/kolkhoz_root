/// @file
/// @brief TimberStandRow — a place timber can be taken from: its standing
/// stock, what the chairman marked for felling, and the logs lying there.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::stands under the double-buffer discipline. Two
/// writers, both sequential sub-steps of the decisions slot (phase 3):
///   * production decisions — reads the felling order, fells what the crew
///     finished, adds the old forest's yearly trunks at the turn, and sizes
///     and settles the carting of the logs (the same settlement the fields'
///     loads go through);
///   * labor — `work_days_remaining` and `haul_days_remaining`, drained by
///     the people on them, and nothing else.
/// No parallel phase touches a stand.
///
/// Design source: timber design §8a (boss, 2026-09-13), "Порубка в ядре:
/// форма и предварительные числа". THE STOCK IS KEPT PER STAND, NOT PER TREE:
/// the trees are the graphics layer's, and tens of thousands of trunks in a
/// save to answer "fell this grove or not" is a model that gives no decision.
///
/// WHY A TABLE OF ITS OWN AND NOT A KIND OF FIELD (boss, parcel 185, "путь
/// Б"). A field row would have carried the carting for free — and every
/// reader of the fields, in the core and across the boundary, would have had
/// to learn that a field is sometimes a grove: the district's arable, the plan
/// alarm's hectares, the ledger's areas, the field cards the layer draws. A
/// field must keep meaning a field. What is shared is the MECHANISM of the
/// carting, not the row.
///
/// WHAT IS NOT HERE, because it is table data and never changes in play: the
/// area, the share of logs, the source contour. They live in
/// tables/timber_stands.csv, addressed by `table_row`. What is here is what
/// play changes, plus the kind and the loading point every reader needs
/// without opening a table (the accountant's road, the layer's marker).

#ifndef CORE_COMMON_TIMBER_STATE_H_
#define CORE_COMMON_TIMBER_STATE_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief What kind of place the timber comes from (timber design §8a).
enum class TimberStandKind : std::uint8_t {
  /// Trees outside the forest zone (`grove` of the map base). Felled down to
  /// nothing, and the ground is then free land (boss, parcel 182: "рощу на
  /// севере сводят целиком, на вырубке — сад"). Nothing grows back.
  kGrove = 0,

  /// A planted belt along a road. Felled down to nothing, like a grove.
  kShelterbelt,

  /// A square of the forest within a team's reach of a road. THE LIVING
  /// MASSIF IS UNTOUCHABLE (timber design §2): only the old trunks that fall
  /// each year may be taken, and in Epoch I what is not taken vanishes.
  kForestOld,

  /// A zone the chairman planted (kPlantForest; timber_planting.h). Holds
  /// nothing to fell until its matures_day, then as a grove: felled down to
  /// nothing, and the ground free again. Seam key `planted`.
  kPlanted,

  /// NOT A VALUE, and never written to a save or read from one: the codecs
  /// range-check 0..kTimberStandKindCount-1 against it. Values are appended
  /// BEFORE it.
  kTimberStandKindCount,
};

/// A planting's day not yet come (TimberStandRow::planted_day, matures_day).
inline constexpr std::uint32_t kNeverPlanted = 0xFFFFFFFFU;

/// @brief One stand. Plain data.
struct TimberStandRow {
  /// Row of tables/timber_stands.csv this stand was made from: its key,
  /// area and share of logs. Fixed at genesis.
  std::uint32_t table_row = 0;

  TimberStandKind kind = TimberStandKind::kGrove;

  /// The loading point: the stand's ground nearest a road, metres from the
  /// map's south-west corner. The walk to the felling and the haul of the
  /// logs are both measured to it, exactly as a field's are to its centre.
  Vec2 position;

  /// Standing timber that may still be taken, cubic metres. A grove or belt
  /// starts at area × its stock density and only falls; an old-forest stand
  /// starts at zero and gains the year's fallen trunks at every turn, never
  /// holding more than `timber_fallen_vanish_years` of them — a trunk lies
  /// that long and is gone (timber design §8a, "в Эпохе I упавшее исчезает
  /// через 2 года"). Never negative.
  float stock_m3 = 0.0F;

  /// Of the stock, what the chairman has marked for felling and the crew has
  /// not yet felled, cubic metres. 0 <= marked_m3 <= stock_m3. Set by the
  /// felling order (OrderKind::kMarkFelling), cleared when the felling ends.
  float marked_m3 = 0.0F;

  /// The felling seam, game man-days: marked_m3 × timber_felling_days_per_m3
  /// when the order is read, drained by the crew (WorkKind::kFelling). At
  /// zero with timber still marked, production fells it: the stock and the
  /// mark go down and the logs are laid on the stand as a load.
  float work_days_remaining = 0.0F;

  /// Logs lying on the stand, unfetched, grams. The firewood the same trees
  /// give is counted and has no holder yet (STUB, timber design §8a).
  Grams load_grams = 0;

  /// The carting seam, game man-days — the same contract as
  /// FieldRow::haul_days_remaining: production sizes it from the load and the
  /// room the stores can take, labor drains it (WorkKind::kHauling). Zero
  /// whenever `load_grams` is zero.
  float haul_days_remaining = 0.0F;

  /// What the settlement last wrote into the carting seam, so it can tell
  /// what people carried from what the room did. Same contract as
  /// FieldRow::haul_days_written.
  float haul_days_written = 0.0F;

  // -- a planting (kind kPlanted; timber_planting.h; save 82) ---------------
  /// What was planted; invalid for every other kind.
  TreeSpeciesId species;

  /// The zone's hectares — a planting's own, since it has no table row to
  /// read them from. 0 for every other kind (their area is the table's).
  float planted_area_ha = 0.0F;

  /// The day the planting crew finished, kNeverPlanted while it is still
  /// planting (its work_days_remaining is then the PLANTING seam, drained by
  /// WorkKind::kPlanting), and for every other kind.
  std::uint32_t planted_day = kNeverPlanted;

  /// The day the planting reaches its logs (planted_day + the species'
  /// plant_years_to_logs); kNeverPlanted until planted.
  std::uint32_t matures_day = kNeverPlanted;
};

/// @brief The table type used by WorldState.
using TimberStandTable = StateTable<TimberStandId, TimberStandRow>;

}  // namespace core

#endif  // CORE_COMMON_TIMBER_STATE_H_
