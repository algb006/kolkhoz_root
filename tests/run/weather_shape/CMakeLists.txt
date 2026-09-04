kolkhoz_add_test_app(weather_shape main.cpp)
# It reads tables/weather.csv and builds its own memoryless twin of it, so it
# runs from the repository root like the other table-driven runs.
set_tests_properties(weather_shape PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
