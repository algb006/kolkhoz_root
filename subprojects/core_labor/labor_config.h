// Internal to core_labor: the parsed labor configuration and its parser.
//
// Values mirror tables/labor.csv plus what labor needs from tables/life.csv,
// tables/livestock.csv and tables/crops.csv; the defaults here equal the
// canonical table contents so that a table set without them (unit tests,
// early runs) behaves like the shipped one. A present-but-malformed table is
// an error, never a silent fallback (parsing in labor_config.cpp).
//
// Norm convention: table columns keep REAL man-days (the human-readable
// numbers of the balance docs); parsing divides by kRealDaysPerGameDay
// (calendar.h) once, so every norm in this struct is already in GAME
// man-days. The FIELD norms are not here: production owns them, because
// production is what opens a working phase and sizes its
// work_days_remaining (manual/65-labor-model.md §2).

#ifndef CORE_LABOR_LABOR_CONFIG_H_
#define CORE_LABOR_LABOR_CONFIG_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/resident_state.h"

namespace core {

class ITableSet;  // Defined in core_tables.

/// Per-work-kind pay and hardness (tables/labor.csv, one row per kind).
struct WorkKindRates {
  /// Trudodni per delivered norm man-day (labor-payment design §2:
  /// 0.5 / 1.0 / 1.5 / 2.0 by grade). Epoch-I manual work is grade 1.0.
  float trudodni_rate = 1.0F;

  /// Rest drained per delivered norm man-day, in metric points. Decision
  /// 107 fixes the daily deltas — ordinary work -2, heavy -4, light -1 —
  /// and a norm man-day IS the reference worker's day, so the canonical
  /// per-day number is charged per norm-day: a long summer day costs
  /// proportionally more, which is the same thing said in hours.
  float rest_drain_per_norm_day = 2.0F;
};

/// What labor needs to know about a crop: only when its calendar window
/// closes, which is what makes one job more urgent than another
/// (crops.csv sow_to_month / harvest_to_month, 1-based in the file, 0-based
/// here like the Month enum). The norms themselves belong to production.
struct CropWindows {
  std::uint8_t sow_to_month = 11;

  std::uint8_t harvest_to_month = 11;
};

/// Shape of the worker-efficiency product (manual/65-labor-model.md §5).
///
/// The scale is anchored, not guessed at: a norm man-day is what the
/// REFERENCE worker does in a standard day — health 70, rest 70, mood 60,
/// average stamina, primary schooling, on the age plateau. Health, mood and
/// skill therefore enter as DEVIATIONS from his values (pivot and slope per
/// 100 metric points), so their product is exactly 1.0 for him by
/// construction and the balance runs' man-days mean what they say. The
/// illiterate majority of Epoch I comes out 15% under him, which is the
/// canon of education design §6. Slopes are ASSUMPTION until playtests.
struct EfficiencyFactors {
  float health_pivot = 70.0F;

  float health_slope = 0.5F;

  float mood_pivot = 60.0F;

  float mood_slope = 0.3F;

  float skill_pivot = 17.5F;  ///< The blend of an unskilled starter.

  float skill_slope = 0.3F;

  // -- rest: the canonical steps of decision 107 ---------------------------
  /// Above the first threshold a man works at full output; below it he
  /// loses 10%, below the second 25% (and hurts himself, which the injury
  /// system will use when it exists).
  float rest_step_tired = 40.0F;

  float rest_step_spent = 20.0F;

  float rest_factor_tired = 0.9F;

  float rest_factor_spent = 0.75F;

  /// Biological age at which the plateau ends and output starts to fall.
  /// Decision 105: the aging threshold is life expectancy minus 20, and the
  /// starting expectancy is 60 — so 40 while the expectancy formula itself
  /// is a STUB (it needs the food system, stage 6).
  /// Years of the margin between life expectancy and the age at which work
  /// starts to decline (decision 105: the threshold is LE - 20). The MARGIN
  /// is labor's knowledge; the life expectancy is the world's, and labor
  /// reads it from WorldState::vitals rather than keeping a copy — one fact,
  /// one home.
  float aging_margin_years = 20.0F;

  /// Output lost per biological year past the threshold, and the floor it
  /// never falls through (an old man still mows, just slowly).
  float age_decline_per_year = 0.02F;

