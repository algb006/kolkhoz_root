/// @file
/// @brief Taking a field off the map: the chairman's kRemoveField
/// (construction design §12, "Поле, сад — ничем: мгновенно и бесплатно";
/// start canon §2, the reserve field the start quest opens with).
/// @threading SINGLE_THREADED
/// Runs from the production sub-step of the decisions slot (phase 3) on the
/// sim thread, when the order is read. It removes a row of the fields table
/// and appends to the step's outbox, so it can only live in a sequential
/// slot (buffer-law rules 5 and 6).

#ifndef CORE_PRODUCTION_FIELD_REMOVAL_H_
#define CORE_PRODUCTION_FIELD_REMOVAL_H_

#include "core_common/order_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief Reads a kRemoveField: removes the field row at once and emits
///        kFieldRemoved with amount = the row's start_reserve mark.
/// @return kNoSuchSubject for a field that is not there; kWrongLand for a
///         meadow; kNotEmpty while the field is in kHarvest or still holds
///         reaped grain waiting for the carts; kNone when removed. A refused
///         order changes nothing and emits nothing of its own.
/// Side effects on success: the row is gone, and with it whatever was sown
/// or ploughed into it (the design's "с потерей вложенного"). Nothing else
/// is touched — a standing work order on the field is closed by labor's own
/// sweep of orders whose target is gone (work_orders.cpp), and a resident
/// working it today finds no field to work.
OrderRefusal RemoveField(WorldState& current, const OrderRow& order);

}  // namespace core

#endif  // CORE_PRODUCTION_FIELD_REMOVAL_H_
