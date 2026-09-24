/// @file
/// @brief What happens to ONE FIELD: the phase it is in, the work that phase
///        asks for, the sowing, the cut and the harvest.
/// @threading SINGLE_THREADED
/// Called from the production decisions sub-step (slot 3) alone, on the sim
/// thread, outside any parallel phase. Every function here MOVES the world,
/// so none of them may be called from a worker.
///
/// THE SEAM IS THE SUBJECT, NOT THE LINE COUNT. The other half of
/// production_system.cpp moves the HOUSEHOLD: the year's plan and its
/// delivery, the order book, the manure the settlement has to spread, the
/// walk that visits every field. This half moves ONE field and knows nothing
/// about the rest — which is why the dependency runs one way only, from the
/// household to the field, and never back.
///
/// The old file had grown to 997 lines against a hard limit of 1000, and the
/// limit names the illness while saying nothing about the cure: a cut by line
/// number would have put the sowing and the harvest in different files.
///
/// One rule is worth stating because it is invisible in the signatures: a
/// field row is handed around by REFERENCE, and the id is re-derived from the
/// row when an event needs one (FieldIdOf). Threading an id through a dozen
/// helpers to say what the row already knows is the shape that lets the two
/// drift apart.

#ifndef CORE_PRODUCTION_FIELD_WORK_H_
#define CORE_PRODUCTION_FIELD_WORK_H_

#include <cstdint>

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief kIdle for a phase that needs no work, the phase itself for the
/// four working ones.
constexpr FieldPhase KindOfWorkingPhase(FieldPhase phase) {
  switch (phase) {
    case FieldPhase::kPlowing:
    case FieldPhase::kHarrowing:
    case FieldPhase::kSowing:
    case FieldPhase::kHarvest:
      return phase;
    case FieldPhase::kIdle:
    case FieldPhase::kGrowing:
    // Not a phase: handled beside the phases that need no work, so this
    // switch keeps no default and a new phase stays a compile error.
    case FieldPhase::kFieldPhaseCount:
      return FieldPhase::kIdle;
  }
  return FieldPhase::kIdle;
}

/// @brief Clears the growing spell a field has been through, so the next
///        one starts from nothing.
///
/// Wipe the weather a field has been through: both accumulators, both
/// run counters and the judgement made off them. Kept as one function
/// because there are four places that end a growing spell (a perennial
/// plan change, a field lost to snow, a fresh sowing, a finished harvest)
/// and five fields to clear — four copies of five lines is how one of them
/// eventually keeps a stale "kSoaking" on a field that is bare.
void ClearFieldWeather(FieldRow& field);

/// @brief The id of a field row, from the row itself.
///
/// The field loops of this file hand a REFERENCE around — that is how they
/// were written, and threading an id through a dozen helpers to say one
/// sentence in the journal would be a larger change than the sentence. The
/// rows live in a vector, so the reference names its own index.
///
/// PRECONDITION, and the only one: `field` is a row of `current.fields`, not
/// a copy of one. Every caller here is inside a loop over those rows; the
/// assert catches the day somebody passes a temporary.
FieldId FieldIdOf(const WorldState& current, const FieldRow& field);

/// @brief Moves a field into a phase AND says so.
///
/// One function because there are ten places that move a phase, and ten
/// copies of "set it, then announce it" is exactly how eighteen event kinds
/// came to have no emitter at all (boss, 2026-09-05). The announcement
/// carries the NEW phase in `amount`, as the kind's contract says.
void MoveFieldPhase(WorldState& current, FieldRow& field, FieldPhase phase);

/// @brief Moves the field into a working phase and sizes its demand:
/// area x the phase's norm. The crop is the one in the ground or, while
/// the field is still being prepared, the one this year's rotation plans.
void OpenPhase(const ProductionConfig& config,
               WorldState& current,
               FieldRow& field,
               FieldPhase phase);

