/// @file
/// @brief ITableSet / ITable — the balance tables and how the core reads them.
/// @threading PARALLEL_READONLY
/// A table set is loaded before the simulation is created and never changes
/// afterwards — any phase and any worker thread may read it freely. A balance
/// edit is a reload: load a new set, rebuild the simulation (seconds); there
/// is no in-place mutation to synchronize.
///
/// Format decision (manual/61-balance-tables.md): CSV, one dialect across the
/// whole pipeline — the balancer edits Google Sheets and exports CSV into
/// tables/, the core reads those files, the UE side imports the same files
/// into DataTables. What is tunable lives in the files; what is structural
/// (calendar shape, phase order) stays constexpr in code.
///
/// Contract shape: core_tables knows cells, not domains. Each subsystem
/// parses its own definition structs out of this generic access at factory
/// time — parsed tables are configuration, which subsystems are allowed to
/// hold (subsystem law, manual/52-state-model.md). Row order in the file is
/// the definition order: DefId.value == row index (core_common/ids.h), the
/// `key` column carries the stable string name, and saves store keys so a
/// reordered table remaps on load. A definition table is therefore capped at
/// 65535 rows — the loader refuses more.

#ifndef CORE_TABLES_TABLES_H_
#define CORE_TABLES_TABLES_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace core {

/// @brief Return value of the lookups below when nothing matches.
inline constexpr std::uint32_t kNoTableRow = 0xFFFFFFFFu;

inline constexpr std::uint32_t kNoTableColumn = 0xFFFFFFFFu;

/// @brief One loaded table: header row plus data rows, cells addressed by
/// (row, column). Comment lines (first cell starting with '#') are already
/// dropped by the loader and never appear among the rows; data row 0 is the
/// first row after the header, so its index is exactly the DefId value.
class ITable {
 public:
  virtual ~ITable() = default;

  virtual std::uint32_t RowCount() const = 0;

  virtual std::uint32_t ColumnCount() const = 0;

  /// @brief Column index by header name, kNoTableColumn if absent.
  /// Consumers must tolerate extra columns they never asked for: unknown
  /// columns are the balancer's notes and are deliberately legal.
  virtual std::uint32_t FindColumn(std::string_view name) const = 0;

  /// @brief Row index by the value of the `key` column; kNoTableRow if the
  /// key is absent or the table has no `key` column. Keys are unique within
  /// a table — the loader refuses duplicates.
  virtual std::uint32_t FindRowByKey(std::string_view key) const = 0;

  /// @brief Raw cell text, exactly as in the file; empty view for an empty
  /// cell or out-of-range indices. The view lives as long as the table set.
  virtual std::string_view CellText(std::uint32_t row, std::uint32_t column) const = 0;

  /// @brief Cell as a whole number. nullopt for an empty cell, text that is
  /// not a plain decimal integer, or out-of-range indices. The consumer
  /// decides whether nullopt means "use the default" or "config error".
  virtual std::optional<std::int64_t> CellInteger(std::uint32_t row,
                                                  std::uint32_t column) const = 0;

  /// @brief Cell as a real number ('.' is the decimal separator). Same
  /// nullopt policy as CellInteger, plus one refusal of its own: a
  /// NON-FINITE cell — "inf", "-inf", "nan", which std::from_chars accepts
  /// as a complete match — is nullopt as well. Every consumer of a real
  /// cell eventually casts it to an integer, where a non-finite value is
  /// undefined behaviour, and these tables are exported and then
  /// hand-edited; so the value never leaves this door. A consumer still
  /// owns its RANGE: finite is not the same as sensible.
  virtual std::optional<float> CellReal(std::uint32_t row, std::uint32_t column) const = 0;
};

/// @brief All tables of one load: every tables/*.csv, named by file name
/// without the extension.
class ITableSet {
 public:
  virtual ~ITableSet() = default;

  /// @brief The table by name, nullptr if no such file was loaded. The
  /// pointer lives as long as the set.
  virtual const ITable* FindTable(std::string_view name) const = 0;

  /// @brief Number of loaded tables — for diagnostics and validation sweeps.
  virtual std::uint32_t TableCount() const = 0;

  /// @brief Name of the table at `index` (load order is file-name order, so
  /// it is stable). Empty view for out-of-range indices.
  virtual std::string_view TableName(std::uint32_t index) const = 0;
};

/// @brief Loads every *.csv in `directory` into an immutable table set.
/// Call from the setup/sim thread only, never concurrently: the file-level
/// PARALLEL_READONLY covers the returned set, not the act of loading (the
/// failure path also logs, and core_log is single-threaded by contract).
/// Fails as a whole: one malformed file, duplicate key or oversized table
/// and the load returns nullptr — a partially loaded balance is worse than
/// none. Details go to the log; the first error is also written to `error`
/// when it is non-null, so headless runs can print it without a log in hand.
/// Implemented in core_tables (stage 1, task O3).
/// @param directory UTF-8 path of the tables folder, usually "tables".
std::unique_ptr<ITableSet> LoadTableSet(std::string_view directory, std::string* error);

}  // namespace core

#endif  // CORE_TABLES_TABLES_H_
