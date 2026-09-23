// The district's ambulance (district_car.h).

#include "district_car.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/district_car_state.h"
#include "core_common/emit_event.h"
#include "core_common/state_table_ops.h"
#include "milk_cart.h"

namespace core {
namespace {

std::uint32_t HourOf(float hours) {
  const float clamped = hours < 0.0F ? 0.0F : hours;
  return static_cast<std::uint32_t>(std::lround(clamped));
}

/// Sets the car on the road: the NEXT morning's hour, a day later in the mud
/// (boss seq 210, 2). A blizzard holds it (the caller checks).
void SetOut(const DistrictCarConfig& config, const WorldState& current, DistrictCarRow& car) {
  const SimDay days = current.weather.mud ? 2U : 1U;
  car.phase = DistrictCarPhase::kOnTheRoad;
  car.arrive_tick = TickOfDayHour(current.calendar.day + days, HourOf(config.arrive_hour));
}

bool CarComingFor(const WorldState& current, ResidentId resident) {
  for (const DistrictCarRow& car : current.district_cars.rows) {
    if (car.resident.value == resident.value) {
      return true;
    }
  }
  return false;
}

void Emit(WorldState& current,
          EventKind kind,
          const ResidentRow& resident,
          ResidentId id,
          std::int64_t amount) {
  SimEvent& event = EmitEvent(current, kind, EventSeverity::kNotable);
  event.resident = id;
  event.family = resident.family;
  event.amount = amount;
}

/// The day's first tick: send for the grave, and set out what the blizzard
/// held yesterday.
void SendAndSetOut(const DistrictCarConfig& config, WorldState& current) {
  const bool blizzard = current.weather.phenomenon == WeatherPhenomenon::kBlizzard;
  for (DistrictCarRow& car : current.district_cars.rows) {
    if (car.phase == DistrictCarPhase::kWaiting && !blizzard) {
      SetOut(config, current, car);
    }
  }
  const Tick now = current.calendar.tick;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& resident = current.residents.rows[row];
    const ResidentId id = current.residents.row_ids[row];
    if (!(resident.health < config.health_line) || AwayInDistrict(resident, now) ||
        CarComingFor(current, id)) {
      continue;
    }
    DistrictCarRow car;
    car.kind = DistrictCarKind::kAmbulance;
    car.resident = id;
    if (!blizzard) {
      SetOut(config, current, car);
    }
    const Tick arrive = car.arrive_tick;
    AppendRow(current.district_cars, car);
    Emit(current, EventKind::kAmbulanceSent, resident, id, static_cast<std::int64_t>(arrive));
  }
}

/// A car whose hour has come stands at the house and takes the patient; an
/// hour later it is gone. A patient no longer in the world takes the car's
/// errand with him.
void RunCarsOnTheirErrands(const DistrictCarConfig& config, WorldState& current) {
  const Tick now = current.calendar.tick;
  std::vector<DistrictCarId> done;
  for (std::uint32_t index = 0; index < current.district_cars.rows.size(); ++index) {
    DistrictCarRow& car = current.district_cars.rows[index];
    const std::uint32_t row = FindRow(current.residents, car.resident);
    if (row == kNoRow) {
      done.push_back(current.district_cars.row_ids[index]);
      continue;
    }
    if (car.phase == DistrictCarPhase::kOnTheRoad && now >= car.arrive_tick) {
      car.phase = DistrictCarPhase::kAtTheYard;
      car.leave_tick = now + 1;
      ResidentRow& patient = current.residents.rows[row];
      const Tick back =
          now +
          static_cast<Tick>(std::lround(config.hospital_days * static_cast<float>(kTicksPerDay)));
      patient.away_until_day = SimDayFromTick(back);
      patient.away_until_hour = static_cast<std::uint8_t>(HourFromTick(back));
      patient.away_walk_hours = 0;
      patient.away_reason = static_cast<std::uint8_t>(AwayReason::kHospital);
      patient.work = WorkAssignment{};  // carried out: he works nowhere from this hour
      Emit(current,
           EventKind::kAmbulanceAtHouse,
           patient,
           car.resident,
           static_cast<std::int64_t>(patient.away_until_day));
    } else if (car.phase == DistrictCarPhase::kAtTheYard && now >= car.leave_tick) {
      done.push_back(current.district_cars.row_ids[index]);
    }
  }
  for (const DistrictCarId id : done) {
    RemoveRow(current.district_cars, id);
  }
}

/// A term that is over: home with the milk cart in its season, else the walk
/// in from the border first — the last hours of the absence, drawn on the
/// road (away_in_district.h, WalkingHomeFromDistrict).
void BringThemHome(const ProductionConfig& config, WorldState& current) {
  const Tick now = current.calendar.tick;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    ResidentRow& resident = current.residents.rows[row];
    if (resident.away_reason == static_cast<std::uint8_t>(AwayReason::kNone) ||
        now < AwayUntilTick(resident)) {
      continue;
    }
    const std::uint32_t walk = HourOf(config.district_car.walk_home_hours);
    if (resident.away_walk_hours == 0 && walk > 0 && !MilkPositionStands(config, current)) {
      const Tick back = now + walk;
      resident.away_until_day = SimDayFromTick(back);
      resident.away_until_hour = static_cast<std::uint8_t>(HourFromTick(back));
      resident.away_walk_hours = static_cast<std::uint8_t>(walk);
      continue;
    }
    const auto reason = static_cast<std::int64_t>(resident.away_reason);
    resident.away_reason = static_cast<std::uint8_t>(AwayReason::kNone);
    resident.away_until_day = 0;
    resident.away_until_hour = 0;
    resident.away_walk_hours = 0;
    resident.health = config.district_car.return_health;
    Emit(current, EventKind::kBackFromDistrict, resident, current.residents.row_ids[row], reason);
  }
}

}  // namespace

void RunDistrictCars(const ProductionConfig& config, WorldState& current) {
  if (HourFromTick(current.calendar.tick) == 0) {
    SendAndSetOut(config.district_car, current);
  }
  RunCarsOnTheirErrands(config.district_car, current);
  BringThemHome(config, current);
}

}  // namespace core
