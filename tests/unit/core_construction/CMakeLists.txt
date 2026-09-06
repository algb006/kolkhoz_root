kolkhoz_add_unit_test(core_construction main.cpp)

# ONE check in this file needs the SHIPPED tables, and it is the one whose
# subject IS the shipped data: the stink radii are this core's ASSUMPTION,
# and the designed start has three sources and twenty-one houses. Whether a
# house stands in a stink zone on day one is a fact about tables/, and no
# in-memory fixture can answer it — a test that writes its own layout would
# be agreeing with itself. The path is a definition rather than a working
# directory: unit tests are run from wherever ctest stands.
target_compile_definitions(unit_core_construction
    PRIVATE "KOLKHOZ_TABLES_DIR=\"${CMAKE_SOURCE_DIR}/tables\"")
