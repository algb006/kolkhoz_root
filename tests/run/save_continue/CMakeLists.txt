kolkhoz_add_test_app(save_continue main.cpp)
# A game loaded mid-day is the continued game (save 229, #2072): run from the repo root.
set_tests_properties(save_continue PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
