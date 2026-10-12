kolkhoz_add_test_app(thirty_years main.cpp)
set_tests_properties(thirty_years PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The three arms behind the flags, for the same reason as idle_curve's: the
# suite ran this app with no arguments, so the free-materials ceiling and the
# two sawmill variants were written and never run (coverage, 2026-09-16). All
# three pass today; each is a full thirty-year walk, which is what they cost.
# SEED 0 BY NAME (#700, 12 Oct 2026): these arms ran `thirty_years --${arm}`, whose
# first argument the run read as the seed - strtoull of a flag is nought - so they
# have always measured seed 0, not the canon's 1929. The run now refuses a word
# where a number stands and reads flags anywhere (tests/run/common/run_args.h);
# the arms keep the world they were measured in, named here. Moving them to the
# canon's seed is boss's question, not a parser's side effect.
foreach(arm free-materials saw-reserve saw-by-anyone)
  add_test(NAME thirty_years_${arm} COMMAND thirty_years 0 30 --${arm})
  set_tests_properties(thirty_years_${arm} PROPERTIES
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS run)
endforeach()
