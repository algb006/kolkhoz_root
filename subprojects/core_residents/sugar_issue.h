/// @file
/// @brief «Сахар — малая радость» (#1241; metrics.md; the human's word of
///        1 October 2026: «нет сахара небольшой дебафф. Жители и особенно
///        дети начнут жаловаться»): the month's sugar to the families by
///        eaters, the mood it gives or takes, and the children's complaint.
/// @threading SINGLE_THREADED
/// The residents' decisions sub-step, on each month's first day, BEFORE the
/// night trade's month: «Сперва людям, потом ночь самогонщика» - what stays
/// in the stores is the still's raw material, as the night trade reads it.
///
/// THE RULE (metrics.md, the numbers STUB; econ's linear term,
/// core-boss-sugar-issue [3]):
///   * a family's need is its residents × sugar_kg_per_resident_year ÷ 12;
///   * its pantry's honey covers first («Мёд считается за сахар»);
///   * the rest comes off the stores' unreserved sugar, eaten - not put in
///     the pantry, as sugar has no meal row; short stores share by need;
///   * each resident's mood = mood_base + the term: full × (2 × share − 1),
///     clamped to [−1, +1] × the term - +2 / +4 for a child under 16 at a
///     full ration, 0 at half, −2 / −4 at none;
///   * a month under sugar_dry_share is dry; sugar_complaint_dry_months dry
///     months in a row and the family's child complains (kChildrenAskSweets),
///     not before game month sugar_complaint_from_month, once a season a
///     village.
#ifndef CORE_RESIDENTS_SUGAR_ISSUE_H_
#define CORE_RESIDENTS_SUGAR_ISSUE_H_

#include <span>
#include <string>
#include <string_view>

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

struct SugarIssueConfig {
  float kg_per_resident_year = 1.0F;      ///< `sugar_kg_per_resident_year`
  float mood_with = 2.0F;                 ///< `sugar_mood_with`
  float mood_with_child = 4.0F;           ///< `sugar_mood_with_child`
  float mood_without = -2.0F;             ///< `sugar_mood_without`
  float mood_without_child = -4.0F;       ///< `sugar_mood_without_child`
  float dry_share = 0.5F;                 ///< `sugar_dry_share`
  std::uint8_t complaint_dry_months = 2;  ///< `sugar_complaint_dry_months`
  std::uint8_t complaint_from_month = 3;  ///< `sugar_complaint_from_month`, game months from 1
  /// The child's age line, biological years (alcohol_adult_from_years, the
  /// drinking's own adult: «у детей до 16 лет»).
  float child_under_years = 16.0F;
  ResourceId sugar;  ///< resources.csv `sugar`; invalid: no issue at all
  ResourceId honey;  ///< resources.csv `honey`; invalid: no honey counted
};

/// @brief The world_params keys this file reads, for the assembly's union.
std::span<const std::string_view> SugarIssueWorldParamKeys();

/// @brief Reads the rows; a missing one keeps its documented number.
bool ParseSugarIssueConfig(const ITableSet& tables, SugarIssueConfig& config, std::string& error);

/// @brief The month's issue, the mood and the complaint, on a month's first
///        day (day % kDaysPerMonth == 0); any other day does nothing. With no
///        sugar row every mood is reset to mood_base, as the month's mood is
///        composed here and the moonshine's term is added after it (#1253).
/// Side effects: the stores' sugar and the pantries' honey taken, every
/// resident's mood written, FamilyRow::sugar_share and sugar_dry_months,
/// ChairmanState::sugar_complaint_season, kChildrenAskSweets.
void IssueSugarMonth(const SugarIssueConfig& config, float life_speedup, WorldState& current);

/// @brief A resident's sugar term for the month at the family's `share`.
float SugarMoodTerm(const SugarIssueConfig& config, float share, bool child);

}  // namespace core

#endif  // CORE_RESIDENTS_SUGAR_ISSUE_H_