  float age_decline_floor = 0.5F;
};

/// How the "skill" of Epoch-I field work is blended (education §11 plus
/// education §6: on simple work strength beats diplomas). The three weights
/// sum to 1; ASSUMPTION.
struct SkillBlend {
  float earned_weight = 0.5F;

  float stamina_weight = 0.35F;

  float schooled_weight = 0.15F;
};

/// The roster key of the groom. Shared knowledge with core_production,
/// which resolves the same post for the herd day (stable_horses.h) — spelt
/// out in both places rather than passed between them, because a profession
/// key is DATA and two modules reading one table are not a dependency
/// (manual/74-posts.md §5).
inline constexpr std::string_view kGroomPostKey = "groom";

/// Who may hold a post at all (professions.csv `gender`). It is the era's
/// own rule and not the core's: the milkmaid and the poultry maid are
/// women's posts in the design's Epoch I, and the day that changes it is a
/// cell that changes, not a branch here.
enum class PostSexRule : std::uint8_t {
  kAny = 0,
  kFemale,
  kMale,
};

/// One post a resident can be appointed to (tables/professions.csv; posts
/// design, manual/74-posts.md §6). Every threshold is a COLUMN and never a
/// rule in code: a post that grows its own age band tomorrow grows it in the
/// table, and nothing here is touched.
struct ProfessionDef {
  EducationStage min_education = EducationStage::kNone;

  /// Biological years. 0 means the ordinary working age — the column is
  /// empty for most posts, and the groom is one of them.
  float min_age_years = 0.0F;

  /// Biological years. 0 means no upper limit.
  float max_age_years = 0.0F;

  PostSexRule sex_rule = PostSexRule::kAny;

  /// The village has room for exactly one holder, wherever he stands. A
  /// second appointment is refused with kNoVacancy and not kRuleForbids:
  /// "the place is taken" and "no such place here" are different things,
  /// and the player fixes them differently.
  std::uint8_t single_post = 0;
};

/// One row of tables/unit_staff.csv: which unit type carries which post, on
/// which step of its ladder, and how many of them.
struct StaffSlot {
  UnitTypeId unit_type;

  ProfessionId profession;

  /// The ladder step that carries the post; 0 means every step. Levels are
  /// 1-based in the world (level 0 is a marked site, which carries nothing).
  std::uint8_t level = 0;

  /// How many may hold it at one unit; 0 means no ceiling.
  std::uint16_t slots = 0;
};

/// The labor configuration: everything the subsystem knows outside state.
struct LaborConfig {
  // -- norm sources (game man-days after parsing; see header comment) -------
  /// Yearly care norm per adult head, by livestock kind row
  /// (livestock.csv care_days_per_year, real man-days in the table; the cow
  /// anchor is 32 real man-days a year). GAME man-days here.
  std::vector<float> care_days_per_year;

  /// Calendar windows per crop row, for job urgency.
  std::vector<CropWindows> crops;

  /// livestock.csv "horse" row: the kind whose adults are the draught pool
  /// and whose private standing locks its host to horse work (start canon).
  LivestockKindId horse_kind;

  // -- rates by work kind (labor.csv) --------------------------------------
  /// Indexed by WorkKind. Epoch-I field work is all grade 1.0
  /// (labor-payment §2); plowing and hand reaping are the heavy kinds of
  /// decision 107, the rest ordinary. kNone pays and costs nothing —
  /// idling is not work.
  std::array<WorkKindRates, kWorkKindCount> rates = {{
      {.trudodni_rate = 0.0F, .rest_drain_per_norm_day = 0.0F},
      {.trudodni_rate = 1.0F, .rest_drain_per_norm_day = 4.0F},
      {.trudodni_rate = 1.0F, .rest_drain_per_norm_day = 2.0F},
      {.trudodni_rate = 1.0F, .rest_drain_per_norm_day = 2.0F},
      {.trudodni_rate = 1.0F, .rest_drain_per_norm_day = 4.0F},
      {.trudodni_rate = 1.0F, .rest_drain_per_norm_day = 2.0F},
  }};

  // -- the day (labor.csv; time design §6-§7) ------------------------------
  /// Hours of work behind one norm man-day.
  float standard_day_hours = 10.0F;

  /// One-way commute limit in game hours: CANON since decision 109 — four
  /// hours, one rule for a unit's staff and for an open field alike (time
  /// design §7). The threshold and the day's output measure the same
  /// shoulder, each order by what it travels on (decision 103), and both
  /// use today's road: the mowers do not walk to the meadow in January.
  float travel_limit_hours = 4.0F;

