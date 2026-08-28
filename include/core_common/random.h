/// @file
/// @brief Deterministic random numbers: the sequential RNG and the
/// counter-style hash for parallel phases.
/// @threading PARALLEL_READONLY
/// The CounterHash* functions are pure — no state, callable from any worker
/// thread; randomness inside a parallel pass is derived counter-style from
/// (seed, tick, entity id), so the result cannot depend on worker scheduling
/// (step-cycle contract, manual/53-step-cycle.md). Functions taking RngState&
/// mutate it and are called only from single-threaded phases of the step: the
/// world owns exactly one sequential RNG (WorldState::rng).
///
/// Algorithms — fixed for the life of the save format, identical on Clang and
/// MSVC because everything is exact 64-bit integer arithmetic:
///   * Sequential: PCG32 (XSH-RR 64/32, M.E. O'Neill) — 64-bit state plus a
///     64-bit odd stream constant, 32 random bits per draw.
///   * Counter: SplitMix64 finalizer chained over (seed, tick, entity, salt).
/// Changing either algorithm changes every world — that is a VERSION_SAVE
/// event, not a refactor.

#ifndef CORE_COMMON_RANDOM_H_
#define CORE_COMMON_RANDOM_H_

#include <cstdint>

namespace core {

/// @brief Sequential random-number generator state, 128 bits.
/// The world owns exactly one, advanced only in single-threaded phases;
/// parallel phases use the counter-hash functions below instead. The bits are
/// opaque to everyone outside this header: seed via SeedRngState, draw via
/// NextRandom*.
struct RngState {
  std::uint64_t state = 0;

  /// Stream selector; kept odd by SeedRngState as PCG32 requires.
  std::uint64_t stream = 0;
};

namespace internal {

inline constexpr std::uint64_t kPcgMultiplier = 6364136223846793005ULL;

/// @brief SplitMix64 finalizer: bijective 64-bit mix with full avalanche.
constexpr std::uint64_t MixBits(std::uint64_t bits) {
  bits += 0x9E3779B97F4A7C15ULL;
  bits = (bits ^ (bits >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  bits = (bits ^ (bits >> 27U)) * 0x94D049BB133111EBULL;
  return bits ^ (bits >> 31U);
}

}  // namespace internal

/// @brief Advances the generator one step and returns 32 random bits.
/// @note Call only from single-threaded phases (see @file).
constexpr std::uint32_t NextRandomBits(RngState& rng) {
  const std::uint64_t old_state = rng.state;
  rng.state = old_state * internal::kPcgMultiplier + rng.stream;
  const auto xorshifted = static_cast<std::uint32_t>(((old_state >> 18U) ^ old_state) >> 27U);
  const auto rotation = static_cast<std::uint32_t>(old_state >> 59U);
  return (xorshifted >> rotation) | (xorshifted << ((32U - rotation) & 31U));
}

/// @brief Builds a seeded generator (PCG32 initialization).
/// @param seed      Any value; usually derived from WorldState::world_seed.
/// @param stream_id Distinct ids give independent sequences for the same seed.
constexpr RngState SeedRngState(std::uint64_t seed, std::uint64_t stream_id) {
  RngState rng;
  rng.stream = (stream_id << 1U) | 1U;
  rng.state = 0;
  NextRandomBits(rng);
  rng.state += seed;
  NextRandomBits(rng);
  return rng;
}

/// @brief Uniform integer in [0, bound).
/// @param bound Must be > 0. Multiply-shift mapping: bias is below 2^-32 —
///              negligible for gameplay, and branch-free, so draw count per
///              call is always exactly one (replay stability).
constexpr std::uint32_t NextRandomBelow(RngState& rng, std::uint32_t bound) {
  return static_cast<std::uint32_t>((static_cast<std::uint64_t>(NextRandomBits(rng)) * bound) >>
                                    32U);
}

/// @brief Uniform float32 in [0, 1): 24 explicit mantissa bits, exact.
constexpr float NextRandomUnitFloat(RngState& rng) {
  return static_cast<float>(NextRandomBits(rng) >> 8U) * 0x1p-24F;
}

/// @brief Pure counter-style hash: 64 random-looking bits from a position.
/// Same arguments — same bits, on every platform and thread count. Use inside
/// parallel phases where the sequential RNG is off limits.
/// @param seed   WorldState::world_seed.
/// @param tick   Current step, so the draw differs each tick.
/// @param entity The entity id value (or row-independent index) the draw
///               belongs to.
/// @param salt   Distinguishes several draws for the same entity in the same
///               tick; pick a per-call-site constant.
constexpr std::uint64_t CounterHashBits(std::uint64_t seed,
                                        std::uint64_t tick,
                                        std::uint64_t entity,
                                        std::uint64_t salt) {
  std::uint64_t bits = internal::MixBits(seed);
  bits = internal::MixBits(bits ^ tick);
  bits = internal::MixBits(bits ^ entity);
  bits = internal::MixBits(bits ^ salt);
  return bits;
}

/// @brief Uniform float32 in [0, 1) from a counter-hash position.
constexpr float CounterHashUnitFloat(std::uint64_t seed,
                                     std::uint64_t tick,
                                     std::uint64_t entity,
                                     std::uint64_t salt) {
  return static_cast<float>(CounterHashBits(seed, tick, entity, salt) >> 40U) * 0x1p-24F;
}

}  // namespace core

#endif  // CORE_COMMON_RANDOM_H_
