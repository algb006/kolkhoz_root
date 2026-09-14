/// @file
/// @brief MaterialShortfall — one line of a building's recipe the village does
/// not hold in full.
/// @threading PARALLEL_READONLY
/// Plain data, answered between steps off the completed state; nobody writes
/// it into the world.
///
/// Why it exists: construction design §6, "старт проверяет материалы — и
/// называет, чего не хватает" (the human's word of 2026-09-14). A start refused
/// with OrderRefusal::kMaterialsShort has to say WHAT is short, and the order
/// row is a fixed-width record while a recipe is a list, so the list is a door
/// of its own (IConstructionSystem::MaterialsShortFor, and the session's).

#ifndef CORE_COMMON_MATERIAL_SHORTFALL_H_
#define CORE_COMMON_MATERIAL_SHORTFALL_H_

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One recipe line that is short: the resource, what the works need of
/// it, and what the village holds of it — the stores, the heaps and the site.
/// held < needed always; the layer shows needed - held as the missing amount.
struct MaterialShortfall {
  ResourceId resource;

  Grams needed = 0;

  Grams held = 0;
};

}  // namespace core

#endif  // CORE_COMMON_MATERIAL_SHORTFALL_H_
