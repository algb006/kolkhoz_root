kolkhoz_add_unit_test(core_labor
    main.cpp
    harvest_order_checks.cpp
    harvest_days_off_checks.cpp
    brigade_cart_checks.cpp
    top_up_checks.cpp
    carter_on_foot_checks.cpp
    carter_road_checks.cpp
    horse_yard_road_checks.cpp
    cart_passengers_checks.cpp
    stuck_state_checks.cpp
    cart_load_checks.cpp
    hay_cart_rank_checks.cpp
    people_cart_checks.cpp
    groom_plan_checks.cpp)

# The placement algorithm is an internal seam of the module (assignment.h);
# its unit tests feed the structs directly, so the module's source dir is on
# the include path.
target_include_directories(unit_core_labor
    PRIVATE "${CMAKE_SOURCE_DIR}/subprojects/core_labor")
