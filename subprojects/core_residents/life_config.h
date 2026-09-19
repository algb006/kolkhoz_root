// Internal to core_residents: the parsed demography configuration.
//
// Values mirror tables/life.csv, tables/demography.csv and
// tables/satisfaction.csv; the defaults here equal the canonical table
// contents so that a table set without these tables (unit tests, early
// runs) behaves like the shipped one. A present-but-malformed table is an
// error, never a silent fallback (parsing in residents_system.cpp).
//
// Stage 6 adds the vitals block (life expectancy, decision 105) and the
// birth-conditions block (decision 106); their keys live in life.csv too.
//
/// @threading PARALLEL_READONLY
/// Filled ONCE, by CreateResidentsSystem, before the system object exists;
/// never written again for the life of the campaign. Slot-5 workers hold a
/// pointer to it (FamilyMetricsPhase) and read it from many threads at
/// once, which is safe for exactly that reason and for no other.
///
/// SO: NO PER-STEP FIELD BELONGS HERE. A cache, a counter, anything the
/// step writes turns this object from a constant into shared mutable state
/// read by every worker at once, and the compiler will not say a word. The
/// label was missing entirely until 2026-09-04, and its absence cost the
/// whole module a full race sweep on every delta — the tooling cannot tell
/// "single-threaded" from "nobody said".

#ifndef CORE_RESIDENTS_LIFE_CONFIG_H_
#define CORE_RESIDENTS_LIFE_CONFIG_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "alcoholism.h"
#include "core_catalog/definitions.h"
#include "core_common/body.h"
#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "membership.h"
#include "night_trade.h"
#include "schooling.h"
#include "sport.h"

namespace core {

class ITableSet;  // Defined in core_tables.

/// Life expectancy and its factors (decision 105; manual/66-food-model.md
/// §7). LE = base + medicine + nutrition + living + working conditions,
/// recomputed once a year from 3-year factor means. Phase 1: only
/// nutrition is alive; medicine and living stay 0, working conditions is a
/// constant knob.
struct VitalsConfig {
  float base_years = 60.0F;

  /// Settlement mean satiety that maps to a zero nutrition contribution,
  /// and the slope of the mapping: contribution = clamp((satiety - neutral)
  /// x slope, -4, +4).
  float satiety_neutral = 70.0F;

  float satiety_to_years_slope = 0.1333F;  ///< ASSUMPTION: +-4 over +-30.

  float nutrition_years_min = -4.0F;

  float nutrition_years_max = 4.0F;

  /// Phase-1 constants of the dormant factors, years.
  float medicine_years = 0.0F;  ///< STUB source: medicine (phase 2).

  float living_years = 0.0F;  ///< STUB source: housing comfort (phase 2).

  float working_conditions_years = 0.0F;  ///< ASSUMPTION: constant, range -2..+2.
};

// The LE derivatives live with their consumers: the aging margin (LE - 20,
// decision 105) is labor's knob (labor.csv aging_margin_years replaces
// aging_from_years), the last-birth-median formula stays a fertility STUB.

/// Birth conditions (decision 106): satisfaction scales the birth rate,
/// hunger and the mother's health stop new pregnancies outright.
struct BirthConditionsConfig {
  /// Band upper bounds over family satisfaction and the multiplier of each
  /// band (design: 0.75 / 0.9 / 1.0 / 1.1 / 1.15). Bounds are ASSUMPTION;
  /// the multipliers are canon.
  std::array<float, 4> satisfaction_bounds = {30.0F, 45.0F, 60.0F, 75.0F};

  std::array<float, 5> multipliers = {0.75F, 0.9F, 1.0F, 1.1F, 1.15F};

  /// No new pregnancies while family satiety is below this (canon 40).
  float satiety_stop = 40.0F;

  /// No new pregnancies while the woman's health is below this (canon 40).
  float mother_health_stop = 40.0F;
};

/// Per-epoch demography parameters (demography design §4).
struct EpochDemography {
  float children_per_family = 6.5F;

