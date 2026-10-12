/// @file
/// @brief The porter (#671; transport design §2, «Как носят: один груз за
///        ходку, вид — по форме груза»): what a resident carrying on foot
///        has on his back on the leg he walks now, for the layer to draw him.
/// @threading PARALLEL_READONLY
/// Plain data, derived from a completed world between steps (the day's groom
/// plan, logistics_state.h) and read from anywhere afterwards. Nothing in the
/// simulation reads it back.
///
/// THE FIRST WALKER SEAM THE LAYER READS (ue's answer of 12 October 2026,
/// through boss): the layer reads no CartPlan and no groom_plan field, and
/// its resident view carries no place and no activity — every walker of the
/// demo is staged by the layer. So this record is the reader's WHOLE view of
/// a walker on a leg: who, from where to where, when, what, how much, and
/// with whom. It does not extend a reader that does not exist.
///
/// WHO IS IN IT: every carrier on foot of the day's groom plan
/// (CartPlan::on_foot) on the leg he has begun and not ended at the
/// completed state's tick - a loaded trip from the load to its receiver, the
/// empty way back, or the walk between loads and home. A carrier on a load
/// with no logistics task is not in the plan (logistics_plan.cpp: «a
/// carrier on a load with no task is the placement's alone») and so not
/// here; every load kind has its task since stage V2, so the class is empty
/// by construction, not by search. A cart's driver is NOT a porter: carts
/// keep their own door (CargoFormOfLeg).
///
/// THE RULES BEHIND THE NUMBERS (transport design §2, boss's answers of
/// 12 October 2026, 5c6b916c), all owned by the core's haul (haul.h,
/// WalkerTripGrams) and priced by production:
///   * ONE RESOURCE A TRIP. A carrier's back holds one resource; a works'
///     supply leg of several is walked one resource at a time, in resource-id
///     order, as the seam drains it. Only a cart mixes («в таре с тарой»).
///   * WHAT A TRIP TAKES, BY THE LOAD'S FORM (resources.csv `cargo_form`):
///     bulk, packed and long boards carry_kg_adult (20 kg); a rick - hay,
///     straw - carry_kg_rick (10 kg, a bundle on the back); manure on a
///     stretcher of two, carry_kg_stretcher_pair (40 kg) a pair, the same
///     grams a head; the odd hand left over carries manure ALONE as bulk, 20 kg
///     in buckets, and waits for nobody.
///   * LOGS BY TWO, A WORKS' SUPPLY ONLY, THE LAST RESORT: one log (its
///     resource's piece mass, 200 kg) a pair trip, 100 kg a head, at
///     log_pair_loaded_pace (0.5) of the walk on the loaded leg, and only
///     when all three hold (LogPairMayCarry): the works is kDelivering with
///     logs still awaiting - the state that puts «Нет материалов» on its
///     builders (resident_activity_details.csv `no_material`); NOT the alarm
///     kSiteWithoutMaterials, which lights only when the village does not
///     HAVE the material, so on it the rule could never fire - no draught
///     horse was free at the morning's placement (DraughtHorsesFree); and the
///     walk from the leg's sender to the site is at most log_pair_max_leg_m
///     (300 m). Never a stand's logs, a dig, or the district's lot.
///   * A WORKS' SUPPLY ON FOOT IS PRICED PER RESOURCE: its grams over that
///     resource's trip, summed - straw and hay make a pre-yard works dearer
///     in hands than the same weight of boards.
/// The numbers live in labor.csv beside carry_kg_adult (carry_kg_rick,
/// carry_kg_stretcher_pair, log_pair_max_leg_m, log_pair_loaded_pace): the
/// core's own table (db.py does not write it), the keys added by core, all
/// STUB, the numbers econ's.
///
/// CONTRACT, NO IMPLEMENTATION (core rules §12a): the producer
/// (ILogisticsSystem::CollectPorterLegs), the plan's new fields
/// (logistics_state.h, save), the year's book (ledger_state.h, save) and the
/// session's door come with the implementation.

#ifndef CORE_COMMON_PORTER_LEG_H_
#define CORE_COMMON_PORTER_LEG_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/cargo_form.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One porter on the leg he walks now: everything the layer needs to
///        put him on the map, give him his load and move him along.
struct PorterLeg {
  /// Who walks. Never invalid in a record.
  ResidentId porter;

  /// The other carrier of a stretcher (manure) or of a log, who walks the
  /// SAME legs with the same ticks; invalid for a carrier alone - every other
  /// form, and the odd hand on manure. Each of the two has his own record,
  /// each naming the other.
  ResidentId partner;

  /// Where the leg starts and ends, metres from the map's south-west corner
  /// (start_layout.csv): a load, its receiver, a house. The layer
  /// interpolates between them over [depart, arrive].
  Vec2 from;
  Vec2 to;

  /// The ticks he leaves `from` and reaches `to` - the plan's estimate,
  /// not finer than a game hour (CartLeg::depart, CartLeg::arrive). The leg
  /// is reported while depart <= the completed tick < arrive.
  Tick depart = 0;
  Tick arrive = 0;

  /// 1 on a LOADED leg (from the load to its receiver); 0 on the way back
  /// and on a walk between loads or home - he carries his empty basket,
  /// sack or stretcher, and `resource`/`form` still say which.
  std::uint8_t loaded = 0;

  /// The ONE resource of his trip (one resource a trip); for the way back
  /// of a trip, the resource the trip carried. Invalid on a walk with no
  /// load behind it (to his first load, home in the evening).
  ResourceId resource;

  /// The resource's form (resources.csv `cargo_form`), which decides what is
  /// in his hands: a sack or basket (kBulk, kPacked), a bundle on the back
  /// (kRick), a stretcher with `partner` or buckets alone (kManure), boards
  /// on the shoulder or a log with `partner` (kLong). kNone with no resource.
  CargoForm form = CargoForm::kNone;

  /// What his back holds on a loaded leg, in grams - HIS share: half the
  /// log or the stretcher's load in a pair. 0 on an empty leg.
  Grams grams = 0;

  /// `grams` over a full trip of his form and pairing (WalkerTripGrams):
  /// 1 on a full trip, below 1 on the last of a load; 0 on an empty leg. The
  /// layer bends the back and slows the step by it; no number is shown
  /// (transport §2, «Нагрузка — фигурой, не цифрой»).
  float load_share = 0.0F;
};

}  // namespace core

#endif  // CORE_COMMON_PORTER_LEG_H_
