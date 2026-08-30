// Placeholder translation unit: the module has a contract (save.h) and no
// implementation yet (stage 7, task O1). Removed by the first real .cpp.
//
// The include is the point: it makes the build compile the contract. The
// one exported symbol keeps MSVC from warning about an empty archive
// member (LNK4221), so it must have external linkage.

#include "core_save/save.h"  // IWYU pragma: keep

namespace core::internal {

int CoreSavePlaceholder() {  // NOLINT(misc-use-internal-linkage)
  return kSaveHeaderSize;
}

}  // namespace core::internal
