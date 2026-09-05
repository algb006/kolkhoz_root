/// @file
/// @brief How many fields an aggregate has, at compile time.
/// @threading SINGLE_THREADED
/// Pure compile-time arithmetic over a type; no state, no runtime cost, no
/// call from any phase. Included only by the codecs.
///
/// WHY THIS EXISTS, and it is one defect and not a wish. Every serialised
/// struct here is guarded by `static_assert(sizeof(T) == N)`, so that adding
/// a field breaks the build at the codec instead of writing a save nobody can
/// read back. THE GUARD IS BLIND TO A FIELD THAT FITS THE PADDING: a uint16
/// added behind two uint8s does not move sizeof by a byte, while the stream it
/// is written to grows by two. That has now happened FOUR times in this
/// module (67-save-format.md §7 names three of them; `WeatherState` on
/// 2026-09-05 is the fourth), and each time the build stayed green.
///
/// A check that misses one case in four is worse than no check while anybody
/// believes it: it says "all well" in exactly the situation it was written
/// for (architecture §8ц). Counting the FIELDS sees what the size cannot,
/// because a field is the thing that actually changed.
///
/// HOW IT WORKS: an aggregate can be list-initialised with at most as many
/// initialisers as it has members, so the arity is found by asking the
/// compiler how many `AnythingAtAll` values T will accept and taking the
/// largest that compiles. Aggregates only — a type with a constructor is not
/// one, and the assert would then be measuring something else.

#ifndef CORE_SAVE_AGGREGATE_ARITY_H_
#define CORE_SAVE_AGGREGATE_ARITY_H_

#include <cstddef>
#include <type_traits>
#include <utility>

namespace core {
namespace detail {

/// Converts to anything, so that a probe initialiser says nothing about types
/// and everything about how many of them fit.
struct AnythingAtAll {
  template <typename T>
  operator T() const;  // NOLINT(google-explicit-constructor): that is the point
};

template <typename T, typename Indices, typename = void>
struct FitsArity : std::false_type {};

template <typename T, std::size_t... Index>
struct FitsArity<T,
                 std::index_sequence<Index...>,
                 std::void_t<decltype(T{(static_cast<void>(Index), AnythingAtAll{})...})>>
    : std::true_type {};

template <typename T, std::size_t Count>
constexpr std::size_t CountUpwards() {
  if constexpr (Count > 64) {
    return 0;  // a struct this wide is a design problem, not a codec one
  } else if constexpr (FitsArity<T, std::make_index_sequence<Count + 1>>::value) {
    return CountUpwards<T, Count + 1>();
  } else {
    return Count;
  }
}

}  // namespace detail

/// @brief The number of direct members of an aggregate.
/// @note Aggregates only: a type with a user-declared constructor answers 0,
///       and an assert on 0 would pass for the wrong reason. Every struct
///       this module serialises is an aggregate by the state model's rule
///       that world state is plain data.
template <typename T>
constexpr std::size_t AggregateArity() {
  static_assert(std::is_aggregate_v<T>, "arity is only meaningful for an aggregate");
  return detail::CountUpwards<T, 0>();
}

}  // namespace core

#endif  // CORE_SAVE_AGGREGATE_ARITY_H_
