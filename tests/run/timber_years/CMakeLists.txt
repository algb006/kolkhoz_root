kolkhoz_add_test_app(timber_years main.cpp)
set_tests_properties(timber_years PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# SEED 1932 AS WELL (0.36.19): the check "no timber cart on a horse while a
# plough or a harrow waits uncrewed" cannot go red on the default seed 1929 —
# with the stand's haul window given back its village never meets the case,
# while 1932's does (14 man-days on one day). A test that cannot fail is a
# stub; this entry is the one that can.
add_test(NAME timber_years_1932 COMMAND timber_years 1932)
set_tests_properties(timber_years_1932 PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    LABELS run)
