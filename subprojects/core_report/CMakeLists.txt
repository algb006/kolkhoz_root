# core_report — the run ledger as CSV: the yearly flows of WorldState::ledger
# and the state at the turn, one row per year, header derived from the
# tables (contract in include/core_report/ledger_csv.h, decision in
# manual/68-run-ledger.md; stage 7, task F2 designs it, task O2 implements
# it together with the thirty-year run).
#
# Infrastructure tier, a leaf like core_save: state and table keys in, text
# out; no subsystem is named and nothing in the core depends on it.

kolkhoz_add_module(core_report
    placeholder.cpp)

target_link_libraries(core_report
    PUBLIC core_common
    PRIVATE core_tables)