  float child_mortality_percent = 28.0F;

  float outflow_percent_per_year = 0.0F;
};

/// Per-epoch weights of the satisfaction components (metrics design §7).
struct SatisfactionWeights {
  float satiety = 45.0F;

  float common_cause = 20.0F;

  float needs = 25.0F;

  float rest = 10.0F;
};

/// The biological clock and its thresholds (tables/life.csv). Ages are
/// biological years; life_speedup maps them to game years.
struct LifeConfig {
  /// The figure knobs of world_params.csv (core_common/body.h). Only the
  /// spreads and the cut are read here: a newborn is given a FRACTION, and
  /// the base heights in metres are the seam's business, not demography's.
  BodyKnobs body;

  /// Pupils one teacher takes (world_params.csv teacher_pupils_per_teacher;
  /// education design, "Эпоха I числами"): the norm by which the district
  /// finds a school short of teachers and sends one (specialist_arrival.h).
  float teacher_pupils_per_teacher = 30.0F;

  /// Days the district's cart takes (world_params.csv limit_delivery_days,
  /// shared with the limit's carts): a specialist comes as the goods do.
  float specialist_delivery_days = 2.0F;

  /// world_params.csv `mud_speed_factor` (boss seq 189): a specialist sent in
  /// the mud (WeatherState::mud) takes specialist_delivery_days divided by
  /// it, as the district's lot does. Only the postman is not delayed — and he
  /// is not modelled here.
  float specialist_mud_speed_factor = 0.5F;

  // -- personal cleanliness (health design §3; world_params.csv) ------------

  /// The band a resident's hygiene is drawn from at the founding. A BAND and
  /// not one figure: a village coming out of ruin is not uniform, and a
  /// single number would make the first filth disease arrive for everybody on
  /// the same morning.
  float hygiene_start_min = 50.0F;
  float hygiene_start_max = 70.0F;

  /// What plain time takes off in a day, before anything else.
  float hygiene_fall_per_day = 0.4F;

  /// What dirty work multiplies that by — the farm, the building site, the
  /// field. A factor and not an addition: the design's own line is «грязная
  /// работа», and the dirt of a day is the day's, not a constant beside it.
  float hygiene_fall_dirty_work_factor = 2.0F;

  /// What a hot day adds, on top of the rest. Its companion is the next
  /// field, and the two were separated for a day: this one had a threshold
  /// written into the rule as a literal 25 compared against the day's MEAN
  /// temperature, and in nine villages of `population_curve` it fired not
  /// once in sixty days.
  float hygiene_fall_heat_extra = 0.3F;

  /// WHAT MAKES A DAY HOT, and it is the AFTERNOON — the day's mean plus its
  /// swing — exactly as the weather's own `kHotAfternoon` decides it
  /// (core_time/time_system.cpp). Read from `hot_afternoon_c` in
  /// weather_params.csv, the same row, so there is one home for the number
  /// and not two that agree today.
  ///
  /// The name of the knob is the whole lesson: it says `afternoon`, and a
  /// rule that compares it with the day's mean is asking the adjacent
  /// question convincingly. The table says so itself, one comment above the
  /// row — «три факта, два из которых случайно разделили число».
  ///
  /// ONE VALUE HERE, PER SEASON THERE, and the day that stops being harmless
  /// is worth naming now: core_time copies this same row into every season
  /// of its table, so the two agree today by construction. Give the design
  /// base a per-season heat and this flat read diverges in silence — that is
  /// the day hygiene must ask the season, not the row.
  float hot_afternoon_celsius = 25.0F;

  /// What a STANDING bathhouse gives back in a day — to the whole village,
  /// which is the stub: the design has four risers and this core has the
  /// machinery for none of the other three (no model of a resident visiting a
  /// unit, no water supply, no soap outside the `consumer_goods` bundle).
  float hygiene_rise_bath_per_day = 1.2F;

