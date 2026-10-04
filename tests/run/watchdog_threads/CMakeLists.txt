kolkhoz_add_test_app(watchdog_threads main.cpp)
set_tests_properties(watchdog_threads PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
