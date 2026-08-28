# Core modules.
#
# One folder per module: subprojects/<name>/CMakeLists.txt declares an OBJECT
# target through kolkhoz_add_module(). Every such target is archived into the
# single static library `core`.
#
# Rules (CLAUDE.md §7; the module map is manual/54-modules.md):
#   * core_common is the base — types, identifiers, math, the world state as
#     plain data. Everyone may use it.
#   * three tiers: infrastructure (core_common, core_log, core_tables,
#     core_sim) may be used by any module; subject modules (core_time,
#     core_residents, core_production, core_logistics, core_labor) never name
#     each other — data flows between them through the state in core_common;
#     core_world is the composition root and the only module that may name
#     everyone.
#   * dependencies are declared explicitly with target_link_libraries().
#   * if two subject modules reach for each other, the boundary is wrong —
#     report it, do not add the dependency.
#
# Folders are picked up automatically in sorted order, so link order is
# deterministic.

file(GLOB kolkhoz_module_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT kolkhoz_module_dirs)

foreach(module_dir IN LISTS kolkhoz_module_dirs)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${module_dir}"
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${module_dir}/CMakeLists.txt")
    add_subdirectory("${module_dir}")
  endif()
endforeach()

get_property(kolkhoz_modules GLOBAL PROPERTY KOLKHOZ_MODULE_TARGETS)

if(kolkhoz_modules)
  set(kolkhoz_module_objects "")
  foreach(module_target IN LISTS kolkhoz_modules)
    list(APPEND kolkhoz_module_objects "$<TARGET_OBJECTS:${module_target}>")
  endforeach()

  add_library(core STATIC ${kolkhoz_module_objects})
  target_link_libraries(core PUBLIC core_headers ${kolkhoz_modules})
  set_target_properties(core PROPERTIES OUTPUT_NAME core)

  message(STATUS "core modules: ${kolkhoz_modules}")
else()
  message(STATUS "core modules: none yet — the library is not created")
endif()
