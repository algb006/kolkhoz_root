/// @file
/// @brief ExtractionSiteRow — a marked plot on the map where clay, stone or
/// sand is dug: its stock, what the chairman marked for digging, and what
/// lies dug and unfetched.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::extraction_sites under the double-buffer
/// discipline. Two writers, both sequential sub-steps of the decisions slot
/// (phase 3):
///   * production decisions — reads the marking order, lays what the crew dug
///     on the site as a load, announces an exhausted site, and sizes and
///     settles the carting of the load (the same settlement a field's and a
///     timber stand's loads go through);
///   * labor — `work_days_remaining` and `haul_days_remaining`, drained by the
///     people on them, and nothing else.
/// No parallel phase touches a site.
///
/// Design source: construction design §3, "Добыча на карте: камень, глина,
/// песок"; the form and the preliminary numbers are boss's decision of
/// 2026-09-14 (parcel 270), made on the model of the timber felling (timber
/// design §8a, core_common/timber_state.h). It came of a measurement: once no
/// house came from nothing, the thirty-year run's house sites stood waiting
/// for clay, and the core had no source of clay at all.
///
/// WHY A TABLE OF ITS OWN, and not a kind of field or a kind of stand, for the
/// reason the stands have one: every reader of a field would have had to learn
/// that a field is sometimes a clay pit, and a stand's stock is cubic metres of
/// timber with a share of logs. What is shared is the MECHANISM of the
/// carting, not the row.
///
/// WHAT IS NOT HERE, because it is table data and never changes in play: the
/// area and the source contour (tables/extraction_sites.csv, addressed by
/// `table_row`), the stock density and the labour per tonne (world_params).
/// What is here is what play changes, plus the resource and the loading point
/// every reader needs without opening a table.

#ifndef CORE_COMMON_EXTRACTION_STATE_H_
#define CORE_COMMON_EXTRACTION_STATE_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief One plot clay, stone or sand is dug on. Plain data.
struct ExtractionSiteRow {
  /// Row of tables/extraction_sites.csv this site was made from: its key and
  /// area. Fixed at genesis.
  std::uint32_t table_row = 0;

  /// What is dug here: clay, stone or sand (a resources.csv row). Fixed at
  /// genesis.
  ResourceId resource;

  /// The loading point: the plot's ground nearest a road, metres from the
  /// map's south-west corner (boss, parcel 272). The walk to the digging and
  /// the haul of the load are both measured to it, as a stand's are.
  Vec2 position;

  /// What may still be dug, grams: area × the resource's stock density at
  /// genesis, and it only falls. NOTHING GROWS BACK (parcel 270): at zero
  /// the site is exhausted for good and a marking order is refused.
  Grams stock_grams = 0;

  /// Of the stock, what the chairman has marked for digging and the crew has
  /// not yet dug, grams. 0 <= marked_grams <= stock_grams. Set by the marking
  /// order (OrderKind::kMarkExtraction), cleared when the digging ends.
  Grams marked_grams = 0;

  /// The digging seam, game man-days: the marked tonnes × the resource's
  /// labour per tonne when the order is read, drained by the crew
  /// (WorkKind::kExtraction). At zero with a mark still standing, production
  /// lays the marked mass on the site as a load and takes it off the stock.
  float work_days_remaining = 0.0F;

  /// Dug and lying on the site, unfetched, grams.
  Grams load_grams = 0;

  /// The carting seam, game man-days — the same contract as
  /// FieldRow::haul_days_remaining and TimberStandRow's: production sizes it
  /// from the load and the room its home can take (resource_stores.csv),
  /// labor drains it (WorkKind::kHauling). Zero whenever `load_grams` is zero.
  float haul_days_remaining = 0.0F;

  /// What the settlement last wrote into the carting seam, so it can tell
  /// what people carried from what the room did. Same contract as
  /// FieldRow::haul_days_written.
  float haul_days_written = 0.0F;

  /// 1 once the stock has run out and the village was told
  /// (EventKind::kExtractionSiteExhausted); the announcement is made once.
  std::uint8_t exhausted = 0;
};

/// @brief The extraction site table used by WorldState.
using ExtractionSiteTable = StateTable<ExtractionSiteId, ExtractionSiteRow>;

}  // namespace core

#endif  // CORE_COMMON_EXTRACTION_STATE_H_
