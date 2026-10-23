/// @file
/// @brief The cart's and the sledge's numbers off transport.csv (SL;
/// core_common/cart_gear.h): one reader for the three modules that cart, so
/// the sledge's row is read the same way everywhere.
/// @threading SINGLE_THREADED (configuration, before the run)

#ifndef CORE_CATALOG_CART_SPECS_CATALOG_H_
#define CORE_CATALOG_CART_SPECS_CATALOG_H_

#include <string>

#include "core_common/cart_gear.h"
#include "core_tables/tables.h"

namespace core {

/// @brief Reads the `sledge` row (its speed both ways, its load_tonnes x
///        load_scale) into `sledge`.
/// @return false with `error` on a malformed cell. No transport table or no
///         sledge row leaves zeros: a snow day is then a cart's (CartGearOf).
bool ReadSledgeSpecs(const ITableSet& tables, SledgeSpecs& sledge, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_CART_SPECS_CATALOG_H_
