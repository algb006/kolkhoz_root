/// @file
/// @brief The map's junctions with the world beyond it, read out of
///        world_junctions.csv and matched to roads.csv's border vertices.
/// @threading SINGLE_THREADED
/// A pure read of the table set at assembly (core_world/world.cpp); the
/// answer is fixed for the world's life (layers design §15а, register 300).
#ifndef CORE_CATALOG_WORLD_JUNCTIONS_H_
#define CORE_CATALOG_WORLD_JUNCTIONS_H_

#include <string>
#include <vector>

#include "core_common/world_junction.h"

namespace core {

class ITableSet;

/// @brief Reads every junction: its key, the road it lies on, the border
///        vertex of that road — found in roads.csv by the road's key and the
///        vertex mark `border_<side>` — where it leads and the neighbour's
///        role.
/// @param junctions Written on success, in the table's row order; left as
///        it was on a refusal.
/// @param error Why it failed: a column missing here or in roads, no roads
///        table, an empty or repeated key, a border vertex without readable
///        metres, a word of `border`,
///        `leads_to` or `neighbor_role` the core does not know, a road with no
///        vertex marked for that border, or a neighbour's role on the
///        district's row.
/// @return false on any of those; true with none read when the set has no
///         `world_junctions` table (the assembly's required list refuses that
///         case, under StubTables::kRefused).
bool ReadWorldJunctions(const ITableSet& tables,
                        std::vector<JunctionView>& junctions,
                        std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_WORLD_JUNCTIONS_H_
