/// @file
/// @brief #2191 «ЗАСТАЛ ВОРА» (crime.md «Ночью кражу видно, и вора можно застать», «Решения
///        председателя о пойманном воре»; econ's theft-grain-store-consolidated §4-§5; boss's
///        word 26 Nov 2026): the moonlit night's sacks as the core remembers them, the catch
///        still open to the chairman's judgement, the seven choices and their STUB numbers.
/// @threading SINGLE_THREADED
/// Written in the residents' decisions sub-step (phase 3) on the sim thread: the night's sacks
/// by link 2a's theft (food_theft.cpp), the catch and the judgement by their orders
/// (core_residents/thief_catch.h). SAVED (save 234).
///
/// WHO CATCHES: the chairman on his night patrol (the layer sends kCatchThief: it alone knows
/// where he stood) or a watchman at his post where the store's leak stood open for another
/// reason - a drinking storekeeper (boss: a sober watchman keeps DETERRING, the leak closed and
/// nobody there to catch; the audit and the separation pair were measured on that).
#ifndef CORE_COMMON_THIEF_CATCH_STATE_H_
#define CORE_COMMON_THIEF_CATCH_STATE_H_

#include <array>
#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One sack of a moonlit night's link 2a theft: who, from which store, what and how
///        much, and the still it went to (#2189). The night's list is cleared on the next
///        moonlit night's hour out, so a catch can only name a thief of the last night.
struct NightSack {
  ResidentId thief;
  UnitId unit;
  ResourceId resource;
  Grams grams = 0;
  ResidentId still;  ///< the distiller whose raw it went into
  SimDay day = 0;    ///< the night's day
};

/// @brief A thief «talked to» (ThiefChoice::kTalk) and the last day of his season.
struct TalkedThief {
  ResidentId thief;
  SimDay until_day = 0;
};

/// @brief The chairman's choices about a thief he caught (crime.md, the table «Решения
///        председателя о пойманном воре»; seven, the seventh econ's §5). The two windows
///        («второе — только у идущего в криминал») are the layer's: the core accepts all seven.
enum class ThiefChoice : std::uint8_t {
  kCloseEyes = 0,     ///< «Закрыть глаза»: thefts go on; his family's attitude +2.
  kTalk,              ///< «Поговорить»: he steals nothing for a season; attitude 0.
  kReturnStolen,      ///< «Вернуть украденное»: what is left of his sack back; −2; Discipline +.
  kWorkOff,           ///< «Отработать»: a labour-day fine by the sack's weight; −1; Discipline +.
  kHandToConstable,   ///< «Сдать участковому»: away for months; −4; authority −2; district +2;
                      ///< Discipline ++.
  kTakeUnderWing,     ///< «Взять под крыло»: a counter only in Epoch I; attitude +2.
  kFindStill,         ///< «Найти аппарат»: the still his sack went to is taken (kTakeNightTrader);
                      ///< the distiller's family −2; Discipline +.
  kThiefChoiceCount,  ///< not a choice: the count, for the codecs' range check
};

/// @brief The catch's and the choices' numbers, all STUB (crime.md's table; boss's word).
struct ThiefCatchRules {
  /// Days after the catch the judgement may still be given (boss: STUB 4, a game month).
  std::uint32_t judge_within_days = kDaysPerMonth;
  /// Each choice's move of the THIEF's family's attitude to the chairman (index: ThiefChoice);
  /// kFindStill's is the DISTILLER's family's.
  std::array<float, static_cast<std::size_t>(ThiefChoice::kThiefChoiceCount)> attitude = {
      2.0F, 0.0F, -2.0F, -1.0F, -4.0F, 2.0F, -2.0F};
  /// Each choice's step of the chairman's Дисциплина (index: ThiefChoice; «+» 1, «++» 2).
  std::array<float, static_cast<std::size_t>(ThiefChoice::kThiefChoiceCount)> discipline = {
      0.0F, 0.0F, 1.0F, 1.0F, 2.0F, 0.0F, 1.0F};
  /// «Любая поимка растит шкалу Детектив»: the step of a catch.
  float detective_step = 1.0F;
  /// The scales' start until the avatar layout (metrics, avatar_scales) reaches the core: STUB.
  float scale_start = 50.0F;
  /// «Чем ниже шкала, тем больше даёт ей поступок»: below this, a step times the factor.
  float catch_up_below = 30.0F;
  float catch_up_factor = 1.5F;
  /// kHandToConstable: the authority deed (−2, «село осуждает») and the district's step (+2).
  float constable_authority = -2.0F;
  float constable_district = 2.0F;
  /// kHandToConstable: the months he is away (STUB 3).
  std::uint32_t constable_months = 3;
  /// kTalk: the days he steals nothing («на сезон»: STUB 12, three months).
  std::uint32_t talk_days = 3 * kDaysPerMonth;
  /// kWorkOff: labour days written off per kilogram of the sack (STUB 1 a 10 kg).
  float work_off_days_per_kg = 0.1F;
};

/// @brief The core's numbers of the catch (the one place they live; world_params rows only
///        when econ tunes them).
inline constexpr ThiefCatchRules kThiefCatchRules{};

/// @brief The catch state. SAVED (save 234).
struct ThiefCatchState {
  /// The last moonlit night's sacks, in the order they were taken.
  std::vector<NightSack> last_night;
  /// The catch open to the judgement: `thief` invalid when none. One at a time - a second
  /// catch while one is open replaces it (the first is judged «закрыть глаза» by default:
  /// nothing done is the choice nobody made).
  NightSack open_catch;
  SimDay caught_day = 0;
  /// 0/1: the open catch was the chairman's own (the patrol), not a watchman's.
  std::uint8_t by_chairman = 0;
  /// The year's «Взять под крыло», a counter only in Epoch I (shadow ties come with the role
  /// lines).
  std::uint32_t under_wing_count = 0;
  /// kTalk's season: the men talked to and the last day they steal nothing (link 2a skips
  /// them while the day is at or before it). Not ResidentRow::talk_until_day - that is the
  /// sport talk's (kTalkToSport), another door.
  std::vector<TalkedThief> talked;
};

}  // namespace core

#endif  // CORE_COMMON_THIEF_CATCH_STATE_H_
