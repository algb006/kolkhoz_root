// Unit test of core_tables: the reading-contract shape.
// The CSV loader arrives with task O3; this test grows with it.

#include <type_traits>

#include "core_tables/tables.h"

static_assert(std::is_abstract_v<core::ITable>, "ITable is a contract");
static_assert(std::is_abstract_v<core::ITableSet>, "ITableSet is a contract");
static_assert(std::has_virtual_destructor_v<core::ITableSet>,
              "table sets are destroyed through the interface");
static_assert(core::kNoTableRow == 0xFFFFFFFFu, "the no-row sentinel is pinned");
static_assert(core::kNoTableColumn == 0xFFFFFFFFu, "the no-column sentinel is pinned");

int main() {
  return 0;
}
