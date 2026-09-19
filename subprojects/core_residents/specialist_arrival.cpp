// The district's specialists of Epoch I (specialist_arrival.h).

#include "specialist_arrival.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "core_common/body.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"

namespace core {
namespace {

/// The age band the district sends, biological years (education design,
/// "Эпоха I числами": «возраст 20–30»).
constexpr float kSpecialistAgeFrom = 20.0F;
constexpr float kSpecialistAgeTo = 30.0F;

bool IsHousing(const LifeConfig& config, UnitTypeId type) {
  return type.value < config.definitions.units.is_housing.size() &&
         config.definitions.units.is_housing[type.value] != 0;
}

/// Rows of empty housing that stands: the houses held for a specialist first
/// (kReserveHouse; boss seq 191, 197 — the specialist takes the house kept for
/// him), then the rest, each in row order.
std::vector<std::uint32_t> FreeHouses(const LifeConfig& config, const WorldState& current) {
  std::vector<std::uint32_t> free;
  for (const std::uint8_t reserved : {std::uint8_t{1}, std::uint8_t{0}}) {
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      const UnitRow& unit = current.units.rows[row];
      if (unit.household.value == kInvalidEntityIdValue && unit.level > 0 &&
          unit.reserved_for_specialist == reserved && IsHousing(config, unit.type)) {
        free.push_back(row);
      }
    }
  }
  return free;
}

/// Residents holding `post` at a unit of `type`, and those on the road for it.
std::uint32_t PostsHeldAndComing(const WorldState& current, UnitTypeId type, ProfessionId post) {
  std::uint32_t count = 0;
  for (const ResidentRow& resident : current.residents.rows) {
    if (resident.post.profession.value != post.value) {
      continue;
    }
    const std::uint32_t unit_row = FindRow(current.units, resident.post.unit);
    count += unit_row != kNoRow && current.units.rows[unit_row].type.value == type.value ? 1U : 0U;
  }
  for (const SpecialistArrivalRow& arrival : current.specialist_arrivals.rows) {
    count += arrival.profession.value == post.value ? 1U : 0U;
  }
  return count;
}

/// Whether `unit` has `post` held there or coming to it.
bool UnitHasPost(const WorldState& current, UnitId unit, ProfessionId post) {
  for (const ResidentRow& resident : current.residents.rows) {
    if (resident.post.profession.value == post.value && resident.post.unit.value == unit.value) {
      return true;
    }
  }
  for (const SpecialistArrivalRow& arrival : current.specialist_arrivals.rows) {
    if (arrival.profession.value == post.value && arrival.unit.value == unit.value) {
      return true;
    }
  }
  return false;
}

std::uint32_t JuniorPupils(const LifeConfig& config, const WorldState& current) {
  std::uint32_t pupils = 0;
  for (const ResidentRow& resident : current.residents.rows) {
    const float age =
        BiologicalAgeYears(config.life_speedup, resident.birth_day, current.calendar.day);
    pupils += age >= config.body.age_school_junior_from_years &&
                      age < config.body.age_school_senior_from_years
                  ? 1U
                  : 0U;
  }
  return pupils;
}

/// Sends one specialist for `post` to `unit` when a free house is left over
/// the ones already promised; says so when not.
void SendOrRefuse(const LifeConfig& config,
                  WorldState& current,
                  UnitId unit,
                  ProfessionId post,
                  std::size_t free_houses) {
  if (free_houses <= current.specialist_arrivals.rows.size()) {
    SimEvent& refused =
        EmitEvent(current, EventKind::kSpecialistNoHousing, EventSeverity::kNotable);
    refused.unit = unit;
    refused.amount = post.value;
    return;
  }
  SpecialistArrivalRow arrival;
  arrival.profession = post;
  arrival.unit = unit;
  // In the mud the cart takes longer, as the lot's does (LimitBaseDeliveryDays).
  const float days = current.weather.mud
                         ? config.specialist_delivery_days / config.specialist_mud_speed_factor
                         : config.specialist_delivery_days;
  arrival.arrive_day = static_cast<std::uint32_t>(current.calendar.day) +
                       static_cast<std::uint32_t>(std::lround(days));
  AppendRow(current.specialist_arrivals, arrival);
}

void DecideWhomToSend(const LifeConfig& config, WorldState& current) {
  const std::size_t free_houses = FreeHouses(config, current).size();
  // TEACHERS: the village's norm against the teachers it has and awaits.
  if (config.school_type.value != kInvalidDefIdValue &&
      config.teacher_post.value != kInvalidDefIdValue && config.teacher_pupils_per_teacher > 0.0F) {
    const std::uint32_t pupils = JuniorPupils(config, current);
    const auto needed = static_cast<std::uint32_t>(
        std::ceil(static_cast<float>(pupils) / config.teacher_pupils_per_teacher));
    if (PostsHeldAndComing(current, config.school_type, config.teacher_post) < needed) {
      for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
        const UnitRow& unit = current.units.rows[row];
        if (unit.type.value == config.school_type.value && unit.level > 0) {
          SendOrRefuse(
              config, current, current.units.row_ids[row], config.teacher_post, free_houses);
          break;
        }
      }
    }
  }
  // LIBRARIANS: one to every standing reading hut that has none.
  if (config.reading_hut_type.value != kInvalidDefIdValue &&
      config.librarian_post.value != kInvalidDefIdValue) {
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      const UnitRow& unit = current.units.rows[row];
      const UnitId id = current.units.row_ids[row];
      if (unit.type.value == config.reading_hut_type.value && unit.level > 0 &&
          !UnitHasPost(current, id, config.librarian_post)) {
        SendOrRefuse(config, current, id, config.librarian_post, free_houses);
      }
    }
  }
}