/// @brief The game man-days a whole phase of this field asks today: area x
/// the phase's norm, horse work lengthened by the traction ration. What
/// OpenPhase writes; the MTS column scales it by the hectares it leaves
/// (mts_column.cpp).
float PhaseWorkDays(const ProductionConfig& config,
                    const WorldState& current,
                    const FieldRow& field,
                    FieldPhase phase);

/// @brief How much of a fed horse's pull a horse on this traction ration
/// has, 0..1 — the divisor PhaseWorkDays lengthens horse work by. One home,
/// because the rescale below must undo exactly what the opening priced.
/// @return 1 when the rule is off (`traction_hungry_factor` 0).
float TractionFactor(const ProductionConfig& config, float traction_ration);

/// @brief Re-prices the horse work still owed on the arable when the
/// traction ration moved: remaining x factor(was) / factor(now).
///
/// WHY. A phase is sized once, when it opens, by the ration of that tick, and
/// the spring's ploughing opens on its first day at the day's turn — before
/// the team has worked a day, on the ration of last autumn. host measured it
/// (econ-host-fodder-and-winter seq 2): the base paid hungry ploughing, 114.29
/// man-days against 80, in 6 seeds of 7 with a full oat store, and the
/// winter's fodder decision never reached the spring's biggest work. The
/// shape is RescaleHaulForMud's: priced at `was`, worked at `now`.
/// @param traction_ration_was The ration before today's herd day wrote it.
void RescaleHorseWorkForRation(const ProductionConfig& config,
                               float traction_ration_was,
                               WorldState& current);

/// @brief The manure bonus this field has coming, by the share of its dose
/// it received (FieldRow::manure_applied is that share in percent).
float ManureBonus(const ProductionConfig& config, const FieldRow& field);

/// @brief Opens the meadow's cut when its month comes round; does nothing
///        on every other day of the year.
///
/// The meadow's whole year: it stands, and once a season the scythes go
/// out. No sowing window, no temperature gate, no snow loss (grass winters
/// where it grew), no fertility — a meadow is land, not a crop
/// (land_state.h, LandKind; boss answer Q6).
void RunMeadow(const ProductionConfig& config,
               WorldState& current,
               FieldRow& field,
               std::uint8_t month);

/// @brief Opens the ploughing for NEXT year's slot when the autumn window
///        and the temperature both allow it.
///
/// The autumn sowing (defect D12). A winter crop is harvested the summer
/// AFTER it is sown, so the slot it belongs to is next year's — "winter rye
/// goes into the ground in the autumn of the same year, and the ring starts
/// turning in the second" (start canon §8). Sown from this year's slot it
/// arrived a year late and ate the following spring as well.
void TrySowWinter(const ProductionConfig& config,
                  WorldState& current,
                  FieldRow& field,
                  std::uint8_t month,
                  float temperature);

/// @brief How long this crop takes to ripen, in game days.
///
/// THE NUMBER IS ALREADY CHOSEN, AND IT IS CHOSEN TWICE OVER BY THE WINDOWS:
/// from the LAST day the crop may be sown to the FIRST day it may be reaped.
/// Oats 13, spring wheat and barley 9, potato 13, cabbage 17 — boss's decision
/// of 2026-09-13.
///
/// WHY NOT A COLUMN IN crops.csv. Real growing seasons give figures of the
/// same order but in places LARGER than this gap, and a larger one would mean
/// a sowing on the last legal day never ripens — a new number contradicting
/// windows that have already been balanced. The gap does not invent the
/// duration; it NAMES what the windows already rest on.
///
/// @return 0 for a crop that is not reaped in the year it is sown — a winter
///         crop or a perennial — where the gap runs backwards through the year
///         and means nothing. Callers treat 0 as "ripening does not gate this".
std::int32_t RipenDays(const ProductionConfig& config, CropId crop);

/// @brief What this field gives of `crop` if it is reaped now: the table's
/// yield × area × fertility against neutral × the weather's capped stress ×
/// the late-sowing slope. THE ONE ESTIMATE, used by the harvest and by the
/// snow that takes the field unreaped — a loss booked by a second formula
/// would be a loss the harvest could not have given.
Grams FieldYieldGrams(const ProductionConfig& config, const FieldRow& field, const CropDef& crop);

