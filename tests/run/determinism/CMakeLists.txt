kolkhoz_add_test_app(determinism main.cpp)
set_tests_properties(determinism PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
