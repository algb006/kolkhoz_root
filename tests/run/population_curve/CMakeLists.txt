kolkhoz_add_test_app(population_curve main.cpp)
# PROCESSORS 16: the run walks its sixteen villages at once, a thread a seed
# (main.cpp, std::async) - and ctest counted it as one, so a suite on -j N ran
# N + 15 threads whenever it overlapped the others (the human, 13 October 2026,
# on the Xeon's load: the declared threads are a ceiling).
# KOLKHOZ_WALKERS_SUITE 16 (7 Nov 2026): with no number asked the run walks ONE
# village at a time, so the suite asks for the sixteen it books - the same
# number as PROCESSORS, the two kept together. It is the LAST source main.cpp
# reads: --walkers, then a job's own KOLKHOZ_WALKERS (a node suite that booked
# fewer exports it), then this.
set_tests_properties(population_curve PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    PROCESSORS 16
    ENVIRONMENT "KOLKHOZ_WALKERS_SUITE=16")