  /// Below it a resident has lice or scabies, and the seam hears about it.
  /// THE CORE DOES NOT MODEL THE DISEASE ITSELF: `diseases.csv` is declared a
  /// table the core has no business with, while `first_hygiene_disease` is
  /// declared the core's to raise, and the two reconcile here — the core owns
  /// the CAUSE and says when it bites, the illness stays off-screen as the
  /// quest's brief asks (boss, parcel 126).
  float hygiene_disease_threshold = 25.0F;

  /// The school and the reading hut (unit_types.csv `school`,
  /// `culture_house`), and the posts the district fills there
  /// (professions.csv `primary_teacher`, `librarian`). Invalid in a table-less
  /// world, and then the district sends nobody.
  UnitTypeId school_type;

  /// The bathhouse, and the only riser of hygiene this core has.
  UnitTypeId bathhouse_type;
  UnitTypeId reading_hut_type;
  ProfessionId teacher_post;
  ProfessionId librarian_post;

  /// THE CATALOGUE, read at factory time (core_catalog/definitions.h): the
  /// housing class of every unit type, the plot radii and the map side.
  ///
  /// Held whole rather than picked apart into fields of this struct, and
  /// that is the point of it: two of these columns were being read TWICE —
  /// once here and once in the module that owns them — which is the very
  /// thing the catalogue exists to stop. A copy of a catalogue column in a
  /// subsystem's config is a second home wearing a different name.
  Definitions definitions;

  /// The organizations and ideology (membership.h; boss, parcel 334).
  MembershipConfig membership;

  /// The night trades (night_trade.h; boss, parcel 346).
  NightTradeConfig night_trade;

  /// The school's pupils (schooling.h; boss, parcel 354).
  SchoolingConfig schooling;

  /// The drinking (alcoholism.h; boss, parcel 364).
  AlcoholismConfig alcoholism;

  /// The sports field and sportiness (sport.h; register 223).
  SportConfig sport;

  float life_speedup = 4.0F;

  float adult_age_years = 16.0F;

  float marriage_age_years = 18.0F;

  float fertility_from_years = 18.0F;

  float fertility_to_years = 40.0F;

  float mortality_age_mid_years = 45.0F;

  float mortality_age_old_years = 60.0F;

  float mortality_young_percent_per_year = 0.3F;

  float mortality_mid_percent_per_year = 1.5F;

  float mortality_old_percent_per_year = 20.0F;

  float migration_per_year = 8.0F;

  float marriage_chance_percent_per_day = 25.0F;

  float sex_balance_gain = 0.3F;

  std::array<EpochDemography, 3> epochs = {{
      {.children_per_family = 6.5F,
       .child_mortality_percent = 28.0F,
       .outflow_percent_per_year = 0.0F},
      {.children_per_family = 4.0F,
       .child_mortality_percent = 12.0F,
       .outflow_percent_per_year = 0.0F},
      {.children_per_family = 2.2F,
       .child_mortality_percent = 4.0F,
       .outflow_percent_per_year = 1.5F},
  }};

  std::array<SatisfactionWeights, 3> weights = {{
      {.satiety = 45.0F, .common_cause = 20.0F, .needs = 25.0F, .rest = 10.0F},
      {.satiety = 30.0F, .common_cause = 20.0F, .needs = 30.0F, .rest = 20.0F},
      {.satiety = 15.0F, .common_cause = 15.0F, .needs = 40.0F, .rest = 30.0F},
  }};

  VitalsConfig vitals;  ///< Stage 6, decision 105.

  BirthConditionsConfig birth_conditions;  ///< Stage 6, decision 106.
};

/// @brief Parses life.csv, demography.csv, satisfaction.csv and the one row
/// core_residents needs from unit_types.csv into `config`.
/// @param error Receives a human-readable message on failure.
/// @return false when a PRESENT table cannot be understood; a missing table
/// keeps the defaults and succeeds. Call once, at factory time.
bool ParseLifeConfig(const ITableSet& tables, LifeConfig& config, std::string& error);

}  // namespace core

#endif  // CORE_RESIDENTS_LIFE_CONFIG_H_
