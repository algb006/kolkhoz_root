/// @file
/// @brief #2191 «застал вора» and the chairman's seven choices about a caught thief (crime.md
///        «Ночью кражу видно, и вора можно застать», «Решения председателя о пойманном воре»;
///        econ's theft-grain-store-consolidated §4-§5; boss 26 Nov 2026). The state and the
///        numbers: core_common/thief_catch_state.h.
/// @threading SINGLE_THREADED
/// Runs in the residents' decisions sub-step (phase 3) on the sim thread, beside link 2's theft
/// (food_theft.h): it writes ThiefCatchState, the chairman's Детектив and Дисциплина, the
/// families' attitude, the authority (a deed), the district's reputation, a resident's labour
/// days and his time away, a distiller's trade, a store's stock and the step's events, so it
/// can only live in a sequential slot.
///
/// CONTRACT ONLY (core §12a): the declarations; the bodies are #2191's implementation.
#ifndef CORE_RESIDENTS_THIEF_CATCH_H_
#define CORE_RESIDENTS_THIEF_CATCH_H_

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"

namespace core {

struct NightTradeConfig;

/// @brief Clears the last night's sacks: called on a moonlit night's hour out, before link
///        2a's theft (food_theft.cpp, RunFoodTheft) records the new night's.
/// @post ThiefCatchState::last_night is empty.
void OpenTheftNight(WorldState& current);

/// @brief Records one sack of link 2a's theft (#2189's routing: `still` the distiller it went
///        to) in ThiefCatchState::last_night, for a catch to name.
void RecordNightSack(WorldState& current,
                     ResidentId thief,
                     UnitId unit,
                     ResourceId resource,
                     Grams grams,
                     ResidentId still);

/// @brief Is `thief` talked to (ThiefChoice::kTalk) and still in his season on `day`? Link 2a
///        skips such a man.
bool TalkedOutOfTheft(const WorldState& current, ResidentId thief, SimDay day);

/// @brief THE WATCHMAN'S CATCH: after link 2a's night, every sack taken at a store where a
///        sober watchman stands at his post although the leak was open (a drinking
///        storekeeper - StoreLeakClosed false for that reason only) opens a catch
///        (kTheftUncovered, `family` 0) - the first such sack of the night; Детектив +1.
void CatchAtWatchedStores(const NightTradeConfig& night, WorldState& current);

/// @brief Consumes the step's kCatchThief and kJudgeThief orders (order_state.h), each to
///        kDone or kRefused with its OrderRefusal, and applies the judgement's choice by
///        kThiefCatchRules. A catch not judged within judge_within_days lapses («закрыть глаза»:
///        nothing done is the choice nobody made) - attitude +2, no event of judgement.
/// @param life_speedup LifeConfig::life_speedup, for the months away of kHandToConstable.
void ConsumeThiefCatchOrders(const NightTradeConfig& night,
                             float life_speedup,
                             WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_THIEF_CATCH_H_
