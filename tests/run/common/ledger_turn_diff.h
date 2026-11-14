/// @file
/// @brief #1580's instrument: the YearLedger members that changed between two books -
///        the book at the close of a year's last day and the same book once closed
///        (day 0's hour 0 runs into it before the rotation). GENERATED from
///        include/core_common/ledger_state.h by ~/claudetmp/core-gen-ledger-diff.py:
///        168 members (lost_to_flood added by hand with #1375); a member added to YearLedger
///        and not here is not compared
///        - LedgerTurnDiffMembers() returns the count for a run to print beside it. The
///        BuiltInByType overload is written by hand (its rows have no operator==).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_LEDGER_TURN_DIFF_H_
#define TESTS_RUN_COMMON_LEDGER_TURN_DIFF_H_

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "core_common/ledger_state.h"

namespace run {

/// @brief The members LedgerTurnDiff compares (the generator's count).
constexpr std::size_t LedgerTurnDiffMembers() {
  return 168;
}

/// One member: its name into `changed` when the two differ.
template <typename Member>
void NoteIfChanged(const Member& before,
                   const Member& after,
                   const char* name,
                   std::vector<std::string>& changed) {
  if (!(before == after)) {
    changed.emplace_back(name);
  }
}

/// built_in_by_type's rows carry no operator== (the core never compares them): by their parts.
inline void NoteIfChanged(const core::BuiltInByType& before,
                          const core::BuiltInByType& after,
                          const char* name,
                          std::vector<std::string>& changed) {
  const auto same = [](const core::BuiltInRow& a, const core::BuiltInRow& b) {
    return a.type.value == b.type.value && a.resource.value == b.resource.value &&
           a.grams == b.grams;
  };
  if (before.size() != after.size() ||
      !std::equal(before.begin(), before.end(), after.begin(), same)) {
    changed.emplace_back(name);
  }
}

/// @brief The names of the members that differ between `before` and `after`.
inline std::vector<std::string> LedgerTurnDiff(const core::YearLedger& before,
                                               const core::YearLedger& after) {
  std::vector<std::string> changed;
  NoteIfChanged(before.year, after.year, "year", changed);
  NoteIfChanged(before.births, after.births, "births", changed);
  NoteIfChanged(before.deaths, after.deaths, "deaths", changed);
  NoteIfChanged(before.arrivals, after.arrivals, "arrivals", changed);
  NoteIfChanged(before.departures, after.departures, "departures", changed);
  NoteIfChanged(before.weddings, after.weddings, "weddings", changed);
  NoteIfChanged(before.births_drawn, after.births_drawn, "births_drawn", changed);
  NoteIfChanged(
      before.births_stopped_health, after.births_stopped_health, "births_stopped_health", changed);
  NoteIfChanged(
      before.births_stopped_away, after.births_stopped_away, "births_stopped_away", changed);
  NoteIfChanged(before.births_stopped_satiety,
                after.births_stopped_satiety,
                "births_stopped_satiety",
                changed);
  NoteIfChanged(before.rest_by_source, after.rest_by_source, "rest_by_source", changed);
  NoteIfChanged(
      before.satiety_day_mean_sum, after.satiety_day_mean_sum, "satiety_day_mean_sum", changed);
  NoteIfChanged(before.satiety_days, after.satiety_days, "satiety_days", changed);
  NoteIfChanged(
      before.satiety_day_mean_min, after.satiety_day_mean_min, "satiety_day_mean_min", changed);
  NoteIfChanged(before.hungry_at_once_max, after.hungry_at_once_max, "hungry_at_once_max", changed);
  NoteIfChanged(before.issued, after.issued, "issued", changed);
  NoteIfChanged(before.ration, after.ration, "ration", changed);
  NoteIfChanged(before.advance, after.advance, "advance", changed);
  NoteIfChanged(before.advance_repaid, after.advance_repaid, "advance_repaid", changed);
  NoteIfChanged(before.advance_written_off_ended,
                after.advance_written_off_ended,
                "advance_written_off_ended",
                changed);
  NoteIfChanged(before.advance_written_off_turns,
                after.advance_written_off_turns,
                "advance_written_off_turns",
                changed);
  NoteIfChanged(before.nets, after.nets, "nets", changed);
  NoteIfChanged(before.night_catch, after.night_catch, "night_catch", changed);
  NoteIfChanged(before.stolen, after.stolen, "stolen", changed);
  NoteIfChanged(before.samogon_paid, after.samogon_paid, "samogon_paid", changed);
  NoteIfChanged(before.bartered, after.bartered, "bartered", changed);
  NoteIfChanged(before.forage, after.forage, "forage", changed);
  NoteIfChanged(before.yard_produce, after.yard_produce, "yard_produce", changed);
  NoteIfChanged(before.plot_harvest, after.plot_harvest, "plot_harvest", changed);
  NoteIfChanged(before.eaten, after.eaten, "eaten", changed);
  NoteIfChanged(before.harvest, after.harvest, "harvest", changed);
  NoteIfChanged(before.hauled_to_stores, after.hauled_to_stores, "hauled_to_stores", changed);
  NoteIfChanged(before.dug, after.dug, "dug", changed);
  NoteIfChanged(before.hauled_from_sites, after.hauled_from_sites, "hauled_from_sites", changed);
  NoteIfChanged(before.lost_no_room, after.lost_no_room, "lost_no_room", changed);
  NoteIfChanged(before.lost_to_snow, after.lost_to_snow, "lost_to_snow", changed);
  NoteIfChanged(before.lost_to_flood, after.lost_to_flood, "lost_to_flood", changed);
  NoteIfChanged(before.seized, after.seized, "seized", changed);
  NoteIfChanged(before.reaping_today, after.reaping_today, "reaping_today", changed);
  NoteIfChanged(before.reaping_last_day, after.reaping_last_day, "reaping_last_day", changed);
  NoteIfChanged(
      before.reaping_today_hands, after.reaping_today_hands, "reaping_today_hands", changed);
  NoteIfChanged(before.reaping_last_day_hands,
                after.reaping_last_day_hands,
                "reaping_last_day_hands",
                changed);
  NoteIfChanged(
      before.reaping_today_hours, after.reaping_today_hours, "reaping_today_hours", changed);
  NoteIfChanged(before.reaping_last_day_hours,
                after.reaping_last_day_hours,
                "reaping_last_day_hours",
                changed);
  NoteIfChanged(before.seed, after.seed, "seed", changed);
  NoteIfChanged(before.area_sown_ha, after.area_sown_ha, "area_sown_ha", changed);
  NoteIfChanged(before.area_harvested_ha, after.area_harvested_ha, "area_harvested_ha", changed);
  NoteIfChanged(before.area_lost_ha, after.area_lost_ha, "area_lost_ha", changed);
  NoteIfChanged(before.area_manured_ha, after.area_manured_ha, "area_manured_ha", changed);
  NoteIfChanged(before.boards_sawn_m3, after.boards_sawn_m3, "boards_sawn_m3", changed);
  NoteIfChanged(before.manure_plowed_in, after.manure_plowed_in, "manure_plowed_in", changed);
  NoteIfChanged(before.manure_carted, after.manure_carted, "manure_carted", changed);
  NoteIfChanged(before.manure_late, after.manure_late, "manure_late", changed);
  NoteIfChanged(before.herd_produce, after.herd_produce, "herd_produce", changed);
  NoteIfChanged(before.adult_head_days, after.adult_head_days, "adult_head_days", changed);
  NoteIfChanged(before.feed, after.feed, "feed", changed);
  NoteIfChanged(before.herd_births, after.herd_births, "herd_births", changed);
  NoteIfChanged(before.herd_deaths_age, after.herd_deaths_age, "herd_deaths_age", changed);
  NoteIfChanged(before.herd_deaths_hunger, after.herd_deaths_hunger, "herd_deaths_hunger", changed);
  NoteIfChanged(before.herd_culled, after.herd_culled, "herd_culled", changed);
  NoteIfChanged(before.herd_young_culled, after.herd_young_culled, "herd_young_culled", changed);
  NoteIfChanged(before.herd_surplus_slaughtered,
                after.herd_surplus_slaughtered,
                "herd_surplus_slaughtered",
                changed);
  NoteIfChanged(before.herd_autumn_slaughtered,
                after.herd_autumn_slaughtered,
                "herd_autumn_slaughtered",
                changed);
  NoteIfChanged(before.herd_frozen, after.herd_frozen, "herd_frozen", changed);
  NoteIfChanged(
      before.herd_hungry_head_days, after.herd_hungry_head_days, "herd_hungry_head_days", changed);
  NoteIfChanged(before.delivered, after.delivered, "delivered", changed);
  NoteIfChanged(before.delivered_own, after.delivered_own, "delivered_own", changed);
  NoteIfChanged(before.plan_due, after.plan_due, "plan_due", changed);
  NoteIfChanged(before.plan_delivered, after.plan_delivered, "plan_delivered", changed);
  NoteIfChanged(before.milk_debt, after.milk_debt, "milk_debt", changed);
  NoteIfChanged(before.samogon_brewed_ml, after.samogon_brewed_ml, "samogon_brewed_ml", changed);
  NoteIfChanged(before.samogon_sold_ml, after.samogon_sold_ml, "samogon_sold_ml", changed);
  NoteIfChanged(before.goods_loan_taken, after.goods_loan_taken, "goods_loan_taken", changed);
  NoteIfChanged(before.goods_loan_repaid, after.goods_loan_repaid, "goods_loan_repaid", changed);
  NoteIfChanged(before.spoiled, after.spoiled, "spoiled", changed);
  NoteIfChanged(before.spoiled_in_clamps, after.spoiled_in_clamps, "spoiled_in_clamps", changed);
  NoteIfChanged(before.built_in, after.built_in, "built_in", changed);
  NoteIfChanged(before.built_in_by_type, after.built_in_by_type, "built_in_by_type", changed);
  NoteIfChanged(before.yard_feed, after.yard_feed, "yard_feed", changed);
  NoteIfChanged(before.processed, after.processed, "processed", changed);
  NoteIfChanged(before.made, after.made, "made", changed);
  NoteIfChanged(before.work_days_by_kind, after.work_days_by_kind, "work_days_by_kind", changed);
  NoteIfChanged(
      before.road_blocked_job_days, after.road_blocked_job_days, "road_blocked_job_days", changed);
  NoteIfChanged(before.idle_person_days, after.idle_person_days, "idle_person_days", changed);
  NoteIfChanged(before.short_job_days, after.short_job_days, "short_job_days", changed);
  NoteIfChanged(before.offered_job_days, after.offered_job_days, "offered_job_days", changed);
  NoteIfChanged(
      before.candidate_person_days, after.candidate_person_days, "candidate_person_days", changed);
  NoteIfChanged(before.cart_trips, after.cart_trips, "cart_trips", changed);
  NoteIfChanged(before.cart_off_road_m, after.cart_off_road_m, "cart_off_road_m", changed);
  NoteIfChanged(
      before.cart_off_road_worst_m, after.cart_off_road_worst_m, "cart_off_road_worst_m", changed);
  NoteIfChanged(
      before.cart_trips_off_road, after.cart_trips_off_road, "cart_trips_off_road", changed);
  NoteIfChanged(
      before.cart_store_off_road_m, after.cart_store_off_road_m, "cart_store_off_road_m", changed);
  NoteIfChanged(before.cart_store_off_road_worst_m,
                after.cart_store_off_road_worst_m,
                "cart_store_off_road_worst_m",
                changed);
  NoteIfChanged(before.cart_grams, after.cart_grams, "cart_grams", changed);
  NoteIfChanged(
      before.cart_grams_off_road, after.cart_grams_off_road, "cart_grams_off_road", changed);
  NoteIfChanged(before.cart_trips_paid, after.cart_trips_paid, "cart_trips_paid", changed);
  NoteIfChanged(before.cart_days_drawn, after.cart_days_drawn, "cart_days_drawn", changed);
  NoteIfChanged(before.cart_days_fallback, after.cart_days_fallback, "cart_days_fallback", changed);
  NoteIfChanged(before.cart_trips_pooled, after.cart_trips_pooled, "cart_trips_pooled", changed);
  NoteIfChanged(before.cart_trips_back, after.cart_trips_back, "cart_trips_back", changed);
  NoteIfChanged(before.hauled_on_foot, after.hauled_on_foot, "hauled_on_foot", changed);
  NoteIfChanged(before.hauled_by_cart, after.hauled_by_cart, "hauled_by_cart", changed);
  NoteIfChanged(before.log_pair_trips, after.log_pair_trips, "log_pair_trips", changed);
  NoteIfChanged(before.log_pair_days, after.log_pair_days, "log_pair_days", changed);
  NoteIfChanged(
      before.log_pair_worst_leg_m, after.log_pair_worst_leg_m, "log_pair_worst_leg_m", changed);
  NoteIfChanged(before.log_pair_near_miss_days,
                after.log_pair_near_miss_days,
                "log_pair_near_miss_days",
                changed);
  NoteIfChanged(before.trudodni_accrued, after.trudodni_accrued, "trudodni_accrued", changed);
  NoteIfChanged(before.trudodni_by_day, after.trudodni_by_day, "trudodni_by_day", changed);
  NoteIfChanged(before.trudodni_burned, after.trudodni_burned, "trudodni_burned", changed);
  NoteIfChanged(before.trudodni_by_post, after.trudodni_by_post, "trudodni_by_post", changed);
  NoteIfChanged(before.walk_offs, after.walk_offs, "walk_offs", changed);
  NoteIfChanged(before.cart_passengers, after.cart_passengers, "cart_passengers", changed);
  NoteIfChanged(before.cart_passengers_no_seat,
                after.cart_passengers_no_seat,
                "cart_passengers_no_seat",
                changed);
  NoteIfChanged(before.cart_passengers_wait_refused,
                after.cart_passengers_wait_refused,
                "cart_passengers_wait_refused",
                changed);
  NoteIfChanged(
      before.cart_wait_worst_hours, after.cart_wait_worst_hours, "cart_wait_worst_hours", changed);
  NoteIfChanged(before.cart_wait_hours, after.cart_wait_hours, "cart_wait_hours", changed);
  NoteIfChanged(before.cart_hours_saved, after.cart_hours_saved, "cart_hours_saved", changed);
  NoteIfChanged(before.waits_made, after.waits_made, "waits_made", changed);
  NoteIfChanged(before.watchdog_fired, after.watchdog_fired, "watchdog_fired", changed);
  NoteIfChanged(before.horse_backed_assignment_days,
                after.horse_backed_assignment_days,
                "horse_backed_assignment_days",
                changed);
  NoteIfChanged(
      before.total_assignment_days, after.total_assignment_days, "total_assignment_days", changed);
  NoteIfChanged(
      before.limit_points_granted, after.limit_points_granted, "limit_points_granted", changed);
  NoteIfChanged(before.limit_points_spent, after.limit_points_spent, "limit_points_spent", changed);
  NoteIfChanged(
      before.limit_points_burned, after.limit_points_burned, "limit_points_burned", changed);
  NoteIfChanged(
      before.limit_points_carried, after.limit_points_carried, "limit_points_carried", changed);
  NoteIfChanged(before.limit_lots_paid_in_grain,
                after.limit_lots_paid_in_grain,
                "limit_lots_paid_in_grain",
                changed);
  NoteIfChanged(before.limit_grain_paid, after.limit_grain_paid, "limit_grain_paid", changed);
  NoteIfChanged(before.herd_rest_days_substituted,
                after.herd_rest_days_substituted,
                "herd_rest_days_substituted",
                changed);
  NoteIfChanged(before.herd_rest_days_self_worked,
                after.herd_rest_days_self_worked,
                "herd_rest_days_self_worked",
                changed);
  NoteIfChanged(before.herd_care_by_yards, after.herd_care_by_yards, "herd_care_by_yards", changed);
  NoteIfChanged(
      before.yard_herd_share_today, after.yard_herd_share_today, "yard_herd_share_today", changed);
  NoteIfChanged(before.satisfaction_sum, after.satisfaction_sum, "satisfaction_sum", changed);
  NoteIfChanged(
      before.satisfaction_samples, after.satisfaction_samples, "satisfaction_samples", changed);
  NoteIfChanged(before.able_bodied_days, after.able_bodied_days, "able_bodied_days", changed);
  NoteIfChanged(before.plan_percent, after.plan_percent, "plan_percent", changed);
  NoteIfChanged(before.plan_percent_known, after.plan_percent_known, "plan_percent_known", changed);
  NoteIfChanged(
      before.worst_season_variety, after.worst_season_variety, "worst_season_variety", changed);
  NoteIfChanged(
      before.variety_seasons_seen, after.variety_seasons_seen, "variety_seasons_seen", changed);
  NoteIfChanged(before.food_days_dec1, after.food_days_dec1, "food_days_dec1", changed);
  NoteIfChanged(before.feed_days_dec1, after.feed_days_dec1, "feed_days_dec1", changed);
  NoteIfChanged(before.winter_days_dec1, after.winter_days_dec1, "winter_days_dec1", changed);
  NoteIfChanged(before.winter_cover_taken, after.winter_cover_taken, "winter_cover_taken", changed);
  NoteIfChanged(before.harnessed_assignment_days,
                after.harnessed_assignment_days,
                "harnessed_assignment_days",
                changed);
  NoteIfChanged(
      before.team_work_share_sum, after.team_work_share_sum, "team_work_share_sum", changed);
  NoteIfChanged(
      before.team_oats_share_sum, after.team_oats_share_sum, "team_oats_share_sum", changed);
  NoteIfChanged(before.team_share_days, after.team_share_days, "team_share_days", changed);
  NoteIfChanged(before.herd_hay_eaten, after.herd_hay_eaten, "herd_hay_eaten", changed);
  NoteIfChanged(before.herd_feed_short, after.herd_feed_short, "herd_feed_short", changed);
  NoteIfChanged(before.zyab_ha_dec1, after.zyab_ha_dec1, "zyab_ha_dec1", changed);
  NoteIfChanged(before.zyab_fields_dec1, after.zyab_fields_dec1, "zyab_fields_dec1", changed);
  NoteIfChanged(
      before.reputation_plan_step, after.reputation_plan_step, "reputation_plan_step", changed);
  NoteIfChanged(
      before.reputation_tier_step, after.reputation_tier_step, "reputation_tier_step", changed);
  NoteIfChanged(
      before.reputation_visit_step, after.reputation_visit_step, "reputation_visit_step", changed);
  NoteIfChanged(before.reputation_losses, after.reputation_losses, "reputation_losses", changed);
  NoteIfChanged(before.reputation_clean_visits,
                after.reputation_clean_visits,
                "reputation_clean_visits",
                changed);
  NoteIfChanged(before.reputation_tiers_reached,
                after.reputation_tiers_reached,
                "reputation_tiers_reached",
                changed);
  NoteIfChanged(before.reputation_own_overfulfil_t,
                after.reputation_own_overfulfil_t,
                "reputation_own_overfulfil_t",
                changed);
  NoteIfChanged(before.team_work_oat_requested,
                after.team_work_oat_requested,
                "team_work_oat_requested",
                changed);
  NoteIfChanged(before.written_off, after.written_off, "written_off", changed);
  NoteIfChanged(before.stolen_before_acceptance,
                after.stolen_before_acceptance,
                "stolen_before_acceptance",
                changed);
  NoteIfChanged(before.truant_man_days, after.truant_man_days, "truant_man_days", changed);
  NoteIfChanged(before.audits, after.audits, "audits", changed);
  NoteIfChanged(before.audit_remarks, after.audit_remarks, "audit_remarks", changed);
  NoteIfChanged(before.drunk_fires, after.drunk_fires, "drunk_fires", changed);
  NoteIfChanged(before.deaths_salt, after.deaths_salt, "deaths_salt", changed);
  NoteIfChanged(before.departures_salt, after.departures_salt, "departures_salt", changed);
  NoteIfChanged(before.salt_loans, after.salt_loans, "salt_loans", changed);
  NoteIfChanged(
      before.spoiled_in_pantries, after.spoiled_in_pantries, "spoiled_in_pantries", changed);
  return changed;
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_LEDGER_TURN_DIFF_H_
