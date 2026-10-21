// The whole-trip arithmetic of the drawn trip's price (core_common/trip_terms.h; #1056 C3).

#include <cstdint>
#include <iostream>

#include "core_common/trip_terms.h"

namespace core_test {
namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: trip terms: " << label << '\n';
  return 1;
}

}  // namespace

int TestTripTerms() {
  int failures = 0;
  // A 750 kg cart: a tonne is two trips, 750 kg one, a gram over it two.
  constexpr core::Grams kCart = 750'000;
  failures +=
      Expect(core::WholeTripsFor(1'000'000, kCart) == 2, "a tonne at 750 kg a cart is two trips");
  failures +=
      Expect(core::WholeTripsFor(kCart, kCart) == 1 && core::WholeTripsFor(kCart + 1, kCart) == 2,
             "a full load is one trip, a gram over it two");
  failures += Expect(core::WholeTripsFor(0, kCart) == 0 && core::WholeTripsFor(1'000, 0) == 0 &&
                         core::WholeTripsFor(-5, kCart) == 0,
                     "no grams or no load, no trip");

  // THE FALLBACK: whole trips at the round trip, over the norm day - trip_model 1's arithmetic.
  core::TaskTripTerms terms;
  terms.trip_hours = 1.5F;
  terms.trip_loaded_hours = 0.9F;
  terms.cart_load = kCart;
  failures += Expect(core::FallbackCartDays(terms, 1'000'000, 10.0F) == 0.3F,
                     "a tonne: two trips of 1.5 h over a 10 h day is 0.3 cart-days");
  failures +=
      Expect(core::FallbackCartDays(terms, 1, 10.0F) == 0.15F, "one gram still takes a whole trip");
  core::TaskTripTerms none;
  failures += Expect(core::FallbackCartDays(none, 1'000'000, 10.0F) == 0.0F &&
                         core::FallbackCartDays(terms, 1'000'000, 0.0F) == 0.0F,
                     "terms with no trip, or no norm day, price nothing");
  std::cout << "  trip terms: a tonne at 750 kg is " << core::WholeTripsFor(1'000'000, kCart)
            << " trips, " << core::FallbackCartDays(terms, 1'000'000, 10.0F) << " cart-days\n";
  return failures;
}

}  // namespace core_test
