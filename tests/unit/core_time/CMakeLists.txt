kolkhoz_add_unit_test(core_time main.cpp)

# The day's weather is an internal seam of the module (weather_of_day.h): its
# unit test asks the one home of the rule directly, so the module's source
# dir is on the include path.
target_include_directories(unit_core_time
    PRIVATE "${CMAKE_SOURCE_DIR}/subprojects/core_time")
