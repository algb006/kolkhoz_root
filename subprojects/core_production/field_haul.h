/// @file
/// @brief Carrying the harvest off a field, and the going-bad of everything
/// that lies in a store.
/// @threading SINGLE_THREADED
/// Both entry points run once a day, at the day's LAST tick, from the
/// production sub-step of the decisions slot (phase 3) on the sim thread.
/// They change stored masses and write the year's ledger, so they can only
/// live in a sequential slot (buffer-law rules 5 and 6).
///
/// Model: manual/75-logistics.md; transport design §9 (how goods move
/// between stores) and §10 (shelf life). Split out of production_system.cpp
/// when that file crossed the thousand-line limit: the day of a load and the
/// day of a crop are different days, and only one of them is about growing.
#ifndef CORE_PRODUCTION_FIELD_HAUL_H_
#define CORE_PRODUCTION_FIELD_HAUL_H_

#include "core_common/haul.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Room a load of THIS RESOURCE could actually be delivered into —
/// unlike the alarm's own free-room sum, this one answers "has this got
/// anywhere to go at all", so a store bounded by an outline (a heap under
/// the open sky) makes it unbounded rather than contributing nothing.
/// @param resource What is being hauled. It is a parameter because the door
///        it has to agree with asks the same question: an outline takes a
///        load only when that resource ALREADY LIES IN IT (DeliverToStores,
///        second pass). A haystack is unbounded room for hay and no room at
///        all for rye.
/// @return The sum over numbered stores, or Grams max when an outline that
///         already holds this resource stands built.
Grams ReceivableRoom(const ProductionConfig& config, const WorldState& world, ResourceId resource);

/// @brief The terms of carting an own-carts limit lot (decision 279,
///        0.36.17): a harnessed cart's load, from the district centre —
///        `district_center_km` beyond the border — and by the network to the
///        log pile. No draught horse free: a rate with no load (nobody is
///        sent; the lot waits at the district).
HaulRate DistrictLotHaulRate(const ProductionConfig& config, const WorldState& world);

/// @brief One day of the village's carters fetching the timber lots that
///        wait at the district centre (LimitDeliveryRow::own_carts): what
///        they drained since last night comes in through the store door,
///        tomorrow's demand is written, and an empty lot's row goes.
/// @note Runs beside SettleStoreEmptying, at the day's last tick.
void SettleDistrictLotHauling(const ProductionConfig& config, WorldState& current);

/// @brief Where a field's heap stands: at the edge of the field nearest a road
///        a produce cart may use — or at that road where it crosses the field;
///        the centre when no road is near at all (0.36.15; decision 275,
///        «куча стоит у подъезда поля», boss-core-epoch1-resume [31] (b)). The
///        field is a disc of its area. Until 0.36.15 the carting started at the
///        field's centre, 141-907 m from a road on the start's map. NO REFUSAL
///        for a field with no road near yet: the start's map has no field
///        roads; that comes with the map's access roads (boss's (a)).
Vec2 FieldHeapPoint(const WorldState& world, const FieldRow& field);

/// @brief What one carrier is worth on this field's shoulder today: a cart's
/// load at harness speed when the settlement has a draught horse to spare, a
/// person's load on foot when it has not.
/// @note The REFERENCE carrier, deliberately — a cart takes its 750 kg
///       whoever leads the horse, and on foot the spread between a strong man
///       and a frail one is paid out in trudodni rather than in tonnage.
HaulRate FieldHaulRate(const ProductionConfig& config,
                       const WorldState& world,
                       const FieldRow& field);

/// @brief Turns the hauling delivered today into grain that actually moved,
/// and re-sizes tomorrow's demand for what is still lying out.
/// @pre The day's last tick, sequential slot, after labor has run.
void SettleHauling(const ProductionConfig& config, WorldState& current);

/// @brief What one carrier is worth on a timber stand's shoulder today —
/// measured to the heap where logs already lie, else to the shared store.
HaulRate StandHaulRate(const ProductionConfig& config,
                       const WorldState& world,
                       const TimberStandRow& stand);

/// @brief The same settlement for the logs lying on the timber stands, by the
/// same rules and the same arithmetic as a field's load (timber design §8a:
/// "груз на участке, как урожай на поле").
/// @pre The day's last tick, sequential slot, after labor has run.
void SettleStandHauling(const ProductionConfig& config, WorldState& current);

/// @brief What one carrier is worth on an extraction site's shoulder today —
/// measured to the outline heap that is that resource's home by
/// resource_stores.csv (IsHomeOf), else to the shared store (the stand's rule,
/// for the stand's reason).
HaulRate SiteHaulRate(const ProductionConfig& config,
                      const WorldState& world,
                      const ExtractionSiteRow& site);

/// @brief The same settlement for what lies dug on the extraction sites:
/// "добытое — груз на участке, как урожай на поле" (boss, parcel 270).
/// @pre The day's last tick, sequential slot, after labor has run.
void SettleSiteHauling(const ProductionConfig& config, WorldState& current);

/// @brief «ПЕРЕВАЛКА» — the settlement for a store being emptied (kEmptyStore;
/// start §5; registers 214 and 233): the carrying delivered today moves that
/// share of what lies in the unit to the stores that accept it, the
/// shortest spoil_days first, then what is stolen more readily
/// (ProductionConfig::theft_rank), then row order; tomorrow's demand is
/// re-sized for what is left that has somewhere to go. A paused unit carries
/// nothing and asks for nobody; the order stands.
/// @pre The day's last tick, sequential slot, after labor has run.
void SettleStoreEmptying(const ProductionConfig& config, WorldState& current);

/// @brief The speed multiplier the haul goes at today: the DIRT bed's
///        condition (road_rules.h), on runners in the snow (sleighs, STUB).
float HaulBedFactor(const ProductionConfig& config, const WeatherState& weather);

/// @brief The bed's condition at the dawn it changes (the 0.34.3 static analysis' med finding,
/// the mud's first tail; widened from the mud to every condition in 0.36.8): tonight's haul
/// demand (the four settlements above) was priced with yesterday's bed and is worked today, so
/// when the bed's speed changes overnight every load's haul_days_remaining and haul_days_written
/// are scaled by was / now of HaulBedFactor. Both are scaled together, so the evening's share
/// done / written is unchanged.
/// @param yesterday The weather the demand was priced with (previous state).
/// @pre The day's first tick, before anything writes a new demand today.
void RescaleHaulForBeds(const ProductionConfig& config,
                        const WeatherState& yesterday,
                        WorldState& current);

/// @brief A day in the life of everything lying in a unit's store.
/// @pre Called AFTER the village has eaten: the meal is the needs slot,
///      phase 2, and this is phase 3 of the same tick. Eaten food cannot
///      rot, and the other way round the settlement starves beside a full
///      store with both halves looking correct.
void SpoilStores(const ProductionConfig& config, WorldState& current);

/// @brief A day in the life of a reaped heap waiting on its field (boss seq
/// 165): it rots as its produce does in a store, at
/// FarmingConfig::field_heap_keeping_factor of the store's keeping — 0.33, a
/// third as long — booked in `spoiled`, as a store's rot is.
/// @pre With SpoilStores, after the village has eaten.
void SpoilFieldHeaps(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_FIELD_HAUL_H_
