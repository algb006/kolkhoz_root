// Digging clay, stone and sand (extraction_digging.h).

#include "extraction_digging.h"

#include <cstdint>

#include "core_catalog/extraction_catalog.h"
#include "core_common/emit_event.h"
#include "core_common/extraction_state.h"
#include "core_common/state_table_ops.h"

namespace core {

OrderRefusal MarkExtraction(const ProductionConfig& config,
                            WorldState& current,
                            const OrderRow& order) {
  const std::uint32_t row = FindRow(current.extraction_sites, order.extraction_site);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  ExtractionSiteRow& site = current.extraction_sites.rows[row];
  // ONE MARK A SITE, not one in the village as a felling is: the design gives
  // the digging no rule of "one at a time", and a clay pit and a stone quarry
  // are different crews on different ground (boss, parcel 270). What is
  // already dug and lying does not count — that is carting's business.
  if (site.marked_grams > 0) {
    return OrderRefusal::kConflictsWithActive;
  }
  ExtractedMaterial material = ExtractedMaterial::kClay;
  if (!MaterialOf(config.extraction, site.resource, material)) {
    return OrderRefusal::kRuleForbids;  // a site of a resource the build does not dig
  }
  const Grams unmarked = site.stock_grams - site.marked_grams;
  if (order.amount <= 0 || order.amount > unmarked) {
    return OrderRefusal::kRuleForbids;
  }
  site.marked_grams = order.amount;
  const float tonnes = static_cast<float>(static_cast<double>(order.amount) / 1.0e6);
  site.work_days_remaining =
      tonnes * config.extraction.days_per_t[static_cast<std::size_t>(material)];
  return OrderRefusal::kNone;
}

void DigFinishedSites(WorldState& current) {
  for (std::uint32_t row = 0; row < current.extraction_sites.rows.size(); ++row) {
    ExtractionSiteRow& site = current.extraction_sites.rows[row];
    if (site.marked_grams > 0 && site.work_days_remaining <= 0.0F) {
      const Grams dug = site.marked_grams < site.stock_grams ? site.marked_grams : site.stock_grams;
      site.load_grams += dug;
      site.stock_grams -= dug;
      site.marked_grams = 0;
      site.work_days_remaining = 0.0F;
    }
    // NOTHING GROWS BACK (parcel 270): the day the stock reaches nothing the
    // village is told, once, and the marking order refuses the site for good.
    if (site.stock_grams <= 0 && site.exhausted == 0) {
      site.exhausted = 1;
      SimEvent& gone =
          EmitEvent(current, EventKind::kExtractionSiteExhausted, EventSeverity::kNotable);
      gone.resource = site.resource;
      gone.amount = static_cast<std::int64_t>(current.extraction_sites.row_ids[row].value);
    }
  }
}

}  // namespace core
