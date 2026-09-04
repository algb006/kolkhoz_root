// Housing for new and roofless households (housing.h).

#include "housing.h"

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/plot.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"

namespace core {
namespace {

/// Where a family lives, or false when it has no house standing.
bool FamilyHousePosition(const WorldState& current, FamilyId family, Vec2& position) {
  const std::uint32_t family_row =
      family.value == kInvalidEntityIdValue ? kNoRow : FindRow(current.families, family);
  if (family_row == kNoRow) {
    return false;
  }
  const UnitId house = current.families.rows[family_row].house;
  const std::uint32_t house_row =
      house.value == kInvalidEntityIdValue ? kNoRow : FindRow(current.units, house);
  if (house_row == kNoRow) {
    return false;
  }
  position = current.units.rows[house_row].position;
  return true;
}

/// The plot rules as core_common wants them, built from the config each
/// time rather than stored: a span into a member is a member's lifetime
/// written down twice.
PlotRules PlotRulesOf(const LifeConfig& config) {
  return PlotRules{.radius_by_type = config.definitions.units.plot_radius_m,
                   .map_side_m = config.definitions.map_side_m};
}

/// The plot radius of the type the STUB raises; zero when the tables know
/// no radius for it, and then the house takes the spot it wanted.
float HouseRadius(const LifeConfig& config) {
  return config.house_type.value < config.definitions.units.plot_radius_m.size()
             ? config.definitions.units.plot_radius_m[config.house_type.value]
             : 0.0F;
}

/// The mean position of the houses people live in; the origin only in a
/// world with no houses at all (a table-less test).
///
/// It is a WANTED position and never a final one: the mean of a village
/// is a spot in the middle of that village, where somebody already lives.
/// Every caller passes it through FreePlot.

Vec2 VillagePosition(const WorldState& current) {
  Vec2 sum{.x = 0.0F, .y = 0.0F};
  std::uint32_t seen = 0;
  for (const UnitRow& unit : current.units.rows) {
    if (unit.household.value == kInvalidEntityIdValue) {
      continue;
    }
    sum.x += unit.position.x;
    sum.y += unit.position.y;
    ++seen;
  }
  if (seen == 0) {
    return Vec2{.x = 0.0F, .y = 0.0F};
  }
  return Vec2{.x = sum.x / static_cast<float>(seen), .y = sum.y / static_cast<float>(seen)};
}

}  // namespace

/// @brief The house a new household moves into.
///
/// A FREE house first — one nobody lives in, of a housing type, standing
/// (level 0 is a site, not a roof). That is the canon's own order
/// (life-cycle §12: "a free house — new, freed, or one the farm got at the
/// start"), and it is how a yard emptied by a death or a marrying-out
/// comes to be lived in again.
///
/// Failing that, STUB: a new house is raised on the spot, so the housing
/// gate never blocks a wedding. This stub is NOT the kolkhoz yard's kind
/// (boss, 2026-09-02): the yard fills a gap — it does what the player would
/// have done — while this one OVERRIDES a rule: the canon says no free
/// house, no wedding (life-cycle §12), and living with the parents is not
/// in it. It stays only until a run can build houses for the player; the
/// thirty-year population it buys is an upper bound, not the curve. It
/// stands BESIDE THE PARENTS — at the groom's house, or the bride's, or
/// amid the village when neither has one (a migrant couple). It used to be appended with no
/// position at all, which is the map's origin: inside the old ten-kilometre village and twelve
/// kilometres from the new one, so every household founded after the
/// start walked all day and worked nothing, and the farm stopped mowing
/// by its seventh year. A position belongs to the scene, never to a
/// default.
UnitId SettleHouse(const LifeConfig& config,
                   WorldState& current,
                   FamilyId groom_family,
                   FamilyId bride_family) {
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const UnitRow& unit = current.units.rows[row];
    if (unit.household.value != kInvalidEntityIdValue || unit.level == 0 ||
        unit.type.value >= config.definitions.units.is_housing.size() ||
        config.definitions.units.is_housing[unit.type.value] == 0) {
      continue;
    }
    return current.units.row_ids[row];
  }
  UnitRow house;
  house.type = config.house_type;
  Vec2 wanted;
  if (!FamilyHousePosition(current, groom_family, wanted) &&
      !FamilyHousePosition(current, bride_family, wanted)) {
    wanted = VillagePosition(current);
  }
  // AND NOW THE SAME RULE AN ORDERED BUILDING GOES THROUGH. Both wanted
  // positions are places that are ALREADY TAKEN — the parents' own house,
  // or the mean of all the houses, which is itself a spot somebody's
  // house tends to sit on — so taking either as given put every new
  // household inside a plot that was standing. Three generations of one
  // family came out as three houses at one coordinate, and every migrant
  // couple of the campaign at the same village mean: the layer drew a
  // stack of identical slabs, which is how this was found (boss,
  // 2026-09-04). The plot rule was never wrong; it simply guarded the
  // ORDER BOOK, and this house does not come through the book.
  house.position = FreePlot(current.units, PlotRulesOf(config), wanted, HouseRadius(config));
  return AppendRow(current.units, house);
}

/// Families whose house is gone: a free one if the village has it, a STUB
/// one otherwise — the same path a newly wed couple and a migrant take.
/// Runs before everything else in the day so that nobody is counted
/// homeless twice.
void Rehouse(const LifeConfig& config, WorldState& current) {
  for (std::uint32_t row = 0; row < current.families.rows.size(); ++row) {
    FamilyRow& family = current.families.rows[row];
    if (family.house.value != kInvalidEntityIdValue &&
        FindRow(current.units, family.house) != kNoRow) {
      continue;
    }
    const FamilyId id = current.families.row_ids[row];
    const UnitId house = SettleHouse(config, current, id, FamilyId{});
    // SettleHouse may append a unit row, which can move the family rows'
    // neighbours but not this vector: families and units are separate
    // tables. Re-read anyway — the reference above is older than the call.
    current.families.rows[row].house = house;
    const std::uint32_t house_row = FindRow(current.units, house);
    if (house_row != kNoRow) {
      current.units.rows[house_row].household = id;
    }
  }
}

}  // namespace core
