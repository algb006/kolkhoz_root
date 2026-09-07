# core_catalog — the definitions read out of the balance tables, and the one
# policy for reading a value with its range (task A6).
#
# INFRASTRUCTURE TIER, beside core_tables: it knows what a unit type IS, and
# nothing about what any subsystem does with one. Subject modules read the
# catalogue; the catalogue reads none of them.
#
# The rule that keeps it a catalogue and not a bag of shared code: NO
# BEHAVIOUR LIVES HERE. A function that moved in because two modules called
# it would make this the dumping ground for whatever is common, which is the
# opposite of a catalogue (boss, 2026-09-04).

kolkhoz_add_module(core_catalog
    definitions.cpp
    table_lookup.cpp
    table_value.cpp)

target_link_libraries(core_catalog
    PUBLIC core_common core_tables)
