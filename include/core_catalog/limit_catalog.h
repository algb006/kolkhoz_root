/// @file
/// @brief The district's limit catalogue: the lots of tables/limit_catalog.csv,
/// what each brings (tables/limit_lot_goods.csv), and the year's points
/// knobs out of world_params.csv (district design §1, §4; boss, parcels 208,
/// 211).
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.
///
/// WHY IN THE CATALOGUE. Production consumes the order and grants the year's
/// points, and the runs' chairman reads the same prices to decide what to
/// buy: one reading, not two. The RULES — what may be ordered, the year's
/// grant — are production's (core_production/district_limit.h); the
/// catalogue holds definitions and no behaviour.

#ifndef CORE_CATALOG_LIMIT_CATALOG_H_
#define CORE_CATALOG_LIMIT_CATALOG_H_

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/ids.h"
#include "core_common/limit_state.h"
#include "core_common/quantities.h"
#include "core_tables/tables.h"

namespace core {

/// @brief What a lot is (limit_catalog.csv `kind`). Only goods are bought in
/// the core today; the others have their own windows (district design §1).
enum class LimitLotKind : std::uint8_t {
  kGoods = 0,
  kLivestock,
  kVehicle,
  kPerson,
  kChoice,

  /// A service the district sends rather than goods it delivers — the MTS
  /// column of spring and autumn (boss, parcels 235, 449; mts.md §1). Only
  /// the two column lots are bought; another service is refused.
  kService,
};

/// @brief The farm's status tier (core loop §3), which sets the base grant.
/// STUB: the core has no economic readiness index yet, so every farm is
/// kLagging (boss, parcel 211).
enum class FarmStatusTier : std::uint8_t {
  kLagging = 0,
  kAverage,
  kStrong,
  kLeading,
};

/// @brief One row of limit_catalog.csv with its goods, and — for a livestock
/// lot — what head it brings (tables/limit_lot_livestock.csv).
struct LimitLotDef {
  /// Price in points; negative when the row names none (the lot is not
  /// ordered until it has one).
  std::int32_t points = -1;

  /// First epoch the lot is open in, 1..3.
  std::uint8_t era = 1;

  LimitLotKind kind = LimitLotKind::kGoods;

  /// Grams of each resource one lot brings, by ResourceId. A resource whose
  /// amount the table leaves empty brings nothing yet (boss, parcel 211);
  /// a lot all of whose amounts are empty is not ordered.
  ResourceAmounts goods;

  // -- a livestock lot (tables/limit_lot_livestock.csv) ----------------------
  // THE DISTRICT SELLS STOCK, AND IT ALWAYS DID. Until 2026-09-16 the core
  // refused every kLivestock lot with a comment that called it a STUB, I
  // reported that refusal as a rule of the world, and the design grew a
  // rejection around it. It was never a rule: `horse_head` is priced at 70
  // points from epoch I, the base grant is 250-350 a year and LARGEST for
  // the farm doing worst, and livestock design calls that pair «страховка от
  // тупика» in so many words — the way out of losing the last draught horse.

  /// Which kind of stock, from the lot's row of limit_lot_livestock.csv.
  /// Invalid for a lot that is not livestock.
  LivestockKindId livestock;

  /// How many head one lot brings. ZERO IS "NOT WRITTEN YET", not "none":
  /// the batches — piglets, chicks — carry an empty cell because their size
  /// is balance work nobody has done, and a livestock lot with no count is
  /// refused exactly as a goods lot with no amount is.
  std::uint16_t head_count = 0;

  /// What age they arrive at: stock grown, poultry young.
  LivestockArrivalStage arrives_stage = LivestockArrivalStage::kAdultStart;

  /// Whether the order names the sex. True for a single head of a kind that
  /// has one; false for a batch, which comes mixed.
  bool sex_choice = false;
};

/// @brief The whole catalogue and the year's points knobs. Defaults are the
/// design's figures, kept for a world with no tables.
struct LimitCatalog {
  /// Every lot, in table row order: LimitLotId indexes it.
  std::vector<LimitLotDef> lots;

  /// Base grant by FarmStatusTier: 350 / 320 / 285 / 250 — inversely to
  /// success (district design §1).
  std::array<std::int32_t, 4> base_points = {350, 320, 285, 250};

