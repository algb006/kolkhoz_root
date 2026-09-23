/// @file
/// @brief The district's ambulance: sent for a resident whose health fell
/// below the line, at the house the next morning, the patient in the
/// district's hospital for a term, and home again (health «Скорая помощь из
/// района», register 236; boss seq 210; the contract core_common/
/// district_car_state.h, save 79).
/// @threading SINGLE_THREADED
/// Runs in the production decisions sub-step (phase 3) on the sim thread,
/// every tick: it appends and removes DistrictCarTable rows and writes the
/// patient's ResidentRow::away_* and his health on the day he is back.
///
/// NO CALL (the human's word of 2026-09-19): the district learns by itself.
/// The police car of the same table has no cause in the core and is never
/// sent (boss seq 210, 1).

#ifndef CORE_PRODUCTION_DISTRICT_CAR_H_
#define CORE_PRODUCTION_DISTRICT_CAR_H_

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief One tick of the ambulance's errands.
///
/// At the day's first tick: a car is sent for every resident whose health is
/// below `health_line`, who is at home and has no car coming; a car held by a
/// blizzard sets out the first morning without one. A car sets out for the
/// NEXT morning's `arrive_hour`, a day later in the mud (boss seq 210, 2).
///
/// Every tick: a car whose hour has come stands at the house and takes the
/// patient — away from that tick for `hospital_days` — and is gone an hour
/// later. A patient whose term is over comes home with the milk cart in its
/// season (MilkPositionStands), else walks in from the border for
/// `walk_home_hours` more; home, his health is `return_health`.
///
/// The car has no death of its own (boss seq 210, 5): a patient who dies
/// before it comes takes his row with him, and the car is dropped.
void RunDistrictCars(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_CAR_H_
