# Core modules.
#
# One folder per module: subprojects/<name>/CMakeLists.txt declares an OBJECT
# target through kolkhoz_add_module(). Every such target is archived into the
# single static library `core`.
#
# Rules (CLAUDE.md §7):
#   * core_common is the base — types, identifiers, math. Everyone may use it.
#   * modules never link to each other directly, only through interfaces;
#     dependencies are declared explicitly with target_link_libraries().
#   * if two modules reach for each other, the boundary is wrong — report it,
#     do not add the dependency.
#
# Folders are picked up automatically in sorted order, so link order is
# deterministic. Nothing here yet: the skeleton arrives in stage 1 of the plan
# (manual/plan/51-phase1-plan.md §3).

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
