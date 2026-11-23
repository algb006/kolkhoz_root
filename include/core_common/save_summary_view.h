/// @file
/// @brief «Сводка для сохранения» (shell.md §4а, «Сведения в шапке слота»; host 108 §3.1а,
///        SlotHeader field 11): the entries the save slot's header shows, as kinds with integer
///        params - host serialises them and rpg's string keys word them, one key a kind.
///
/// Built off the session's own doors at the moment of the write (ISession::SaveSummary): the
/// era's readiness, the state's counts, the stock forecast and the lit lamps. The core keeps no
/// wording and no scale: the village mood's rung is host's (signal_bands.csv, one home), the
/// core gives the mean it is read off.
/// @threading SINGLE_THREADED
/// A value type, built between steps on the sim thread.

#ifndef CORE_COMMON_SAVE_SUMMARY_VIEW_H_
#define CORE_COMMON_SAVE_SUMMARY_VIEW_H_

#include <array>
#include <cstdint>
#include <vector>

namespace core {

/// @brief One entry's kind. Appended BEFORE the count; a kind is never renumbered (the slot
///        header stores it).
enum class SaveSummaryKind : std::uint8_t {
  /// The era and its path: params {epoch, conditions met, conditions in all}. The conditions
  /// are the transition's (EraReadinessView): its six blocks, the population, and both indices
  /// held for the years required - «Эпоха I · 5 из 8».
  kEraPath = 0,
  /// params {residents, households}.
  kPeople,
  /// The village's mood: params {mean family satisfaction x 10 (0-1000), families averaged}.
  /// host maps the mean to its one-word rung (signal_bands.csv, metric satisfaction).
  kVillageMood,
  /// Bread by term: params {month the food lasts to (0-based), years ahead of today's (0 =
  /// this year), days it lasts}; month -1 when nothing eats it or the forecast has no data
  /// (days then -1 or 0 as the forecast says).
  kFoodTerm,
  /// Feed by term: the same params as kFoodTerm, for the herds' feed.
  kFeedTerm,
  /// The main concern: params {AlarmKind of the FIRST red lamp (Alarm::lamp 1) in
  /// ActiveAlarms' own order, lit red lamps in all}. Present only when a red lamp is lit.
  kConcern,
  /// When no red lamp is lit: params {FieldPhase most fields stand in among ploughing,
  /// harrowing, sowing and reaping, fields in it}; FieldPhase kIdle with 0 when none works.
  kSeasonWork,
  /// ALWAYS present (#2089, game's load list's season icon; host 108 §3.1a puts the calendar's
  /// conversion in the core): params {calendar year (1931...), month 0-11, core::Season (0
  /// winter ... 3 autumn), day in the month (0-based)} of the saved day.
  kCalendar,

  /// NOT A KIND: the count, for a consumer's mirror.
  kSaveSummaryKindCount,
};

/// @brief One entry: a kind and up to four integer params (unused ones 0).
struct SaveSummaryEntry {
  SaveSummaryKind kind = SaveSummaryKind::kEraPath;
  std::array<std::int32_t, 4> params{};
};

}  // namespace core

#endif  // CORE_COMMON_SAVE_SUMMARY_VIEW_H_
