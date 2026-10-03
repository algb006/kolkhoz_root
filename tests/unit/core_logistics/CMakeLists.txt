kolkhoz_add_unit_test(core_logistics main.cpp)

# The tasks' functions are an internal seam of the module (logistics_tasks.h);
# the unit tests drive them directly, so the module's source dir is on the
# include path.
target_include_directories(unit_core_logistics
    PRIVATE "${CMAKE_SOURCE_DIR}/subprojects/core_logistics")
