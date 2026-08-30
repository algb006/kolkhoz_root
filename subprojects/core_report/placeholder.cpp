// Placeholder translation unit: the module has a contract (ledger_csv.h)
// and no implementation yet (stage 7, task O2). Removed by the first real
// .cpp.
//
// The include is the point: it makes the build compile the contract. The
// one exported symbol keeps MSVC from warning about an empty archive
// member (LNK4221), so it must have external linkage.

#include "core_report/ledger_csv.h"  // IWYU pragma: keep

namespace core::internal {

int CoreReportPlaceholder() {  // NOLINT(misc-use-internal-linkage)
  return static_cast<int>(kWorkKindCount);
}

}  // namespace core::internal
