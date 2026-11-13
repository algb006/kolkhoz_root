#ifndef TESTS_RUN_COMMON_LEDGER_SUMS_H_
#define TESTS_RUN_COMMON_LEDGER_SUMS_H_

/// @file ledger_sums.h
/// @brief Every YearLedger member as one number, by name (#1580's probe: which members the step at
/// hour 0
///        of a year's first day writes into the closing book - a book's sums before that step
///        against the closed book after it). GENERATED from ledger_state.h by
///        ~/claudetmp/core-gen-ledger-sums.py (157 members on wip/1586; 163 with 0.37.368's crime
///        book); a member it does not list is caught by the count check beside (kLedgerMembers
///        against AggregateArity). Scalars as they are; ranges summed; a row of structs counted by
///        its size (named «rows»).
/// @threading SINGLE_THREADED - a run's own helper.

#include <cstddef>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "core_common/aggregate_arity.h"
#include "core_common/ledger_state.h"

namespace run {

inline constexpr std::size_t kLedgerMembers = 167;
static_assert(core::AggregateArity<core::YearLedger>() == kLedgerMembers,
              "YearLedger's members changed: regenerate ledger_sums.h (core-gen-ledger-sums.py)");

template <typename T>
double ScalarOf(const T& value) {
  if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
    return static_cast<double>(value);
  } else if constexpr (requires { value.value; }) {
    return static_cast<double>(value.value);
  } else if constexpr (requires { std::begin(value); }) {
    double sum = 0.0;
    for (const auto& element : value) {
      sum += ScalarOf(element);
    }
    return sum;
  } else {
    return 0.0;  // a struct with no number of its own: its rows are counted by SumOf
  }
}

template <typename Range>
double SumOf(const Range& range) {
  using Element = std::decay_t<decltype(*std::begin(range))>;
  if constexpr (
      std::is_arithmetic_v<Element> || requires(Element e) { e.value; } ||
      requires(Element e) { std::begin(e); }) {
    double sum = 0.0;
    for (const auto& element : range) {
      sum += ScalarOf(element);
    }
    return sum;
  } else {
    return static_cast<double>(std::size(range));  // rows
  }
}

