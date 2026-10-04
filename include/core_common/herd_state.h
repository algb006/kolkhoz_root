/// @file
/// @brief HerdRow — the per-herd state: kind, place, headcount by age rung.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::herds under the double-buffer discipline. THREE
/// writers, and every one of them is a sequential sub-step of the decisions
/// slot (phase 3), which is what keeps structure changes away from any
/// parallel phase:
///   * production decisions — the herd day: births, maturation, deaths,
///     transfers, billeting (stage 6; horse offspring is additionally
///     blocked until a stable exists — the capacity rule of the start
///     rework), and the one-time merge of the team into the kolkhoz yard
///     (task A7, core_production/stable_horses.h), which REMOVES rows;
///   * demography, in core_residents — a dying family's flock passes to the
///     heir: `household` is reassigned, head counts are folded in, and the
///     emptied row is REMOVED (residents_system.cpp). A second module that
///     changes the table's shape, named here because a write map that lists
///     one writer is the map a future parallel phase would be planned
///     against;
///   * labor — care_days_remaining, and nothing else.
///
/// Design sources: livestock design §6 and the mobs parcel: the age ladder
/// is 1/2/3 (wild/poultry/cattle), ONLY ADULTS produce — milk, wool, eggs,
/// draught and manure are computed from adult_count; no growth curves or
/// per-age feed norms exist by design (a juvenile eats a fixed half of the
/// adult norm, a newborn at the dam nothing). Disease is a bare STUB degree
/// (0 = healthy, 1 vulnerable, 2 obvious, 3 down): an Era II+ mechanic
/// (design question 99 closed 2026-08-29 — Era I animals never get sick;
/// its cold ladder is freeze -> productivity drop -> death, no disease
/// step). The field stays a STUB for all of phase 1.
///
/// A herd stands either at a unit (the stock-yard's cows) or at a family's
/// yard (the start keeps all 16 kolkhoz horses in private yards until the
/// kolkhoz yard is built AND a groom is appointed — livestock design §5,
/// task A7: the herd day moves them the morning after the appointment) —
/// exactly one of `unit`/`household` is valid.
///
/// Labor seam (stage 5, manual/65-labor-model.md): unit-standing herds are
/// a daily work source — the labor sub-step refills care_days_remaining
/// each morning from the kind's care norm and drains it with assigned barn
/// workers. A household-standing herd generates NO kolkhoz job: its care is
/// the owner's leak (livestock design §5), never assigned labor.

#ifndef CORE_COMMON_HERD_STATE_H_
#define CORE_COMMON_HERD_STATE_H_

#include <array>
#include <cstdint>
#include <optional>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"
#include "core_common/wait_state.h"

namespace core {

/// @brief One herd of one kind in one place. Plain data.
struct HerdRow {
  /// Row of tables/livestock.csv: cow, horse, sheep, pig, chicken, duck.
  LivestockKindId kind;

  /// The unit housing the herd; invalid when it stands at a family yard.
  UnitId unit;

  /// The family yard housing the herd; invalid when it stands at a unit.
  FamilyId household;

  /// 0/1: WHOSE the herd is, which is a different question from where it
  /// stands (livestock design §6, boss answer 2026-08-30: "billeting is
  /// placement, not ownership"). A kolkhoz herd is fed from the stores and
  /// delivers to them wherever it stands — the sixteen start horses live in
  /// private yards and are still the farm's, and so is a cow with no room in
  /// the barn. A household herd is the family's own: it eats out of that
  /// family's pantry and its milk and eggs land there.
  std::uint8_t household_owned = 0;

  std::uint16_t newborn_count = 0;

  std::uint16_t juvenile_count = 0;

  /// The only count that produces anything (mobs canon).
  std::uint16_t adult_count = 0;

  /// Males among adult_count, for the breeding rule (boss summary 2026-08-29
  /// §2.2: offspring needs an adult male present). Always 0 for sexless
  /// kinds (poultry); genesis and transfers keep it <= adult_count.
  std::uint16_t adult_male_count = 0;

