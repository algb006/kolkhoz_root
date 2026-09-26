// The room booked for missing seed (seed_room.h).
#include "seed_room.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/land_state.h"
#include "core_common/spoilage.h"
#include "field_haul.h"
#include "stock_ops.h"

namespace core {
namespace {

constexpr Grams kUnbounded = std::numeric_limits<Grams>::max();

/// The crop a field's next sowing puts in, when it takes seed; invalid else.
/// The crop the rotation sows next is what the player has assigned and what
/// the seed alarm is about ("an alarm at assignment, not in spring" —
/// farming design §7). Until 0.34.38 the alarm read the slot `(year + 1) %
/// 3`, over slots the year's turn SHIFTS, and named the wrong crop two years
/// in three; the one rule of "next sowing" is fund_ladder.h's.
CropId SeededNextCrop(const ProductionConfig& config,
                      const WorldState& world,
                      const FieldRow& field) {
  if (field.kind != LandKind::kArable) {
    return CropId{};
  }
  const bool year0_winter = field.rotation_year0.value < config.crops.size() &&
                            config.crops[field.rotation_year0.value].is_winter;
  const CropId next = NextSowingCrop(field, world.calendar.day, year0_winter);
  if (next.value >= config.crops.size() ||
      !(config.crops[next.value].sowing_norm_kg_per_ha > 0.0F)) {
    return CropId{};
  }
  return next;
}

/// Whether any of `resource`'s crop still stands on a field THIS YEAR —
/// being reaped with a share not yet laid in the heap, or growing toward a
/// reaping whose window has not closed this calendar year.
///
/// THIS YEAR, AND THE STATIC REVIEW FOUND WHY: a winter crop grows from its
/// autumn sowing to the next July, and counted as "standing" it booked room
/// all winter for a harvest a year away, holding October's potato and grain
/// heaps on their fields for nothing.
bool StillStanding(const ProductionConfig& config, const WorldState& world, ResourceId resource) {
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  for (const FieldRow& field : world.fields.rows) {
    if (field.crop.value >= config.crops.size()) {
      continue;
    }
    const CropDef& crop = config.crops[field.crop.value];
    if (crop.resource != resource) {
      continue;
    }
    const bool reaping = field.phase == FieldPhase::kHarvest && field.harvest_laid_share < 1.0F;
    const bool ripening = field.phase == FieldPhase::kGrowing && month <= crop.harvest_to_month;
    if (reaping || ripening) {
      return true;
    }
  }
  return false;
}

/// Game days from today to the END of the latest sowing month of the crops
/// `seed` sows — the latest the held seed is taken — counted forward over
/// the turn, since the booking holds the NEXT sowing. 0 when no crop of it
/// names a window.
std::uint32_t DaysToSowingEnd(const ProductionConfig& config,
                              const WorldState& world,
                              ResourceId seed) {
  const auto today = static_cast<std::uint32_t>(world.calendar.date.month);
  std::uint32_t latest = 0;
  for (const CropDef& crop : config.crops) {
    if (crop.resource != seed || !(crop.sowing_norm_kg_per_ha > 0.0F)) {
      continue;
    }
    // This month counts whole, as the seed fund's horizon counts it.
    const std::uint32_t months =
        ((crop.sow_to_month + kMonthsPerYear - today) % kMonthsPerYear) + 1U;
    latest = std::max(latest, months * kDaysPerMonth);
  }
  return latest;
}

/// Grams of `resource` lying reaped in the fields' heaps.
Grams LyingInHeaps(const WorldState& world, ResourceId resource) {
  Grams lying = 0;
  for (const FieldRow& field : world.fields.rows) {
    if (field.reaped_resource == resource && field.reaped_grams > 0) {
      lying += field.reaped_grams;
    }
  }
  return lying;
}

/// Free room, in grams of `seed`, of the numbered stores that take `seed`
/// and do NOT take `other` — where the seed can lie without touching the
/// other's room. Unbounded when an outline is the seed's home.
Grams RoomApartFrom(const ProductionConfig& config,
                    const WorldState& world,
                    ResourceId seed,
                    ResourceId other) {
  Grams room = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (HomeOutlineAccepts(unit, config, seed)) {
      return kUnbounded;
    }
    if (!NumberedStoreAccepts(unit, config, seed) || NumberedStoreAccepts(unit, config, other)) {
      continue;
    }
    room += GramsFitting(config.processing, seed, FreeRoomGrams(unit, config));
  }
  return room;
}

/// Free room, in grams of `other`, of the numbered stores that take BOTH —
/// the only room a booking of `seed` can ever take from `other`.
Grams RoomShared(const ProductionConfig& config,
                 const WorldState& world,
                 ResourceId seed,
                 ResourceId other) {
  Grams room = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (NumberedStoreAccepts(unit, config, seed) && NumberedStoreAccepts(unit, config, other)) {
      room += GramsFitting(config.processing, other, FreeRoomGrams(unit, config));
    }
  }
  return room;
}

