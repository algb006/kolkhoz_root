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
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    ResidentRow& resident = current.residents.rows[row];
    const ResidentId id = current.residents.row_ids[row];
    // Anybody with a reason already — waiting for a car, or away — is not
    // sent for again.
    if (!(resident.health < config.health_line) ||
        resident.away_reason != static_cast<std::uint8_t>(AwayReason::kNone) ||
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
    // FROM THE SENDING TO THE CAR HE LIES AT HOME and takes no work (boss,
    // boss-core-epoch1-2 seq 1, answer 3): no working hours for the car to
    // cut, so none go unpaid. This is the day's turn, after labor's hour-0
    // placement and before sunrise: the placement he got is taken back
    // before a minute of it is worked.
    resident.away_reason = static_cast<std::uint8_t>(AwayReason::kAwaitingAmbulance);
    resident.work = WorkAssignment{};
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
    // Waiting for the car is not away: there is no term to end (its until is
    // nought, and read as a term it would bring him "home" the tick he was
    // sent for).
    if (resident.away_reason == static_cast<std::uint8_t>(AwayReason::kNone) ||
        resident.away_reason == static_cast<std::uint8_t>(AwayReason::kAwaitingAmbulance) ||
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

namespace {

/// THE WAIT AND THE CAR SAY THE SAME THING, every tick. They are set in one
/// move and only a loaded world can part them: a 0.34.18 save has a car on
/// the road for a resident with no reason (he would work until it came), and
/// a hand-edited or damaged one could carry the waiting reason with no car
/// (he would take no work for ever, and never be sent for again).
void ReconcileTheWait(WorldState& current) {
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    ResidentRow& resident = current.residents.rows[row];
    const bool car = CarComingFor(current, current.residents.row_ids[row]);
    const auto reason = static_cast<AwayReason>(resident.away_reason);
    if (car && reason == AwayReason::kNone) {
      resident.away_reason = static_cast<std::uint8_t>(AwayReason::kAwaitingAmbulance);
      resident.work = WorkAssignment{};
    } else if (!car && reason == AwayReason::kAwaitingAmbulance) {
      resident.away_reason = static_cast<std::uint8_t>(AwayReason::kNone);
    }
  }
}

}  // namespace

void RunDistrictCars(const ProductionConfig& config, WorldState& current) {
  ReconcileTheWait(current);
  if (HourFromTick(current.calendar.tick) == 0) {
    SendAndSetOut(config.district_car, current);
  }
  RunCarsOnTheirErrands(config.district_car, current);
  BringThemHome(config, current);
}

}  // namespace core
