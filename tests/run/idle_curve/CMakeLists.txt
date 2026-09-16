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
# TWO ARMS ARE NOT REGISTERED, and their absence is a finding rather than an
# oversight: `--no-chairman` and `--yard-only` both fail today on the design's
# own red line — "a year with work standing and nobody working at all" — three
# times each. That is a question about the model (a village that loses its last
# draught horse stalls for ever), and it is boss's to answer; registering them
# red would only make the suite red without adding a single new fact.
foreach(arm far gap herd-alarm watch-build sow-idle-land)
  add_test(NAME idle_curve_${arm} COMMAND idle_curve --${arm})
  set_tests_properties(idle_curve_${arm} PROPERTIES
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS run)
endforeach()
