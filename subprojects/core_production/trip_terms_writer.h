/// @file
/// @brief PRODUCTION WRITES THE TRIP's FACTS (#1056 C3; core_common/trip_terms.h
///        for the whole design): every evening, under CartTripModel::kPlanned,
///        each open logistics task a cart prices gets its TaskTripTerms from
///        THE SAME RATE its seam is priced by - so the groom's plan draws its
///        trips from the price's own facts (econ's ruling 2: the road bed and
///        the heap point), and writes the price from them (trip_pricing.h).
///
/// THE RATE BY THE TASK's KIND (one home each, the function the seam's own
/// pricing calls today):
///   * kFieldHeap: FieldHaulRate from FieldHeapPoint (field_haul.h);
///   * kStandLogs, kSiteDig: the stand's / the pit's rate to its pile, as
///     SettleStandHauling / SettleSiteHauling price it;
///   * kStoreTransfer: SettleStoreEmptying's rate to the taking store;
///   * kDistrictLot: LotOwnCartDays's rate (district_carts.h);
///   * kSiteSupply: the works' front supply leg's rate (supply_haul.h, LegDays);
///   * kMilkLeg, kRickSupply, and (with C2) kManureField: their seams' rates.
/// A kind with no cart rate (on foot by choice, no carts in the settlement)
/// gets all-zero terms: the plan draws it as before C3 and writes no price.
///
/// @threading SINGLE_THREADED
/// Phase 4, production's evening settlement, after every seam is re-priced.
#ifndef CORE_PRODUCTION_TRIP_TERMS_WRITER_H_
#define CORE_PRODUCTION_TRIP_TERMS_WRITER_H_

#include "core_common/trip_terms.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief One task's facts from its load's rate, tonight.
/// @return false (with `terms` all zero) for a task no cart prices: no rate,
///         a load priced on foot, a task whose load is gone.
bool TaskTripTermsOf(const ProductionConfig& config,
                     const WorldState& world,
                     const LogisticsTaskRow& task,
                     TaskTripTerms& terms);

/// @brief Writes TaskTripTermsOf on every open task (LogisticsTaskRow::
///        trip_terms) under CartTripModel::kPlanned; zeroes them under every
///        other model.
void WriteTaskTripTerms(const ProductionConfig& config, WorldState& world);

}  // namespace core

#endif  // CORE_PRODUCTION_TRIP_TERMS_WRITER_H_