/// @brief (name, sum) a member, in ledger_state.h's order.
inline std::vector<std::pair<std::string_view, double>> LedgerSums(const core::YearLedger& book) {
  std::vector<std::pair<std::string_view, double>> out;
  out.reserve(kLedgerMembers);
  const auto add = [&out](std::string_view name, double sum) { out.emplace_back(name, sum); };
  add("year", ScalarOf(book.year));
  add("births", ScalarOf(book.births));
  add("deaths", ScalarOf(book.deaths));
  add("arrivals", ScalarOf(book.arrivals));
  add("departures", ScalarOf(book.departures));
  add("weddings", ScalarOf(book.weddings));
  add("births_drawn", ScalarOf(book.births_drawn));
  add("births_stopped_health", ScalarOf(book.births_stopped_health));
  add("births_stopped_away", ScalarOf(book.births_stopped_away));
  add("births_stopped_satiety", ScalarOf(book.births_stopped_satiety));
  add("rest_by_source", SumOf(book.rest_by_source));
  add("satiety_day_mean_sum", ScalarOf(book.satiety_day_mean_sum));
  add("satiety_days", ScalarOf(book.satiety_days));
  add("satiety_day_mean_min", ScalarOf(book.satiety_day_mean_min));
  add("hungry_at_once_max", ScalarOf(book.hungry_at_once_max));
  add("issued", SumOf(book.issued));
  add("ration", SumOf(book.ration));
  add("advance", SumOf(book.advance));
  add("advance_repaid", ScalarOf(book.advance_repaid));
  add("advance_written_off_ended", ScalarOf(book.advance_written_off_ended));
  add("advance_written_off_turns", ScalarOf(book.advance_written_off_turns));
  add("nets", SumOf(book.nets));
  add("night_catch", SumOf(book.night_catch));
  add("stolen", SumOf(book.stolen));
  add("samogon_paid", SumOf(book.samogon_paid));
  add("bartered", SumOf(book.bartered));
  add("forage", SumOf(book.forage));
  add("yard_produce", SumOf(book.yard_produce));
  add("plot_harvest", SumOf(book.plot_harvest));
  add("eaten", SumOf(book.eaten));
  add("harvest", SumOf(book.harvest));
  add("hauled_to_stores", SumOf(book.hauled_to_stores));
  add("dug", SumOf(book.dug));
  add("hauled_from_sites", SumOf(book.hauled_from_sites));
  add("lost_no_room", SumOf(book.lost_no_room));
  add("lost_to_snow", SumOf(book.lost_to_snow));
  add("seized", SumOf(book.seized));
  add("reaping_today", ScalarOf(book.reaping_today));
  add("reaping_last_day", ScalarOf(book.reaping_last_day));
  add("reaping_today_hands", ScalarOf(book.reaping_today_hands));
  add("reaping_last_day_hands", ScalarOf(book.reaping_last_day_hands));
  add("reaping_today_hours", ScalarOf(book.reaping_today_hours));
  add("reaping_last_day_hours", ScalarOf(book.reaping_last_day_hours));
  add("seed", SumOf(book.seed));
  add("area_sown_ha", ScalarOf(book.area_sown_ha));
  add("area_harvested_ha", ScalarOf(book.area_harvested_ha));
  add("area_lost_ha", ScalarOf(book.area_lost_ha));
  add("area_manured_ha", ScalarOf(book.area_manured_ha));
  add("boards_sawn_m3", ScalarOf(book.boards_sawn_m3));
  add("manure_plowed_in", ScalarOf(book.manure_plowed_in));
  add("manure_carted", ScalarOf(book.manure_carted));
  add("manure_late", ScalarOf(book.manure_late));
  add("herd_produce", SumOf(book.herd_produce));
  add("adult_head_days", SumOf(book.adult_head_days));
  add("feed", SumOf(book.feed));
  add("herd_births", ScalarOf(book.herd_births));
  add("herd_deaths_age", ScalarOf(book.herd_deaths_age));
  add("herd_deaths_hunger", ScalarOf(book.herd_deaths_hunger));
  add("herd_culled", ScalarOf(book.herd_culled));
  add("herd_young_culled", SumOf(book.herd_young_culled));
  add("herd_surplus_slaughtered", SumOf(book.herd_surplus_slaughtered));
  add("herd_autumn_slaughtered", SumOf(book.herd_autumn_slaughtered));
  add("herd_frozen", SumOf(book.herd_frozen));
  add("herd_hungry_head_days", ScalarOf(book.herd_hungry_head_days));
  add("delivered", SumOf(book.delivered));
  add("delivered_own", SumOf(book.delivered_own));
  add("plan_due", SumOf(book.plan_due));
  add("plan_delivered", SumOf(book.plan_delivered));
  add("milk_debt", ScalarOf(book.milk_debt));
  add("samogon_brewed_ml", ScalarOf(book.samogon_brewed_ml));
  add("samogon_sold_ml", ScalarOf(book.samogon_sold_ml));
  add("goods_loan_taken", SumOf(book.goods_loan_taken));
  add("goods_loan_repaid", SumOf(book.goods_loan_repaid));
  add("spoiled", SumOf(book.spoiled));
  add("spoiled_in_clamps", SumOf(book.spoiled_in_clamps));
  add("built_in", SumOf(book.built_in));
  add("built_in_by_type", ScalarOf(book.built_in_by_type));
  add("yard_feed", SumOf(book.yard_feed));
  add("processed", SumOf(book.processed));
  add("made", SumOf(book.made));
  add("work_days_by_kind", SumOf(book.work_days_by_kind));
  add("road_blocked_job_days", SumOf(book.road_blocked_job_days));
  add("idle_person_days", SumOf(book.idle_person_days));
  add("short_job_days", SumOf(book.short_job_days));
  add("offered_job_days", ScalarOf(book.offered_job_days));
  add("candidate_person_days", ScalarOf(book.candidate_person_days));
  add("cart_trips", SumOf(book.cart_trips));
  add("cart_off_road_m", SumOf(book.cart_off_road_m));
  add("cart_off_road_worst_m", SumOf(book.cart_off_road_worst_m));
  add("cart_trips_off_road", SumOf(book.cart_trips_off_road));
  add("cart_store_off_road_m", SumOf(book.cart_store_off_road_m));
  add("cart_store_off_road_worst_m", SumOf(book.cart_store_off_road_worst_m));
  add("cart_grams", SumOf(book.cart_grams));
  add("cart_grams_off_road", SumOf(book.cart_grams_off_road));
  add("cart_trips_paid", SumOf(book.cart_trips_paid));
  add("cart_days_drawn", ScalarOf(book.cart_days_drawn));
  add("cart_days_fallback", ScalarOf(book.cart_days_fallback));
  add("cart_trips_pooled", ScalarOf(book.cart_trips_pooled));
  add("cart_trips_back", ScalarOf(book.cart_trips_back));
  add("hauled_on_foot", SumOf(book.hauled_on_foot));
  add("hauled_by_cart", SumOf(book.hauled_by_cart));
  add("log_pair_trips", ScalarOf(book.log_pair_trips));
  add("log_pair_days", ScalarOf(book.log_pair_days));
  add("log_pair_worst_leg_m", ScalarOf(book.log_pair_worst_leg_m));
  add("log_pair_near_miss_days", ScalarOf(book.log_pair_near_miss_days));
  add("trudodni_accrued", ScalarOf(book.trudodni_accrued));
  add("trudodni_by_day", SumOf(book.trudodni_by_day));
  add("trudodni_burned", ScalarOf(book.trudodni_burned));
  add("trudodni_by_post", ScalarOf(book.trudodni_by_post));
  add("walk_offs", ScalarOf(book.walk_offs));
  add("cart_passengers", ScalarOf(book.cart_passengers));
  add("cart_passengers_no_seat", ScalarOf(book.cart_passengers_no_seat));
  add("cart_passengers_wait_refused", ScalarOf(book.cart_passengers_wait_refused));
  add("cart_wait_worst_hours", ScalarOf(book.cart_wait_worst_hours));
  add("cart_wait_hours", ScalarOf(book.cart_wait_hours));
  add("cart_hours_saved", ScalarOf(book.cart_hours_saved));
  add("waits_made", SumOf(book.waits_made));
  add("watchdog_fired", SumOf(book.watchdog_fired));
  add("horse_backed_assignment_days", ScalarOf(book.horse_backed_assignment_days));
  add("total_assignment_days", ScalarOf(book.total_assignment_days));
  add("limit_points_granted", ScalarOf(book.limit_points_granted));
  add("limit_points_spent", ScalarOf(book.limit_points_spent));
  add("limit_points_burned", ScalarOf(book.limit_points_burned));
  add("limit_points_carried", ScalarOf(book.limit_points_carried));
  add("limit_lots_paid_in_grain", ScalarOf(book.limit_lots_paid_in_grain));
  add("limit_grain_paid", ScalarOf(book.limit_grain_paid));
  add("herd_rest_days_substituted", ScalarOf(book.herd_rest_days_substituted));
  add("herd_rest_days_self_worked", ScalarOf(book.herd_rest_days_self_worked));
  add("herd_care_by_yards", ScalarOf(book.herd_care_by_yards));
  add("yard_herd_share_today", ScalarOf(book.yard_herd_share_today));
  add("satisfaction_sum", ScalarOf(book.satisfaction_sum));
  add("satisfaction_samples", ScalarOf(book.satisfaction_samples));
  add("able_bodied_days", ScalarOf(book.able_bodied_days));
  add("plan_percent", ScalarOf(book.plan_percent));
  add("plan_percent_known", ScalarOf(book.plan_percent_known));
  add("worst_season_variety", ScalarOf(book.worst_season_variety));
  add("variety_seasons_seen", ScalarOf(book.variety_seasons_seen));
  add("food_days_dec1", ScalarOf(book.food_days_dec1));
  add("feed_days_dec1", ScalarOf(book.feed_days_dec1));
  add("winter_days_dec1", ScalarOf(book.winter_days_dec1));
  add("winter_cover_taken", ScalarOf(book.winter_cover_taken));
  add("harnessed_assignment_days", ScalarOf(book.harnessed_assignment_days));
  add("team_work_share_sum", SumOf(book.team_work_share_sum));
  add("team_oats_share_sum", SumOf(book.team_oats_share_sum));
  add("team_share_days", SumOf(book.team_share_days));
  add("herd_hay_eaten", SumOf(book.herd_hay_eaten));
  add("herd_feed_short", SumOf(book.herd_feed_short));
  add("zyab_ha_dec1", ScalarOf(book.zyab_ha_dec1));
  add("zyab_fields_dec1", ScalarOf(book.zyab_fields_dec1));
  add("reputation_plan_step", ScalarOf(book.reputation_plan_step));
  add("reputation_tier_step", ScalarOf(book.reputation_tier_step));
  add("reputation_visit_step", ScalarOf(book.reputation_visit_step));
  add("reputation_losses", ScalarOf(book.reputation_losses));
  add("reputation_clean_visits", ScalarOf(book.reputation_clean_visits));
  add("reputation_tiers_reached", ScalarOf(book.reputation_tiers_reached));
  add("reputation_own_overfulfil_t", ScalarOf(book.reputation_own_overfulfil_t));
  add("team_work_oat_requested", ScalarOf(book.team_work_oat_requested));
  // #1568's (0.37.368): the crime links' book.
  add("written_off", SumOf(book.written_off));
  add("stolen_before_acceptance", SumOf(book.stolen_before_acceptance));
  add("truant_man_days", ScalarOf(book.truant_man_days));
  add("audits", ScalarOf(book.audits));
  add("audit_remarks", ScalarOf(book.audit_remarks));
  add("drunk_fires", ScalarOf(book.drunk_fires));
  add("deaths_salt", ScalarOf(book.deaths_salt));               // save 209 (#1630 (C))
  add("departures_salt", ScalarOf(book.departures_salt));       // save 209
  add("salt_loans", ScalarOf(book.salt_loans));                 // save 209
  add("spoiled_in_pantries", SumOf(book.spoiled_in_pantries));  // save 210 (#1644)
  return out;
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_LEDGER_SUMS_H_
