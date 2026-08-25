# Mandatory per-module unit tests.
#
# One folder per module, named exactly like the module it covers:
#   tests/unit/core_time/CMakeLists.txt  ->  kolkhoz_add_unit_test(core_time main.cpp)
#
# The helper links `core` and the module itself, names the application
# unit_core_time and labels it "unit", so `ctest -L unit` runs exactly the
# obligatory set.

file(GLOB kolkhoz_unit_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT kolkhoz_unit_dirs)

foreach(unit_dir IN LISTS kolkhoz_unit_dirs)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${unit_dir}"
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${unit_dir}/CMakeLists.txt")
    add_subdirectory("${unit_dir}")
  endif()
endforeach()
