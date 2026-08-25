# Console applications: unit tests and simulation runs.
#
#   tests/unit/<module>/   MANDATORY unit test of that module, one per module.
#                          Application unit_<module>, CTest label "unit".
#   tests/run/<name>/      Simulation runs and everything else, as needed.
#                          CTest label "run".
#
# The unit test is not optional: a module (a .h + .cpp pair) is not handed over
# until its test exists and passes. `scripts/check_module_tests.sh` checks that
# mechanically, `make check-tests` runs the check.
#
# The unit-test framework is not chosen in the design documents yet — plain
# executables returning a non-zero exit code are enough until it is.
#
# tests/ is deliberately outside the /check_static_new analysis loop: analysis
# here is simplified and run on demand. See manual/setup/56-static-analysis-and-tests.md.

add_subdirectory(unit)
add_subdirectory(run)
