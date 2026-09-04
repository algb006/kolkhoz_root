// The colour rule of the stock traffic light (stock_forecast.h).

#include "core_common/stock_forecast.h"

namespace core {

StockLight LightFrom(std::int32_t days_of_stock,
                     std::int32_t days_to_date,
                     std::int32_t margin_days,
                     bool already_short) {
  if (already_short) {
    // Red is a fact and outranks any forecast: whatever the arithmetic says
    // about February, the trouble is here in November.
    return StockLight::kRed;
  }
  if (days_of_stock == kStockNeverRunsOut) {
    return StockLight::kGreen;  // nothing consumes it; no date can be missed
  }
  if (days_of_stock >= days_to_date + margin_days) {
    return StockLight::kGreen;
  }
  // "It will not last to the date, or only just." Both halves are yellow on
  // purpose: the light is an invitation to look into the farm, and a player
  // who is told only when it is already impossible has been told too late.
  return StockLight::kYellow;
}

}  // namespace core
