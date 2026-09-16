kolkhoz_add_test_app(thirty_years main.cpp)
set_tests_properties(thirty_years PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The three arms behind the flags, for the same reason as idle_curve's: the
# suite ran this app with no arguments, so the free-materials ceiling and the
# two sawmill variants were written and never run (coverage, 2026-09-16). All
# three pass today; each is a full thirty-year walk, which is what they cost.
foreach(arm free-materials saw-reserve saw-by-anyone)
  add_test(NAME thirty_years_${arm} COMMAND thirty_years --${arm})
  set_tests_properties(thirty_years_${arm} PROPERTIES
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS run)
endforeach()
