kolkhoz_add_unit_test(core_labor main.cpp)

# The placement algorithm is an internal seam of the module (assignment.h);
# its unit tests feed the structs directly, so the module's source dir is on
# the include path.
target_include_directories(unit_core_labor
    PRIVATE "${CMAKE_SOURCE_DIR}/subprojects/core_labor")