  /// THE FED DAYS IN A ROW (save 142, 0.37.192; boss, the logistics thread
  /// [113]-[117]): days the herd was fed in full, counted to the hunger that
  /// ends them; 0 on a hungry day. kNeverHungry for a herd never hungry yet —
  /// its first hungry day is a new episode. A hungry day is a NEW episode of
  /// hunger, said by kHerdWentHungry, only after kHungerEpisodeFedDays fed
  /// days (herd_system.cpp): a herd fed one day and hungry the next is the
  /// same hunger. Until 0.37.192 every hungry day after a fed one was said,
  /// and a herd flapping between the two stopped the fast-forward ten times a
  /// year (0.37.191's pair B9, village 1933 year 5, against econ's gate 8).
  std::uint16_t fed_days_in_a_row = 0xFFFFU;

  // -- stage-6 cohort flow (manual/66-food-model.md §6) --------------------
  // The row stores counts, not per-head ages (mobs canon: no per-animal
  // modeling), so aging and births run as deterministic fractional flows:
  // each day the accumulator gains count / rung-duration (or the birth
  // rate x females), and the integer part moves whole heads. Written only
  // in the production decisions sub-step.

  /// Accumulated fractional heads maturing newborn -> juvenile.
  float newborn_progress = 0.0F;

  /// Accumulated fractional heads maturing juvenile -> adult.
  float juvenile_progress = 0.0F;

  /// Accumulated fractional births (adult females x kind birth rate).
  float birth_progress = 0.0F;

  /// Accumulated fractional heads culled as surplus males. Half of what
  /// matures is male and the herd keeps only its share of sires, so the cull
  /// is a fractional flow like the rest and needs its own carry — without
  /// it, a herd that matures one head a day would never cull anyone.
  float cull_progress = 0.0F;

  /// Accumulated fractional heads owed to hunger. Death is a flow like the
  /// others and needs the same carry: five percent of a fifteen-head barn is
  /// three quarters of an animal, and a rule that truncates that to zero
  /// takes NOBODY from any herd under twenty head — which at the start
  /// canon's sizes is the ordinary case, not the corner one. A barn would
  /// have starved for ever at half milk and no deaths at all.
  float hunger_progress = 0.0F;

  /// Accumulated fractional heads owed to the frost (Livestock design,
  /// «Числа лестницы — Эпоха I»: at «замерзает» a share of the ADULTS a day,
  /// world_params `livestock_freezing_loss_share_day`; save 119) — the same
  /// carry as hunger_progress, for the same reason: 3 % of forty is a head
  /// and a fifth, and a whole number a day would round the loss away.
  float frost_progress = 0.0F;

  /// Sum of the adult heads' ages in GAME years — total age, not years since
  /// adulthood, because that is what the lifespan band of livestock.csv
  /// measures. Maintained by the same flows (daily aging, +adult-entry age
  /// per maturation, -mean per death); the mean drives the age-death draw —
  /// the "threshold with randomness" of the boss rules over a count-only
  /// cohort.
  ///
  /// GAME years, and the name says so on purpose: the design's livestock
  /// ages already have the x4 life acceleration applied to them, so nothing
  /// here may divide by it a second time (boss parcel 2026-08-30).
  float adult_age_game_years_total = 0.0F;

  /// The youngest and the oldest adult head's age, GAME years: the band the
  /// age death reads (core_common/herd_age_band.h; 0.35.16, save 91). Kept by
  /// the flows that add, age and take adults; meaningless with no adults,
  /// and set anew by the next head that comes in.
  float adult_age_min_game_years = 0.0F;
  float adult_age_max_game_years = 0.0F;