  /// For a plan delivered in full, 100 % of every position.
  std::int32_t plan_met_points = 150;

  /// Per percent over the plan, and the most that term gives.
  std::int32_t overfulfil_points_per_percent = 10;
  std::int32_t overfulfil_points_max = 200;

  /// Days the district's cart takes, and the most it may be late by.
  std::uint32_t delivery_days = 2;
  std::uint32_t delivery_delay_days_max = 2;

  // -- the district MTS's column (MTS design §1; boss, parcel 449; STUB) -----

  /// The two lots that buy a column, by key: mts_column_spring and
  /// mts_column_autumn. Invalid when the catalogue carries no such row.
  LimitLotId mts_spring_lot;
  LimitLotId mts_autumn_lot;

  /// Hectares a column works in a season (world_params.csv
  /// mts_column_ha_limit) and in one working day (mts_column_ha_per_work_day).
  float mts_column_ha_limit = 70.0F;
  float mts_column_ha_per_work_day = 10.0F;

  /// The field-work windows, 0-based months inclusive: spring March-May,
  /// autumn August-October (world_params.csv mts_column_spring_from_month ..
  /// mts_column_autumn_to_month, human 1..12 there).
  std::uint8_t mts_spring_from_month = 2;
  std::uint8_t mts_spring_to_month = 4;
  std::uint8_t mts_autumn_from_month = 7;
  std::uint8_t mts_autumn_to_month = 9;

  // -- handing a head back (livestock design, «Лишних лошадей сдают райкому»)

  /// WHAT THE DISTRICT PAYS FOR A HEAD, as a SHARE of what the same lot costs
  /// to buy — never a price of its own. The design's rule is «сдают дешевле,
  /// чем берут — иначе это была бы не сдача излишка, а способ печатать баллы
  /// на обороте», and a share kept under 1.0 makes that arithmetic rather
  /// than a guard somebody has to remember to write.
  ///
  /// The four bands are the ages `livestock.csv` already draws — newborn,
  /// young, adult, and an adult past `life_game_years_min` — so no second
  /// ladder of ages is introduced here. The numbers are ASSIGNED, not
  /// measured (boss, parcel 91: `source = default` in the design db), and
  /// polishing will move them; what polishing may not do is put any of them
  /// at or above one.
  /// (world_params.csv livestock_handover_newborn .. _old.)
  /// Points the district must have granted IN ALL before electrification
  /// comes (world_params.csv electrification_points_min; electricity design
  /// §3, the first of its three blockers).
  ///
  /// IT IS THE RUNNING TOTAL AND NOT THE YEAR'S REMAINDER, which is the whole
  /// reason LimitState carries one: the design's measure is «сумма баллов
  /// лимита, полученных с начала партии, накопительно, не остаток». Measured
  /// on four seeds 2026-09-17, the total stands at 350 after the first year,
  /// 700 after the second and 1200 after the third on every one of them; 900
  /// is crossed by the third year's grant with room either side, and stops
  /// short of the fourth year, where the seeds begin to disagree (boss,
  /// parcel 98).
  std::int32_t electrification_points_min = 900;

  float handover_share_newborn = 0.15F;
  float handover_share_young = 0.35F;
  float handover_share_adult = 0.55F;
  float handover_share_old = 0.25F;
};

/// @brief The world_params.csv keys this catalogue reads, for the assembly's
/// declared-readers check (core_world/world.cpp).
std::span<const std::string_view> LimitWorldParamKeys();

/// @brief Reads the catalogue. Missing tables keep the defaults and an empty
/// lot list; a present one that cannot be understood refuses — an unknown
/// kind, a lot of goods naming a resource or lot the tables do not carry, a
/// non-numeric or non-positive amount.
///
/// AND limit_lot_livestock.csv the same way: a row naming a lot or a
/// livestock kind the tables do not carry refuses, as does an unreadable
/// `arrives_stage` — the words are `adult_start` and `young` and nothing
/// else. An EMPTY `head_count` does not refuse; it is the "not written yet"
/// of the goods amounts, and the lot simply cannot be ordered. A livestock
/// lot with no row here at all is in the same case.
/// @return false with `error` naming the table, row and column.
bool ParseLimitCatalog(const ITableSet& tables, LimitCatalog& catalog, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_LIMIT_CATALOG_H_