/// Whether a numbered store takes any seed other than `resource` that has a
/// booking — a store the heap should fill only after the others.
bool TakesBookedSeed(const UnitRow& unit,
                     const ProductionConfig& config,
                     ResourceId resource,
                     const std::vector<Grams>& booked) {
  for (std::uint32_t index = 0; index < booked.size(); ++index) {
    const ResourceId seed = DefIdFromIndex<ResourceIdTag>(index);
    if (booked[index] > 0 && seed != resource && NumberedStoreAccepts(unit, config, seed)) {
      return true;
    }
  }
  return false;
}

}  // namespace

Grams FieldSeedNeed(const ProductionConfig& config,
                    const WorldState& world,
                    const FieldRow& field,
                    ResourceId& seed) {
  seed = ResourceId{};
  const CropId next = SeededNextCrop(config, world, field);
  if (next.value == kInvalidDefIdValue) {
    return 0;
  }
  const CropDef& crop = config.crops[next.value];
  seed = crop.resource;
  return GramsFromKilograms(crop.sowing_norm_kg_per_ha * field.area_ga);
}

std::vector<Grams> SeedNeedByResource(const ProductionConfig& config, const WorldState& world) {
  std::vector<Grams> need(config.feed_values.size(), 0);
  for (const FieldRow& field : world.fields.rows) {
    ResourceId seed;
    const Grams wanted = FieldSeedNeed(config, world, field, seed);
    if (wanted > 0 && seed.value < need.size()) {
      need[seed.value] += wanted;
    }
  }
  return need;
}

std::vector<Grams> SeedHeldToSowing(const ProductionConfig& config,
                                    const WorldState& world,
                                    SimDay as_of) {
  return SeedHeldByField(config, world, as_of).by_resource;
}

