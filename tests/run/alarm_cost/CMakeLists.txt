# alarm_cost: a profiling probe over a saved world, NOT a ctest test (it needs a file a thirty_years run
# wrote; main.cpp). Built beside the run apps so a profiler finds it.
add_executable(alarm_cost main.cpp)
kolkhoz_configure_target(alarm_cost)
target_include_directories(alarm_cost PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(TARGET core)
  target_link_libraries(alarm_cost PRIVATE core)
endif()