  /// Below this much daylight left after the road, a job is not worth
  /// walking to at all. ASSUMPTION.
  float min_usable_hours = 1.0F;

  /// Straight-line to path-distance factor. Roads are deferred in phase 1
  /// and the canonical numbers are straight-line ones — the design's own
  /// "~2 game hours is ~800 m on foot" only holds at 1.0, and the v9 start
  /// map costs the road the same way — so phase 1 keeps 1.0 and the knob
  /// waits for the roads system (polish P43ac2).
  float path_factor = 1.0F;

  /// Real walking speed, km/h (transport.csv pedestrian row); the game
  /// speed is this / kClockScale.
  float walk_speed_kmh = 5.0F;

  /// Real speed of a harnessed order, km/h (transport.csv horse_trot row):
  /// a plowman rides out with his horse instead of walking (decision 103).
  float harness_speed_kmh = 12.0F;

  /// Sleep hours per day, for the family's household_hours arithmetic.
  float sleep_hours = 8.0F;

  // -- rest and the walk-off (labor.csv; unit rules §8, metrics §11) -------
  /// At or below this rest a worker stops and goes home — his own decision
  /// (decision 107 puts the critical limit at 10).
  float rest_walkoff_threshold = 10.0F;

  /// Rest recovered by a full day off, metric points (decision 107: +8; the
  /// +15 with leisure and +20 on a holiday wait for clubs and events).
  float rest_recovery_day_off = 8.0F;

  /// Rest recovered by a working day spent at home without an order.
  float rest_recovery_idle_day = 4.0F;

  /// Health lost per day while rest is under the spent threshold (decision
  /// 107: -1 a week in that zone).
  float health_loss_per_spent_day = 1.0F / 7.0F;

  /// How much stamina+sportiness soften the drain: at 100 combined the
  /// drain is multiplied by (1 - this). ASSUMPTION.
  float stamina_drain_relief = 0.4F;

  // -- efficiency factors (labor.csv; education design §6, §11) ------------
  /// Multipliers by education stage for SIMPLE work: illiterate 0.85,
  /// primary 1.0, the rest 1.05 (diplomas mean little in the field).
  std::array<float, 5> education_factor = {0.85F, 1.0F, 1.05F, 1.05F, 1.05F};

  /// Self-education adds up to this at metric 100 (education §6: x1.10).
  float self_education_max_bonus = 0.10F;

  EfficiencyFactors efficiency;

  SkillBlend skill;

  // -- the working life (life.csv, shared with core_residents) -------------
  /// Biological years per game year: ages are biological, the calendar is
  /// not (demography design §2).
  float life_speedup = 4.0F;

  /// From this biological age a resident is a worker (life-cycle §1).
  /// Child labor (life-cycle §7) is deferred: nobody younger is placed.
  float adult_age_years = 16.0F;

  // -- posts (professions.csv, unit_staff.csv; task A7) ---------------------
  /// Indexed by ProfessionId, in table order. Empty when the table is
  /// absent — and then no post exists that could be filled, so every
  /// kAppoint is refused by rule.
  std::vector<ProfessionDef> professions;

  /// The staff table flat, in file order: a handful of dozens of rows, walked
  /// linearly. An index by unit type would be a second structure to keep in
  /// step with the first for no measurable gain at this size.
  std::vector<StaffSlot> staff;

  /// professions.csv "groom" — the one post the core raises an alarm about,
  /// because it is the one that unlocks the start (livestock design §5: the
  /// yard without a groom leaves the team at private yards and a third of
  /// the village tied to it). Resolved from kGroomPostKey; invalid when the
  /// roster has no such row, and then the alarm never fires.
  ProfessionId groom_post;

  // -- placement (labor.csv; society design §1) ----------------------------
  /// Accountant placement quality 0-3. Campaign default 0: the start has no
  /// accountant, the chairman places naively. Rises when the post is filled
  /// (a later stage wires that; the knob exists now).
  std::uint8_t placement_level = 0;
};

/// @brief Fills `config` from the table set.
/// @return false with `error` set when a PRESENT table is malformed; a
///         missing table leaves the canonical defaults in place.
bool ParseLaborConfig(const ITableSet& tables, LaborConfig& config, std::string& error);

}  // namespace core

#endif  // CORE_LABOR_LABOR_CONFIG_H_