SeedHold SeedHeldByField(const ProductionConfig& config, const WorldState& world, SimDay as_of) {
  SeedHold hold;
  hold.by_resource.assign(config.feed_values.size(), 0);
  hold.by_field_row.assign(world.fields.rows.size(), 0);
  hold.seed_of_row.assign(world.fields.rows.size(), ResourceId{});
  std::vector<Grams>& held = hold.by_resource;
  const auto month = static_cast<std::int32_t>((as_of % kDaysPerYear) / kDaysPerMonth);
  constexpr auto kYear = static_cast<std::int32_t>(kMonthsPerYear);
  constexpr std::int32_t kNever = std::numeric_limits<std::int32_t>::max();

  // THE SOWINGS FIELD BY FIELD (0.36.21; boss-core-epoch1-resume [35]): each
  // arable field's next sowing, in months from `as_of` to its window's end —
  // by the slot it comes from (a winter crop of slot k is sown in the autumn
  // of year k − 1), this month counting whole as DaysToSowingEnd counts it;
  // a held chain at its crop's next window (NextSowing::held_chain). And the
  // month that sowing is reaped, which is a harvest of its seed too.
  struct Sowing {
    const CropDef* crop = nullptr;
    std::uint32_t row = 0;
    std::int32_t sow_end = 0;  // months; <= 0 — its window gone
  };

  std::vector<Sowing> sowings;
  // THE NEXT HARVEST OF EACH SEED, from `as_of`, in months — off the FIELDS,
  // not off the crop table (static review of 0.36.21): a crop in the ground
  // (nought while in its reaping months), and a sowing to come, reaped in its
  // slot's year. A seed nothing will reap holds its sowings whatever the
  // calendar says: in a year with no field of oats, next year's oat seed is
  // not "given by August's oats".
  std::vector<std::int32_t> harvest_in(held.size(), kNever);
  const auto months_to = [month](std::uint8_t target) {
    return (static_cast<std::int32_t>(target) - month + kYear) % kYear;
  };
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const FieldRow& field = world.fields.rows[row];
    if (field.kind != LandKind::kArable) {
      continue;
    }
    const bool in_ground =
        (field.phase == FieldPhase::kGrowing ||
         (field.phase == FieldPhase::kHarvest && field.harvest_laid_share < 1.0F)) &&
        field.crop.value < config.crops.size();
    if (in_ground) {
      const CropDef& standing = config.crops[field.crop.value];
      if (standing.resource.value < held.size()) {
        const bool reaping_now = month >= static_cast<std::int32_t>(standing.harvest_from_month) &&
                                 month <= static_cast<std::int32_t>(standing.harvest_to_month);
        const std::int32_t months = reaping_now ? 0 : months_to(standing.harvest_from_month);
        harvest_in[standing.resource.value] = std::min(harvest_in[standing.resource.value], months);
      }
    }
    const bool year0_winter = field.rotation_year0.value < config.crops.size() &&
                              config.crops[field.rotation_year0.value].is_winter;
    const NextSowing next = NextSowingOf(field, as_of, year0_winter);
    if (next.crop.value >= config.crops.size()) {
      continue;
    }
    const CropDef& crop = config.crops[next.crop.value];
    if (!(crop.sowing_norm_kg_per_ha > 0.0F) || crop.resource.value >= held.size()) {
      continue;
    }
    std::int32_t sow_end = 0;
    std::int32_t reaped_in = 0;
    if (next.held_chain) {
      const std::int32_t sown = months_to(crop.sow_to_month);
      const std::int32_t reaped = months_to(crop.harvest_from_month);
      sow_end = sown + 1;
      reaped_in = reaped > sown ? reaped : reaped + kYear;  // the first reaping after it
    } else {
      const std::int32_t sow_year = static_cast<std::int32_t>(next.slot) - (crop.is_winter ? 1 : 0);
      sow_end = (sow_year * kYear) + static_cast<std::int32_t>(crop.sow_to_month) - month + 1;
      reaped_in = (static_cast<std::int32_t>(next.slot) * kYear) +
                  static_cast<std::int32_t>(crop.harvest_from_month) - month;
    }
    if (sow_end > 0 && reaped_in >= 0) {
      harvest_in[crop.resource.value] = std::min(harvest_in[crop.resource.value], reaped_in);
    }
    sowings.push_back(Sowing{.crop = &crop, .row = row, .sow_end = sow_end});
  }
  // A FIELD'S SEED IS HELD when its sowing ends before the seed's next
  // harvest: otherwise that harvest gives it. The winter rye is sown in
  // September out of July's rye; holding it from January failed the canon's
  // rye 17 years of 108 (seq 5 item 8). Until 0.36.21 the rule was asked of
  // the seed as a whole, and at the turn held the potato of every chain whose
  // potato comes NEXT year out of this year's stores (econ's E2Bf, seed 1934,
  // the turn of year 7: the position 10 t short beside the idle seed).
  for (const Sowing& sowing : sowings) {
    const auto resource = sowing.crop->resource.value;
    if (sowing.sow_end <= 0 || harvest_in[resource] < sowing.sow_end) {
      continue;  // its window gone, or a harvest comes first
    }
    const Grams norm = GramsFromKilograms(sowing.crop->sowing_norm_kg_per_ha *
                                          world.fields.rows[sowing.row].area_ga);
    held[resource] += norm;
    hold.by_field_row[sowing.row] = norm;
    hold.seed_of_row[sowing.row] = sowing.crop->resource;
  }
  return hold;
}

Grams SeedNeedWithRot(const ProductionConfig& config,
                      const WorldState& world,
                      ResourceId seed,
                      Grams need) {
  if (need <= 0) {
    return need;
  }
  // The store's keeping factor is 1 everywhere (spoilage.h, STUB), so the
  // table's shelf life is the one.
  const float spoil_days =
      seed.value < config.spoil_days.size() ? config.spoil_days[seed.value] : 0.0F;
  return need + RotMarginGrams(need, spoil_days, DaysToSowingEnd(config, world, seed));
}

std::vector<Grams> SeedRoomBooked(const ProductionConfig& config, const WorldState& world) {
  // EVERY NEXT SOWING, NOT THE PLAN DOOR'S RULE (0.36.23, named to boss): the
  // booking asks for room for the seed a harvest BRINGS — the rye reaped in
  // July and kept to September — which is exactly what the door's rule does
  // not hold ("a harvest comes first, it gives it"). Read by the door's rule
  // the rye's booking is nought in its own reaping month.
  std::vector<Grams> booked = SeedNeedByResource(config, world);
  for (std::uint32_t index = 0; index < booked.size(); ++index) {
    if (booked[index] <= 0) {
      continue;
    }
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    // THE NORM AND ITS ROT to the end of the seed's sowing window: the bare
    // norm lay in the church on seed 1939 and was 2-3 t short by 1 March,
    // all of it rot (seq 26 acceptance).
    booked[index] = SeedNeedWithRot(config, world, resource, booked[index]);
    const Grams held = HeldEverywhere(world, resource);
    Grams missing = booked[index] > held ? booked[index] - held : 0;
    // Nothing standing: the booking is no larger than what can still come in.
    if (missing > 0 && !StillStanding(config, world, resource)) {
      const Grams lying = LyingInHeaps(world, resource);
      missing = missing < lying ? missing : lying;
    }
    booked[index] = missing;
  }
  return booked;
}