  /// THE BAND IN TWO (save 103; boss-core-epoch1-resume [80]-[81]): a group
  /// of adults a game year or more away from the band — young horses bought
  /// into a row of old ones — is kept as a band of its own, so that a cut
  /// takes the old ones and not a share of an imagined even spread. With
  /// `adult_older_count` 0 there is one band, [min, max]; with it between 0
  /// and adult_count, the younger band is [min, adult_younger_to] of the
  /// rest and the older [adult_older_from, max] of these. Kept by
  /// herd_age_band.h and nothing else.
  float adult_older_from_game_years = 0.0F;
  float adult_younger_to_game_years = 0.0F;
  std::uint16_t adult_older_count = 0;

  /// Heads of every rung with no room under the roof (herd_system.cpp,
  /// BilletHerds counts the herd whole; «adult» stood here until 0.37.62 and
  /// was never so), BILLETED at private yards
  /// (livestock design §6, boss answer 2026-08-30). They are not slaughtered
  /// and they stay kolkhoz property — the milk is the farm's, not the
  /// family's. One number, not an allocation per household: who took the
  /// billet decides nothing, and modelling it would cost memory and an
  /// explanation the player never needs. Billeting is paid for in leakage,
  /// never in deaths.
  std::uint16_t billeted_count = 0;

  /// Consecutive days the herd went underfed (stage 6): produce drops at
  /// once, deaths begin past the config threshold. Reset by a fed day.
  /// The freeze ladder proper (question 99) stays a STUB — every phase-1
  /// herd stands under a roof.
  float unfed_days = 0.0F;

  /// THE SHARE OF TODAY'S RATION THE HERD GOT, 0..1 (boss seq 171 А; save
  /// 71): what the day's feeding covered of the need. Hunger is a SHARE and
  /// not a yes-or-no: the produce is scaled by it, floored at
  /// unfed_produce_factor, so bought feed that closes a third of the ration
  /// gives a third back — until 2026-09-19 a herd fed 30 % gave exactly what
  /// one fed 0 % did (host's MG+, «как без корма, до сотых»). 1 for a fed day
  /// and for the yards' self-fed beasts.
  float fed_share = 1.0F;

  /// STUB: disease degree 0-3. The field exists so saves and interfaces are
  /// final; no mechanics reads or writes it in phase 1.
  std::uint8_t disease_stage = 0;

  /// 0/1: this autumn's pig slaughter is done (livestock design, «Свиньи —
  /// сезонное содержание»; boss, host-econ-shops seq 26-28; save 76). THE
  /// SLAUGHTER WAITS FOR ROOM: on an October day with no room in the stores
  /// for its meat it does not happen and kSlaughterWaitsForRoom says so; the
  /// first day with room it does, and the month's last day it does whatever
  /// the room. Set when it happens, cleared outside the slaughter month. The
  /// herd must remember it: the sows kept are a share of the adults LEFT, so
  /// a slaughter asked again the next day would take a third of what it kept,
  /// and the herd would melt day by day through October.
  std::uint8_t autumn_slaughter_done = 0;

  /// THE COLD NIGHTS' COUNTER (Livestock design, «Числа лестницы — Эпоха I»,
  /// in force by boss-core-start-no-yards [15]; save 120): one per herd, the
  /// stage read off it as hunger's is off unfed_days. A night below the
  /// kind's threshold for its place (livestock.csv `cold_night_cold_place_c`
  /// or `cold_night_warm_place_c`) adds `livestock_cold_step_night`, one
  /// below weather_params `still_frost_c` adds `livestock_cold_step_still_frost`,
  /// any other night adds `livestock_cold_step_warm_night` (negative); never
  /// below 0, and the morning after a move from a cold place to a warm one it
  /// is 0. «Мёрзнет» from 1, «замерзает» from `livestock_freezing_counter`.
  /// A herd on billet has none of it: 0.
  std::uint8_t cold_nights = 0;

