/// @file
/// @brief The pupils of Epoch I's school — who is enrolled, in which school,
/// and when the primary stage is counted (education design §10, "Эпоха I
/// числами — запись ученика"; boss, parcel 354).
/// @threading SINGLE_THREADED
/// Runs in the residents' decisions sub-step (phase 3) on the sim thread, on
/// the first day of the school year and on the day it ends, and every day for
/// a school that is gone. It writes ResidentRow::school and education_stage
/// and the step's events, so it can only live in a sequential slot.
///
/// WHAT DECIDES, in boss's numbers (assigned, not measured):
///   * when — the first day of September: every child aged 6.5–11 who is not
///     a pupil and has no primary stage yet is enrolled. At ×4 a child ages
///     four years in a game year, so the yearly intake meets every child once;
///     one already past 11 in September missed the school;
///   * where — the nearest standing school (level ≥ 1) within 1500 m of his
///     yard; beyond it he is not enrolled;
///   * how many — 40 pupils at level 1, 60 at 2, 80 at 3; over it the elder
///     go first and the younger wait for next September;
///   * the stage — the first day of June, the end of the school year: when a
///     teacher holds his post at the school that day, every pupil there is
///     given the primary stage and leaves; without one the year is lost, and
///     a pupil under 11 stays enrolled while one of 11 leaves without it;
///   * a school that is gone lets its pupils go the same day, without it.
///
/// STUB, each with its place: grades (the grades door; until it, the pioneers'
/// autumn wave passes nobody); the school closing below +15; the winter and
/// summer holidays; the children's meals; the secondary stage (Epoch II); the
/// resident activity kStudying, which still follows age and not enrollment
/// (boss's debts).

#ifndef CORE_RESIDENTS_SCHOOLING_H_
#define CORE_RESIDENTS_SCHOOLING_H_

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief The school's numbers (world_params.csv). Defaults are boss's
/// figures of parcel 354, kept for a world with no tables.
struct SchoolingConfig {
  float enroll_age_from_years = 6.5F;     ///< `school_enroll_age_from_years`
  float enroll_age_to_years = 11.0F;      ///< `school_enroll_age_to_years`
  float walk_radius_primary_m = 1500.0F;  ///< `school_walk_radius_primary_m`

  /// Pupils a school holds by its level, 1..3 (`school_pupil_capacity_level_1`
  /// ... `_3`); a level past the last takes the last.
  std::array<std::uint32_t, 3> pupil_capacity = {40, 60, 80};

  std::uint8_t year_start_month = 9;  ///< `school_year_start_month`, September
  std::uint8_t year_end_month = 6;    ///< `school_year_end_month`, June: the stage day

  /// The school unit type and the primary teacher's post (unit_types.csv
  /// `school`, professions.csv `primary_teacher`); invalid in a table-less
  /// world, and then nobody is enrolled.
  UnitTypeId school_type;
  ProfessionId teacher_post;
};

/// @brief The world_params.csv keys this file reads.
std::span<const std::string_view> SchoolingWorldParamKeys();

/// @brief Reads the knobs, the school type and the teacher's post.
/// @return false with `error` naming the key for a value out of range — an
///         age band upside down, a month outside 1..12, the same month for the
///         start and the end, a capacity past 10 000.
bool ParseSchoolingConfig(const ITableSet& tables, SchoolingConfig& config, std::string& error);

/// @brief The first day of the school year: the intake by the rules above,
///        each enrolment a kPupilEnrolled.
/// @param life_speedup LifeConfig::life_speedup, for the biological age.
void EnrollPupils(const SchoolingConfig& config, float life_speedup, WorldState& current);

/// @brief The day the school year ends: the primary stage counted where a
///        teacher is at his post, the year lost where none is; each leaving a
///        kPupilLeftSchool.
void CloseSchoolYear(const SchoolingConfig& config, float life_speedup, WorldState& current);

/// @brief Every day: pupils of a school that no longer stands leave it
///        without the stage.
void ReleasePupilsOfGoneSchools(WorldState& current);

/// @brief The daily entry point: the intake on the first day of the start
///        month, the stage on the first day of the end month, the gone schools
///        every day.
/// @pre Called once per day, at the day's turn.
void RunSchoolDay(const SchoolingConfig& config, float life_speedup, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_SCHOOLING_H_
