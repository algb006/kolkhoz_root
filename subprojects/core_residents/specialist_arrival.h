// Internal to core_residents: the district's specialists of Epoch I — the
// primary teacher and the librarian (core_common/specialist_state.h).
//
// @threading SINGLE_THREADED
// Runs from the residents' decisions sub-step (phase 3), once a day, after
// demography has settled who lives where. It adds residents and families,
// fills houses and writes a resident's post, so it can only live in a
// sequential slot.

#ifndef CORE_RESIDENTS_SPECIALIST_ARRIVAL_H_
#define CORE_RESIDENTS_SPECIALIST_ARRIVAL_H_

#include <cstdint>

#include "core_common/world_state.h"
#include "life_config.h"

namespace core {

/// @brief One day of the district's specialists: those due arrive, and on
///        the first day of a month in Epoch I the district decides whom to
///        send (specialist_state.h, @file).
///
/// ARRIVAL: a row whose day has come takes the first free house — an empty
/// housing unit standing at level 1 or more; never a house raised from
/// nothing — and becomes a resident aged 20..30, of either sex, with a
/// vocational education, a household of one, holding the row's post at the
/// row's unit; kSpecialistArrived goes out. No free house: the row waits for
/// the next day.
///
/// DECISION, first day of a month, Epoch I only (from Epoch II the district
/// wants points — STUB, nobody is sent):
///   * teachers: the children of the junior school band (age_school_junior_
///     from_years <= age < age_school_senior_from_years) need
///     ceil(children / teacher_pupils_per_teacher) teachers; the village's
///     teacher posts at schools plus the teachers on the road are counted
///     against it, and one is sent to the first standing school when short;
///   * librarian: each standing reading hut with no librarian holding a post
///     there and none on the road is sent one.
/// A specialist is sent only while the free houses outnumber the specialists
/// already on the road; otherwise kSpecialistNoHousing names the unit and the
/// post, and the month passes.
///
/// WRITES A RESIDENT'S POST, which core_labor otherwise writes at the day's
/// close from an order: the district's specialist "is appointed at once"
/// (education design), and there is no order for the chairman to give.
void RunSpecialistArrivals(const LifeConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_SPECIALIST_ARRIVAL_H_
