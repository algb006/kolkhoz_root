// The cart's and the sledge's numbers off transport.csv (cart_specs_catalog.h).

#include "core_catalog/cart_specs_catalog.h"

#include <cstdint>
#include <string>

#include "core_catalog/table_value.h"
#include "core_common/haul.h"

namespace core {
namespace {

/// One row's speed, and its load_tonnes x load_scale in kg when asked.
bool ReadRow(const ITable& transport,
             const char* key,
             float& speed_kmh,
             float* load_kg,
             std::string& error) {
  const std::uint32_t row = transport.FindRowByKey(key);
  if (row == kNoTableRow) {
    return true;  // no such row: the caller's default stands
  }
  if (!CellOrDefault(transport,
                     row,
                     transport.FindColumn("speed_kmh"),
                     Range{.low = 0.5F, .high = 60.0F},
                     speed_kmh,
                     speed_kmh,
                     error)) {
    error = std::string("transport: ") + key + ": " + error;
    return false;
  }
  if (load_kg == nullptr) {
    return true;
  }
  float tonnes = *load_kg / 1000.0F;
  float scale = 1.0F;
  const std::uint32_t scale_col = transport.FindColumn("load_scale");
  if (!CellOrDefault(transport,
                     row,
                     transport.FindColumn("load_tonnes"),
                     Range{.low = 0.01F, .high = 100.0F},
                     tonnes,
                     tonnes,
                     error) ||
      (scale_col != kNoTableColumn &&
       !CellOrDefault(transport,
                      row,
                      scale_col,
                      Range{.low = kCartLoadScaleMin, .high = kCartLoadScaleMax},
                      1.0F,
                      scale,
                      error))) {
    error = std::string("transport: ") + key + ": " + error;
    return false;
  }
  *load_kg = tonnes * scale * 1000.0F;
  return true;
}

}  // namespace

bool ReadSledgeSpecs(const ITableSet& tables, SledgeSpecs& sledge, std::string& error) {
  sledge = SledgeSpecs{};  // no sledge row: a snow day is a cart's (CartGearOf reads the zeros)
  const ITable* const transport = tables.FindTable("transport");
  if (transport == nullptr || transport->FindRowByKey("sledge") == kNoTableRow) {
    return true;
  }
  sledge.kmh = 8.0F;
  sledge.load_kg = 1125.0F;
  return ReadRow(*transport, "sledge", sledge.kmh, &sledge.load_kg, error);
}

}  // namespace core
