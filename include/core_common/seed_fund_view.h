/// @file
/// @brief The seed fund as the stores window prints it (#1680 via boss; game's
/// window, resources.md §6 «Посевной материал под назначенный севооборот
/// следующего года»): the grams held for the next sowings, by resource -
/// «семенной фонд: нужно на сев {amount}».
/// @threading PARALLEL_READONLY
/// Plain data, built between steps by a pure read (ISession::SeedFund).

#ifndef CORE_COMMON_SEED_FUND_VIEW_H_
#define CORE_COMMON_SEED_FUND_VIEW_H_

#include "core_common/quantities.h"

namespace core {

/// @brief The fund's need today.
struct SeedFundView {
  /// False from a bare engine: say «not measured», never «nothing to sow».
  bool measured = false;

  /// Grams held for the next sowings, dense by ResourceId: the seed rung's one
  /// rule (fund_ladder.h SeedHeldByField) - each field's next sowing and the
  /// winter crop its chain sows after it, when held. The same number the
  /// ration, the exchange, the herds and the delivery door may not take.
  ResourceAmounts need;
};

}  // namespace core

#endif  // CORE_COMMON_SEED_FUND_VIEW_H_
