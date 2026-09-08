// Implementation of core_tables/required_tables.h: the refusal that names
// the missing file.
//
// The message keeps the wording the four hand-written copies had grown, so
// a log line from a run before 2026-09-08 reads the same as one after it.

#include "core_tables/required_tables.h"

#include <algorithm>
#include <string>

#include "core_log/log.h"
#include "core_tables/tables.h"

namespace core {

bool RequireTables(const ITableSet& tables,
                   StubTables stubs,
                   std::string_view module,
                   std::initializer_list<std::string_view> names,
                   std::string* error) {
  if (stubs == StubTables::kAllowed) {
    return true;
  }
  const auto* const missing = std::ranges::find_if(
      names, [&tables](const std::string_view name) { return tables.FindTable(name) == nullptr; });
  if (missing == names.end()) {
    return true;
  }
  std::string message = std::string(module) + ": the table set carries no '" +
                        std::string(*missing) +
                        "' table, and this caller did not allow the "
                        "defaults";
  if (error != nullptr) {
    *error = std::move(message);
  } else {
    LogError(message);
  }
  return false;
}

}  // namespace core