  /// 0/1: last night's count was made in a COLD place (save 120). The
  /// reset «переезд в тёплое место обнуляет счётчик назавтра» needs
  /// yesterday's place, and nothing else in the state keeps it: a barn
  /// raised or insulated this morning is warm in the previous step's buffer
  /// too, an hour back. Written by the ladder each day with the place it
  /// counted in.
  std::uint8_t cold_place_yesterday = 0;

  /// Game man-days of barn work left today (stage 5). Refilled every morning
  /// by the labor sub-step from the kind's yearly care norm (real man-days
  /// / 7 / days per year x heads), drained by assigned kHerdCare workers.
  /// Unmet care has no consequence yet — that STUB ties into feeding
  /// (stage 6). Zero for household-standing herds (see @file).
  float care_days_remaining = 0.0F;

  /// THE WAIT A HEAD OF IT STANDS IN (save 138; architecture §7ж³; routing
  /// stage B, B6): a riding horse spending the night at its worker's yard
  /// (wait_state.h, kHorseAtWorkersYard). The herd is the agent: the core
  /// keeps a horse as a head of the kolkhoz team, not as a row of its own.
  /// Nothing makes it yet — the rider kept overnight is queued after stage B
  /// — and the watchdog's rules for it stand ready (the contract builds no
  /// dog with a kind that has none).
  std::optional<WaitRecord> wait;
};

/// @brief The herds table type used by WorldState.
using HerdTable = StateTable<HerdId, HerdRow>;

/// The days of the harness's rolling week (TractionWatch): the alarm
/// «лошадей не хватает» asks a week, not a day (boss-core-epoch1-queue [67]).
inline constexpr std::uint32_t kHarnessWeekDays = 7;

/// @brief What the team's two alarms remember between days (save 109;
///        boss-core-epoch1-queue [84], [90]; econ canon-horses-oats.md §3, §4).
///
/// Written only by the herd day (herd_system.cpp, RunHerdDay), at the tick
/// that turns the day; read by the production alarms.
///
/// «УПРЯЖЬ НА СЕНЕ»: the team's work ration short of full on consecutive
/// WORKING days — a day with the work ration's room above nought, a horse in
/// the traces — short beyond what the takes' whole grams may cost. A day
/// nobody works leaves both numbers as they are; the first day of the full
/// ration, or a day with no adult horse of the kolkhoz's, clears them.
///
/// «ЛОШАДЕЙ НЕ ХВАТАЕТ»: the harnessed assignment-days and those of them a
/// horse carried, a slot a day by `calendar.day % kHarnessWeekDays`, every
/// day written (a day with no harness writes noughts), so the sums are the
/// last seven days'.
struct TractionWatch {
  /// Working days in a row the team's work ration fell short of full.
  std::uint16_t short_ration_days = 0;

  /// Grams of the team's work grain those days lacked: the work ration's
  /// uncovered feed units over the feed value of the horse's first
  /// work-only feed (oats in the shipped tables).
  Grams work_grain_short = 0;

  /// Harnessed assignment-days by day of the week (the mechanisation
  /// share's denominator, YearLedger::harnessed_assignment_days).
  std::array<float, kHarnessWeekDays> week_harnessed = {};

  /// Of them, the horse-backed ones (its numerator).
  std::array<float, kHarnessWeekDays> week_horse_backed = {};

  // NO HARNESS PEAK HERE (0.37.141). 0.37.140's contract carried one — the
  // most horses a day held under work that cannot wait, this year's and the
  // year gone's — for the hay lamp's first floor, and it was withdrawn before
  // anything wrote it: measured on nine villages, the peak WAS the herd
  // (16, 18, 24, 28 against 16, 19, 25, 28 adults), and the month's need
  // followed it a year later. A measure of how busy the horses are says how
  // many there ARE, not how many are needed (boss-all-carts-carry-people-go-
  // 2026-10-02 [127], [128]). The horses' floor is the ploughing's.
};

}  // namespace core

#endif  // CORE_COMMON_HERD_STATE_H_
