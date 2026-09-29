/// @file
/// @brief The issue norm in force for every position of the bundle: the
///        chairman's where he set one, else the default rule — a share of the
///        remainder for a position a field's harvest gives, the table's
///        grams for the rest (labor-payment §7, «Норма по умолчанию — доля
///        остатка»; register 302; 0.37.29).
/// @threading SINGLE_THREADED
/// Called from the sequential decisions slot (the distribution, phase 3) and
/// between steps (the doors): it calls FoodConfig's production callbacks,
/// which may not run on a worker.
///
/// THE RULE. A position's free remainder over the funds (SealedFunds, times
/// the table's issue share) is divided among the trudodni that will claim it
/// before the position's next harvest: those earned and not yet issued, and
/// those the village is forecast to earn until the harvest window opens.
/// Recomputed at every distribution — daily (food.csv
/// distribution_period_days) — from the stock as it stands and the trudodni
/// as they stand, so the norm follows both and never stands for a month.
///
/// THE FORECAST is last year's trudodni on the same calendar days
/// (YearLedger::trudodni_by_day of the closed book): the strada where the
/// strada was. In the first year there is no last year, and the forecast is
/// the adults × kFirstYearTrudodniPerAdultDay × the working days to the
/// harvest (STUB; the line says so, IssueNormLine::forecast_fallback).
///
/// THE SUBSTITUTE'S STOCK JOINS ITS CATEGORY. A position that keeps longer
/// than another of its food category is its substitute in the bundle
/// (CoverBundle): it is issued only for what the shorter ones could not
/// cover. Under a share of the remainder the shorter ones are never short —
/// their norm shrinks with their stock — so the sauerkraut would never go
/// out and would rot beside hungry yards. Its remainder is therefore shared
/// with the shorter positions' remainder, to their harvest, and it takes the
/// same norm, covering what they cannot.
#ifndef CORE_RESIDENTS_ISSUE_NORM_H_
#define CORE_RESIDENTS_ISSUE_NORM_H_

#include <cstdint>
#include <vector>

#include "core_common/issue_norm_view.h"
#include "core_common/world_state.h"
#include "food_config.h"

namespace core {

/// First-year forecast, trudodni per adult per WORKING day: STUB, the median
/// of the canon's first year over nine seeds (0.37.27, plan700 canon:
/// trudodni ÷ able_bodied_days, 0.345..0.406, median 0.388; the second
/// year's median is 0.444). Only the first year reads it.
inline constexpr float kFirstYearTrudodniPerAdultDay = 0.39F;

/// @brief Every position of food.csv's roster, dense by ResourceId, as the
/// next distribution issues it.
/// @param reserve SealedFunds(config, world) — passed in, as the
///        distribution already holds it.
/// @param life_speedup For the first year's adult count.
/// @return Sized to the roster; a non-food position answers nought grams
///         under kTableGrams.
std::vector<IssueNormLine> ResolveIssueNorms(const FoodConfig& config,
                                             const std::vector<Grams>& reserve,
                                             const WorldState& world,
                                             float life_speedup);

/// @brief The trudodni the village is forecast to earn from today over the
/// next `days` days (see the file header); `fallback` says the first year's
/// rule answered.
float ForecastTrudodni(const FoodConfig& config,
                       const WorldState& world,
                       float life_speedup,
                       std::int32_t days,
                       bool& fallback);

/// @brief Trudodni earned and not yet issued, over every family.
float OutstandingTrudodni(const WorldState& world);

}  // namespace core

#endif  // CORE_RESIDENTS_ISSUE_NORM_H_
