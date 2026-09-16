kolkhoz_add_test_app(idle_curve main.cpp)
set_tests_properties(idle_curve PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# THE ARMS BEHIND THE FLAGS, REGISTERED SO THAT THE SUITE ACTUALLY RUNS THEM.
# ctest called the app with no arguments and nothing else, so seven arms of
# this run — every check they carry — had never been executed by the suite at
# all. Measured by coverage on 2026-09-16: the alarms kSiteUnreachable and
# kSiteWithoutMaterials came out as code no test reaches, while the checks for
# them sat right here, written and unrun (boss, standstill parcels 11, 14).
#
# THE TWO UNSTEERED ARMS WERE AIMED AT THE WRONG ANSWER, and that is why they
# were red: `--no-chairman` and `--yard-only` demanded that a village nobody
# steers survive, while the canon says it must end («Село кончилось»). They
# assert the stop now, and boss's words are in the test beside it (standstill
# parcel 16): the design promises the PLAYER a move, not that doing nothing is
# survivable.
#
# AND `--no-horses` JOINS IN THE COMMIT THAT OPENS THE LIVESTOCK WINDOW, which
# is the whole of rule §6д (manual/setup/56-static-analysis-and-tests.md): it
# was written before the work, as its acceptance, and was red on purpose until
# the district would sell a horse. An arm outside the suite is either that or
# a forgotten check, and by the file the two are identical.
foreach(arm far gap herd-alarm watch-build sow-idle-land no-chairman yard-only no-horses)
  add_test(NAME idle_curve_${arm} COMMAND idle_curve --${arm})
  set_tests_properties(idle_curve_${arm} PROPERTIES
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS run)
endforeach()