/// @brief The part of the field's yield still on the stalk: FieldYieldGrams
///        less the share this reaping has already laid into the heap
///        (FieldRow::harvest_laid_share). The whole yield before the reaping.
Grams StandingYieldGrams(const ProductionConfig& config,
                         const FieldRow& field,
                         const CropDef& crop);

/// @brief THE HARVEST BY PARTS (farming design §6, 24 September 2026): lays
///        into the heap at the field's edge the share of the yield the
///        reaping's labour has cut since the last lay —
///        `yield × (1 − work left / phase work − laid share)` — with its book
///        (ledger harvest, area_harvested_ha by the share), its straw into
///        the stores, and the carting's price grown by the same load. Does
///        nothing on a field not being reaped, a meadow, or a share already
///        laid. Called at the day's turn and before a reaping closes or is
///        lost, so whoever drained the work — crew, column, avral — is paid.
void LayReapedShare(const ProductionConfig& config, WorldState& current, FieldRow& field);

/// @brief The snow takes what still STANDS of an unreaped annual (farming
/// design §6): the share the reaping has cut is laid into the heap first
/// (LayReapedShare), the rest — StandingYieldGrams — goes to lost_to_snow,
/// its hectares to area_lost_ha; the field is left idle with its crop
/// cleared, and kFieldLost says field, resource and grams. The HEAP IS NOT
/// TOUCHED: lying snow takes it, the first snowfall does not (the two
/// thresholds, production_system.cpp).
/// @pre The caller has decided it is snowing in `crop`'s reaping season and
///      the crop is neither a winter crop nor a perennial.
void LoseFieldToSnow(const ProductionConfig& config,
                     WorldState& current,
                     FieldRow& field,
                     const CropDef& crop);

/// @brief Whether the crop standing on this field has ripened by `day`.
///
/// True for a stand that carries no sowing day (kNeverSownDay): a crop whose
/// sowing this core never saw — genesis stands, worlds from older saves — has
/// stood longer than any ripening. True whenever RipenDays is 0 — a winter
/// crop and a perennial are ruled by their windows alone, as they always were.
bool CropHasRipened(const ProductionConfig& config, const FieldRow& field, SimDay day);

/// @brief At the year's turn, lets go of a field still being prepared for the
///        crop of the year that has just ended.
///
/// A field ploughed on the thaw waits, harrowed, for its crop's sowing to
/// open; when the harrow finished too late for that crop to ripen, the sowing
/// never opens, and until 2026-09-13 the field carried that crop through the
/// turn and sowed it next spring IN THE NEXT SLOT's place. Measured on
/// oat_balance: cabbage harrowed on day 26 of the first year stood harrowed
/// all year and went into the oat slot of the second, so oats were never sown
/// again and the run had nothing to balance for fifteen years.
///
/// The crop is dropped and the field is idle. Ploughing that WAS finished is
/// kept as autumn ploughing (FieldRow::autumn_plowed): the furrow is in the
/// ground whichever crop it was turned for. A fallow being ploughed is left
/// alone — it carries no crop to go stale.
/// @return true when the field was released.
bool ReleaseUnsownPreparation(WorldState& current, FieldRow& field);

/// @brief Whether the crop standing on this field may be opened for reaping
///        in `month` of the day `day`.
///
/// From `harvest_from_month` on, and ripe (CropHasRipened). A winter or
/// perennial crop also stops at `harvest_to_month`; a same-year annual does
/// not — ripened past its window it stands and may be reaped until the snow
/// takes it (farming design, "за окном уборки, вызревание ДО СНЕГА: хлеб
/// стоит, косят поздно"). That was defect UB-001 until 2026-09-13.
bool ReapingMayOpen(const ProductionConfig& config,
                    const FieldRow& field,
                    std::uint8_t month,
                    SimDay day);

