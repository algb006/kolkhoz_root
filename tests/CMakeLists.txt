# Console applications: unit tests and simulation runs.
#
# One folder per application: tests/<name>/CMakeLists.txt calls
# kolkhoz_add_test_app(<name> <sources>). The helper links `core`, applies the
# project options and registers the app with CTest, so `make test` runs it.
#
# Both kinds of runs live here: a quick check that a subsystem behaves, and a
# thirty-year run whose numbers are compared against manual/balance/49-simulations.md.
#
# The unit-test framework is not chosen in the design documents yet — plain
# executables returning a non-zero exit code are enough until it is.
#
# Empty for now.

file(GLOB kolkhoz_test_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT kolkhoz_test_dirs)

foreach(test_dir IN LISTS kolkhoz_test_dirs)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}"
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}/CMakeLists.txt")
    add_subdirectory("${test_dir}")
  endif()
endforeach()
