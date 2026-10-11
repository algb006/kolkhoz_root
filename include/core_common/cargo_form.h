/// @file
/// @brief CargoForm — what a load is to the cart that carries it: what may
/// share the cart's body with it, whether people may ride on top of it, and
/// what the cart looks like on the road.
/// @threading PARALLEL_READONLY
/// Plain data and pure rules over it; nobody writes them into the world.
///
/// Why it exists: transport design §11, «Форма груза: что с чем едет и кого
/// подсаживают». The human's words of 5 October 2026: «Например телега с
/// бревнами не может взять попутчиков»; on boss's questions «1. В эту
/// сборку. От этого зависит визуал телеги в пути и работа логистики. 2. Не
/// усложняем. Визуал телеги перестраивается под текущий груз»; «3. Сено и
/// солома надо». ONE form per resource (resources.csv `cargo_form`, a design
/// base column - boss); the cart is not a counted object, its look follows
/// the load it carries now.
///
/// Until it, the core seated riders on a cart's first leg whatever it
/// carried (logistics_plan.cpp), a log cart included.

#ifndef CORE_COMMON_CARGO_FORM_H_
#define CORE_COMMON_CARGO_FORM_H_

#include <cstdint>

namespace core {

/// @brief The design's seven forms (transport §11's table), and kNone for
/// a leg with no load (to a load, home in the evening).
enum class CargoForm : std::uint8_t {
  kNone = 0,  ///< An empty leg: no load, every seat open.
  kBulk,      ///< «Внавал»: potato, roots, threshed grain, clay, sand, stone.
  kManure,    ///< «Навоз и нечистоты»: manure, the pits' contents.
  kPacked,    ///< «В таре»: sacks, cans, barrels, crates — food, fuel, parts.
  kRick,      ///< «Воз»: hay, straw - riders sit on top.
  kLong,      ///< «Длинномер»: logs, boards, poles.
  kLive,      ///< «Живое»: calves, pigs, fowl in cages.
  kPeople,    ///< «Люди»: the people's cart; no goods.
  kCargoFormCount
};

/// @brief Whether a leg carrying `form` may seat riders beside the driver
/// (transport §11, «Попутчики»: bulk, packed and the rick yes; manure, long
/// and live no; an empty leg always). The people's cart seats by its own
/// rule (its six places), not by this one.
bool CargoFormSeatsRiders(CargoForm form);

/// @brief Whether a load of `added` may ride in one body with a load of
/// `first` (transport §11, «Смешение»): bulk only with the SAME resource
/// (`same_resource`), packed with packed except food with fuel
/// (`food_with_fuel`), the rick only with the same resource, every other
/// form only with itself. An empty leg mixes with nothing: it carries none.
/// Read by the mixing step (stage-В delivery F3); F2 does not mix.
bool CargoFormsMix(CargoForm first, CargoForm added, bool same_resource, bool food_with_fuel);

}  // namespace core

#endif  // CORE_COMMON_CARGO_FORM_H_