/// @brief Whether a crop's seed may START going into the ground today: its
///        window has OPENED and the ground is warm enough for it.
///
/// THE FRONT EDGE ONLY, and `sow_to_month` is deliberately not asked. The back
/// edge belongs to the crew: a sowing that began inside its window finishes
/// when the hands finish it, and a field that reached the harrow late is sown
/// late rather than not at all. Closing this on the back edge too was measured
/// on 2026-09-13 and costs the canonical village its first harvest — 10.5
/// hectares sown instead of 66.5 — and puts it on trial in the third year.
///
/// ONE HOME FOR THE CONDITION, with one asker today (AdvanceFinishedPhases,
/// where a harrowed field moves into the sowing) and a second expected the day
/// a late sowing starts costing yield instead of nothing. A test spelt out
/// twice is how one of the two eventually forgets the temperature.
///
/// @param crop The crop being sown; an invalid id is a bare fallow and never
///        sows, so this answers false for it.
/// @param day_of_year Today within the year, 0..kDaysPerYear-1. The back edge
///        is measured in DAYS and not months: "sown today + RipenDays" has to
///        land on or before `growing_season_last_day` — the snow, not the
///        reaping window — and a month is too coarse to say so.
bool SowingMayOpen(const ProductionConfig& config,
                   CropId crop,
                   std::uint8_t month,
                   std::uint32_t day_of_year,
                   float temperature);

/// @brief Opens the ploughing for this year's slot as soon as the GROUND can
///        be worked — not when the sowing window opens.
///
/// THE TWO CONDITIONS ARE SEPARATE, and they were one until 2026-09-13.
/// "Пахать можно, как только земля открыта. Сеять — только в свой
/// агрономический срок" (boss's decision): a sowing window is an agronomic
/// fact about SEED, and using it to gate the plough made it a gate on field
/// work as such. Oats name one month — four game days — and ploughing,
/// harrowing and sowing together are 2.29 game man-days a hectare, so the
/// spring could not fit however many hands the village had. Measured before
/// the repair: the sowing overran its window in ALL TWELVE years walked, the
/// twelfth with 247 able-bodied residents and 46 horses, and 49.5 hectares
/// stood untouched through the first twelve days of that year
/// (tests/run/sowing_window).
///
/// So the field enters the plough on the thaw and waits, harrowed, for its own
/// window; the sowing's condition is asked where the sowing opens.
void TrySow(const ProductionConfig& config,
            WorldState& current,
            FieldRow& field,
            std::uint8_t month,
            float temperature);

/// @brief Closes a worked-through sowing phase: the seed is in the ground
///        and the field starts growing.
///
/// The seed goes into the ground when the sowing phase is worked through,
/// AS FAR AS IT GOES (farming design §7; 0.34.50): FieldRow::sown_share is the
/// seed taken over the whole field's norm, and the crop grows on that share
/// (FieldYieldGrams). A crop that takes no seed sows the whole field.
void FinishSowing(const ProductionConfig& config, WorldState& current, FieldRow& field);

/// @brief Closes a worked-through harvest phase: arable lays the last of
///        its share into the field buffer (LayReapedShare) and closes its
///        books — fertility, rotation, kFieldHarvested for the whole reaping
///        — a meadow pays into the manger and the stores.
void FinishHarvest(const ProductionConfig& config, WorldState& current, FieldRow& field);

/// @brief A field whose working phase is drained moves on: ploughing opens
///        the harrowing, the harrowing the sowing (in its agronomic term) or a
///        bare fallow's end, the sowing and the reaping finish. A field still
///        owed work, or idle, is left alone. Called for every field each tick
///        (production_system.cpp) and by the MTS column for the field it has
///        finished (mts_column.cpp).
void AdvanceFinishedField(const ProductionConfig& config, WorldState& current, FieldRow& field);

}  // namespace core

#endif  // CORE_PRODUCTION_FIELD_WORK_H_
