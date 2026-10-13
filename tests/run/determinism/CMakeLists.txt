kolkhoz_add_test_app(determinism main.cpp)
# PROCESSORS 4: the many-thread arm steps on four workers (main.cpp), and ctest
# counts a test as one thread unless told (population_curve's note).
set_tests_properties(determinism PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    PROCESSORS 4)