/// The specialist himself, in the house at `house_row`.
void Arrive(const LifeConfig& config,
            WorldState& current,
            const SpecialistArrivalRow& arrival,
            std::uint32_t house_row) {
  FamilyRow household;
  household.house = current.units.row_ids[house_row];
  const FamilyId family = AppendRow(current.families, household);
  current.units.rows[house_row].household = family;

  ResidentRow specialist;
  specialist.family = family;
  specialist.sex = NextRandomUnitFloat(current.rng) < 0.5F ? Sex::kMale : Sex::kFemale;
  RollBody(current.world_seed, current.residents.next_id_value, config.body, specialist);
  const float age = kSpecialistAgeFrom +
                    (NextRandomUnitFloat(current.rng) * (kSpecialistAgeTo - kSpecialistAgeFrom));
  specialist.birth_day =
      static_cast<std::int32_t>(current.calendar.day) -
      static_cast<std::int32_t>(age / config.life_speedup * static_cast<float>(kDaysPerYear));
  specialist.education_stage = EducationStage::kVocational;
  specialist.post.profession = arrival.profession;
  specialist.post.unit = FindRow(current.units, arrival.unit) != kNoRow ? arrival.unit : UnitId{};
  if (specialist.post.unit.value == kInvalidEntityIdValue) {
    specialist.post.profession = ProfessionId{};  // the two are set and cleared together
  }
  const ResidentId id = AppendRow(current.residents, specialist);

  SimEvent& arrived = EmitEvent(current, EventKind::kSpecialistArrived, EventSeverity::kNotable);
  arrived.resident = id;
  arrived.family = family;
  arrived.unit = arrival.unit;
  arrived.amount = arrival.profession.value;
  // "Приехавший по путёвке обязательно комсомолец" (district design §3; boss,
  // parcel 334): up to the komsomol's age he comes a member.
  if (age <= config.membership.specialist_komsomol_age_max_years) {
    const std::uint32_t row = FindRow(current.residents, id);
    current.residents.rows[row].social_status = SocialStatus::kKomsomol;
    SimEvent& joined = EmitEvent(current, EventKind::kSocialStatusChanged, EventSeverity::kRoutine);
    joined.resident = id;
    joined.family = family;
    joined.amount = static_cast<std::int64_t>(SocialStatus::kKomsomol);
  }
}

void ArriveThoseDue(const LifeConfig& config, WorldState& current) {
  std::vector<SpecialistArrivalId> arrived;
  for (std::uint32_t row = 0; row < current.specialist_arrivals.rows.size(); ++row) {
    const SpecialistArrivalRow arrival = current.specialist_arrivals.rows[row];
    if (arrival.arrive_day > static_cast<std::uint32_t>(current.calendar.day)) {
      continue;
    }
    const std::vector<std::uint32_t> free = FreeHouses(config, current);
    if (free.empty()) {
      continue;  // the house was taken on the road; he waits for tomorrow
    }
    Arrive(config, current, arrival, free.front());
    // He is in: the house is his, and no longer held (boss seq 191).
    current.units.rows[free.front()].reserved_for_specialist = 0;
    arrived.push_back(current.specialist_arrivals.row_ids[row]);
  }
  for (const SpecialistArrivalId id : arrived) {
    RemoveRow(current.specialist_arrivals, id);
  }
}

}  // namespace

void RunSpecialistArrivals(const LifeConfig& config, WorldState& current) {
  ArriveThoseDue(config, current);
  if (current.epoch == Epoch::kOne && current.calendar.date.day_in_month == 0) {
    DecideWhomToSend(config, current);
  }
}

}  // namespace core
