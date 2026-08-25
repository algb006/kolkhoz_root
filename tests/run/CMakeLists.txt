# Simulation runs and other non-obligatory test applications.
#
# One folder per application:
#   tests/run/thirty_years/CMakeLists.txt  ->  kolkhoz_add_test_app(thirty_years main.cpp)
#
# Here live the long runs whose numbers are compared against
# manual/balance/49-simulations.md, and any focused check that does not belong
# to a single module.

file(GLOB kolkhoz_run_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT kolkhoz_run_dirs)

foreach(run_dir IN LISTS kolkhoz_run_dirs)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${run_dir}"
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${run_dir}/CMakeLists.txt")
    add_subdirectory("${run_dir}")
  endif()
endforeach()
