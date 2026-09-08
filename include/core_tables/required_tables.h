/// @file
/// @brief One door for "this module cannot be built without these tables".
/// @threading SINGLE_THREADED
/// Called from factory functions on the setup thread, before the simulation
/// exists. Reads the set, writes nothing, logs the first fault.
///
/// WHY THE LIST IS NAMED AND NOT DERIVED FROM THE DIRECTORY. A load reads
/// every tables/*.csv it finds, so the set it returns describes the folder
/// and not the need: THE DIRECTORY DOES NOT KNOW WHAT IS MISSING FROM IT.
/// Until 2026-09-08 the answer to "is the balance complete?" was therefore
/// "yes, whatever is there" — and a run on a set with a file removed
/// converged, printed plausible numbers, and said nothing.
///
/// WHY IT IS ONE FUNCTION AND NOT FOUR LOOPS. Four modules had written the
/// same loop and the same message by hand, and each new module copied the
/// nearest one; a rule with four homes drifts in silence, and this one had
/// already drifted — EVERY ONE of those lists named a subset of what its
/// module actually read, so the check passed while the module fell back to
/// its defaults for the rest.
///
/// THE RULE THE LISTS FOLLOW: under StubTables::kRefused, EVERY table a
/// module reads is required. Not a chosen subset — the whole read set, so
/// that "required" needs no judgement and a new reader cannot quietly land
/// outside the list. A table whose absence is a legitimate state says so in
/// a written reason at its own call site and stays out of the list.

#ifndef CORE_TABLES_REQUIRED_TABLES_H_
#define CORE_TABLES_REQUIRED_TABLES_H_

#include <initializer_list>
#include <string>
#include <string_view>

#include "core_tables/stub_tables.h"

namespace core {

class ITableSet;

/// @brief Refuses, by name, when a table the module reads is absent.
/// @param module Module name for the message, e.g. "production".
/// @param names Every table this module reads. Order fixes which absence is
///        reported first, so keep it as the reader reads them.
/// @param error Where the message goes: written here when non-null, LOGGED
///        when null. Exactly one of the two, because the callers that carry
///        an error string log it themselves and a fault reported twice reads
///        as two faults. The wording stays in one place either way.
/// @return true when every name is present, and always true under
///         StubTables::kAllowed — the caller has said it wants the
///         documented defaults (stub_tables.h).
bool RequireTables(const ITableSet& tables,
                   StubTables stubs,
                   std::string_view module,
                   std::initializer_list<std::string_view> names,
                   std::string* error);

}  // namespace core

#endif  // CORE_TABLES_REQUIRED_TABLES_H_
