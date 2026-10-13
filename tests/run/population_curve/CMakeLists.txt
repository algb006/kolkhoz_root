kolkhoz_add_test_app(population_curve main.cpp)
# PROCESSORS 16: the run walks its sixteen villages at once, a thread a seed
# (main.cpp, std::async) - and ctest counted it as one, so a suite on -j N ran
# N + 15 threads whenever it overlapped the others (the human, 13 October 2026,
# on the Xeon's load: the declared threads are a ceiling).
set_tests_properties(population_curve PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    PROCESSORS 16)