Grams HeapRoom(const ProductionConfig& config,
               const WorldState& world,
               ResourceId resource,
               const std::vector<Grams>& booked) {
  const Grams receivable = ReceivableRoom(config, world, resource);
  if (receivable == kUnbounded) {
    return receivable;  // an outline is its home: nobody's seed is squeezed
  }
  Grams held_back = 0;
  for (std::uint32_t index = 0; index < booked.size(); ++index) {
    const ResourceId seed = DefIdFromIndex<ResourceIdTag>(index);
    if (booked[index] <= 0 || seed == resource) {
      continue;
    }
    // What the seed cannot lay apart, and never more than the room the two
    // share: a granary the potato cannot enter is not held for the potato
    // (static review, H1).
    const Grams apart = RoomApartFrom(config, world, seed, resource);
    if (apart < booked[index]) {
      const Grams spill = booked[index] - apart;
      const Grams shared = RoomShared(config, world, seed, resource);
      held_back += spill < shared ? spill : shared;
    }
  }
  const Grams left = receivable > held_back ? receivable - held_back : 0;
  // A HEAP'S OWN SEED IS NEVER HELD BACK by another seed's booking (static
  // review, H2): two short seeds sharing one church would otherwise each
  // hold the other's heap, and the snow would take both. Its own share goes
  // through; between two seeds the door serves whoever is carted first.
  const std::uint32_t own = resource.value;
  const Grams own_seed = own < booked.size() ? booked[own] : 0;
  const Grams own_room = own_seed < receivable ? own_seed : receivable;
  return left > own_room ? left : own_room;
}

Grams DeliverHeapToStores(WorldState& world,
                          const ProductionConfig& config,
                          ResourceId resource,
                          Grams amount,
                          const std::vector<Grams>& booked) {
  if (amount <= 0) {
    return 0;
  }
  Grams placed = 0;
  // First the stores no booked seed can use: the granary before the church.
  for (UnitRow& unit : world.units.rows) {
    if (placed >= amount) {
      break;
    }
    if (!NumberedStoreAccepts(unit, config, resource) ||
        TakesBookedSeed(unit, config, resource, booked)) {
      continue;
    }
    const Grams room = GramsFitting(config.processing, resource, FreeRoomGrams(unit, config));
    if (room <= 0) {
      continue;
    }
    const Grams left = amount - placed;
    placed += AddToStock(unit.stock, resource, room < left ? room : left);
  }
  // Then the door as always, shared stores and the outline home included.
  return placed + DeliverToStores(world, config, resource, amount - placed);
}

void CollectSeedRoomAlarms(const ProductionConfig& config,
                           const WorldState& world,
                           std::vector<Alarm>& alarms) {
  const std::vector<Grams> booked = SeedRoomBooked(config, world);
  for (std::uint32_t index = 0; index < booked.size(); ++index) {
    if (booked[index] <= 0) {
      continue;
    }
    const ResourceId seed = DefIdFromIndex<ResourceIdTag>(index);
    bool holds_a_heap = false;
    for (const FieldRow& field : world.fields.rows) {
      const ResourceId other = field.reaped_resource;
      if (field.reaped_grams <= 0 || other == seed || other.value == kInvalidDefIdValue) {
        continue;
      }
      // This seed's part of the other heap's squeeze, and the heap bigger
      // than the room the bookings leave it.
      // The booking must actually take room from it — not full stores alone
      // (static review, L8).
      const Grams room = HeapRoom(config, world, other, booked);
      if (RoomApartFrom(config, world, seed, other) < booked[index] &&
          room < ReceivableRoom(config, world, other) && field.reaped_grams > room) {
        holds_a_heap = true;
        break;
      }
    }
    if (!holds_a_heap) {
      continue;
    }
    Alarm alarm;
    alarm.kind = AlarmKind::kSeedHasNoRoom;
    alarm.resource = seed;
    alarm.amount = booked[index];
    alarms.push_back(alarm);
  }
}

}  // namespace core
