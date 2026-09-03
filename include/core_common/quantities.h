/// @file
/// @brief Units of measure and numeric conventions of the simulation state.
/// @threading PARALLEL_READONLY
/// Type aliases, compile-time constants, and — since the named cast pass —
/// three PURE conversion functions (GramsFromFloat and its two unit
/// wrappers). Pure is what earns the label: no state of any kind, static or
/// global, no lazy initialization, the result a function of the argument
/// alone. They are called from parallel phases (the family meal, the
/// household plot) as well as sequential ones, and a future edit that gave
/// any of them memory would break this label rather than merely bend it.
///
/// Every quantity that must balance exactly — mass of resources, labor-day
/// accruals, money — is stored as a scaled integer. Integer bookkeeping makes
/// conservation laws exact: what one table subtracts, another table adds, and
/// the sum never drifts. Floating point is reserved for continuous gameplay
/// metrics where a drift of 1e-6 is meaningless.
///
/// Floating-point determinism rules (single-thread run must equal the
/// multi-thread run, Clang build must be checkable against the MSVC build):
///   * float (32-bit) everywhere in state; double only inside local math.
///   * No fast-math. FP contraction is off project-wide (build flags).
///   * Reductions over entities run in row order, never in completion order.

#ifndef CORE_COMMON_QUANTITIES_H_
#define CORE_COMMON_QUANTITIES_H_

#include <cstdint>
#include <vector>

namespace core {

// ---------------------------------------------------------------------------
// Mass — the one unit for every storable resource
// ---------------------------------------------------------------------------

/// @brief Mass of a resource, in grams.
/// One unit for grain, milk, meat, manure, firewood and everything else that
/// lies in a buffer or a pantry: liquids are counted by mass (1 l of milk is
/// ~1000 g), design documents speak in kilograms and tonnes, the presentation
/// converts. Signed on purpose: arithmetic intermediates may dip below zero,
/// but a stored amount must never be negative.
/// Range check: the whole map's yearly harvest is under 4000 t = 4e9 g, and
/// 30 years of everything summed stays far below the 9.2e18 limit.
using Grams = std::int64_t;

inline constexpr Grams kGramsPerKilogram = 1'000;
inline constexpr Grams kGramsPerTonne = 1'000'000;

/// @brief Whole grams from a float mass in kilograms — the ONE way a
/// computed float becomes a stored mass.
///
/// Why a function and not a cast at each site (the named DefId/cast pass,
/// task A7a; boss 2026-09-03). Converting a float to an integer is
/// UNDEFINED when the value is NaN, an infinity, or when its truncation
/// does not fit the destination ([conv.fpint]/1) — and the floats that reach
/// these sites come off hand-editable balance tables and, since the save
/// format exists, off files a reader bit_casts without inspecting. The core
/// had three of these casts guarded by hand and a dozen unguarded, which is
/// how a class of defect becomes a property of the codebase rather than a
/// case. One guard, one place, every site.
///
/// @param kilograms A computed mass. May legitimately be a fraction: the
///        result is TRUNCATED, as every mass in the core is.
/// @return The mass in grams, or 0 when the argument cannot be one: NaN, an
///         infinity, a negative, or a magnitude that would overflow. Zero is
///         the neutral answer everywhere this is used — no stock, no
///         estimate, no dose — so a poisoned number costs an empty result
///         rather than a wrong one or a crash.
/// @note Not the place for a diagnostic: it is called inside per-field and
///       per-row loops, and a caller that wants to complain about a bad cell
///       must check the cell where it is READ (that is what the table
///       parsers do), not here.
Grams GramsFromKilograms(float kilograms);

/// @brief Whole grams from a float mass in tonnes. Same refusals.
Grams GramsFromTonnes(float tonnes);

/// @brief Whole grams from a float that is ALREADY a mass in grams — a
/// share of a harvest, a by-product ratio. The primitive the other two are
/// written in; same refusals, and the same 0 for anything unusable.
Grams GramsFromFloat(float grams);

/// @brief Amounts of every resource kind, indexed by ResourceId (ids.h).
/// A dense vector sized to the resource definition table: amounts[id.value]
/// is the stored mass of that resource. The layout of a warehouse buffer, a
/// family pantry and the yearly plan is one and the same type.
using ResourceAmounts = std::vector<Grams>;

// ---------------------------------------------------------------------------
// Labor and money
// ---------------------------------------------------------------------------

/// @brief Labor-day (trudoden) balance, in hundredths of a labor-day.
/// Work rates are 0.5 / 1.0 / 1.5 / 2.0 per day (labor-payment design, §2),
/// with skill coefficients on top — hundredths hold every rate exactly.
/// Accrues to the family account, not to the person; the yearly account is
/// closed at the end of the economic year and unspent trudodni burn.
using TrudodniHundredths = std::int32_t;

inline constexpr TrudodniHundredths kTrudodniScale = 100;

/// @brief Money, in kopecks. Family purses and the kolkhoz account.
/// Money exists from Epoch I (market, sales); wages in money appear only in
/// Epoch III. Integer for the same reason as Grams: exact bookkeeping.
using Kopecks = std::int64_t;

inline constexpr Kopecks kKopecksPerRuble = 100;

// ---------------------------------------------------------------------------
// Continuous gameplay metrics
// ---------------------------------------------------------------------------

/// @brief A gameplay metric on the canonical 0..100 scale.
/// Satiety, health, rest, mood, fertility of a field, wear of a unit,
/// satisfaction of a family — the design gives them all the same scale.
/// Stored as float: metrics drift by design and are never summed across
/// long periods, so integer exactness buys nothing here.
using Metric = float;

/// @brief The top of the wear scale — a ruin (unit rules §15: 0..100, and
/// 100 is where it stops). Named here beside Metric because wear IS one,
/// and because "100" spelled out at every clamp is the kind of magic
/// number a rule hides behind (task A5).
inline constexpr Metric kWearScale = 100.0F;

inline constexpr Metric kMetricMin = 0.0f;
inline constexpr Metric kMetricMax = 100.0f;

/// @brief A dimensionless fraction or multiplier, usually 0..1.
/// Progress of a production cycle, a weight in an aggregate formula, a
/// probability. Multipliers above 1 (e.g. the intellect growth factor) use
/// the same type.
using Ratio = float;

}  // namespace core

#endif  // CORE_COMMON_QUANTITIES_H_
