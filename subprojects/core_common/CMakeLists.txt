# core_common — the base: identifiers, units of measure, calendar arithmetic,
# geometry math, state-table operations, the RNG. The only module every other
# module may always use; itself depends on nothing.
#
# The state of the world lives in include/core_common/ as plain data
# (manual/52-state-model.md), so subsystems read each other's state through
# this module alone and never through each other.

kolkhoz_add_module(core_common
    alarm_state.cpp
    flood_state.cpp
    body.cpp
    authority.cpp
    day_off.cpp
    district_visit_state.cpp
    fund_ladder.cpp
    herd_age_band.cpp
    road_graph.cpp
    road_day.cpp
    road_route.cpp
    trail_lattice.cpp
    trails.cpp
    road_rules.cpp
    road_view.cpp
    meadow_mark.cpp
    ice_water.cpp
    store_theft.cpp
    obstacle_raster.cpp
    road_trace.cpp
    road_pieces.cpp
    road_cut.cpp
    home_reach.cpp
    funeral_reads.cpp
    horse_yard_road.cpp
    haul.cpp
    trip_terms.cpp
    cart_gear.cpp
    load_resource.cpp
    hauled_book.cpp
    logistics_rules.cpp
    cold_housing.cpp
    cargo_form.cpp
    cemetery_rules.cpp
    wait_state.cpp
    plot.cpp
    spoilage.cpp
    quantities.cpp
    rain_stops_work.cpp
    stock_forecast.cpp
    resident_activity.cpp
    version_pin.cpp
    work_seam.cpp
    site_supply_queue.cpp
    chairman_away.cpp)
