# core_construction — the construction subsystem: a unit is built over time
# and costs materials and labour days; it is raised a level the same way and
# taken down the same way in reverse (contract in
# include/core_construction/construction_system.h, decisions in
# manual/71-construction.md; project phase 2, task A2 — designed by the
# senior pass, filled by the implementing task).
#
# Subject tier. It owns no phase slot: its one sub-step runs sequentially in
# the decisions slot, called by core_world, so it does not know core_sim. It
# names no other subject module — the site is the unit row itself, the
# labour seam is a field of that row, and the stores it takes materials from
# are unit stock: everything crosses through core_common state. The live
# tables give it the ladder, the recipes, the gates and the plots.

kolkhoz_add_module(core_construction
    construction_config.cpp
    construction_system.cpp)

target_link_libraries(core_construction
    PUBLIC core_common
    PRIVATE core_tables core_log)
