kolkhoz_add_test_app(shop_pace main.cpp)
set_tests_properties(shop_pace PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The yard 1.6 km from the master's house: the road's price, printed; the
# stand and the issue asserted as at his door.
add_test(NAME shop_pace_far COMMAND shop_pace --far)
set_tests_properties(shop_pace_far PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS run)

# The morning's slaughter: the smokehouse works it the same day, its master
# taken off the sauerkraut shop before sunrise (boss, host-econ-shops seq 31).
add_test(NAME shop_pace_smoke COMMAND shop_pace --smoke)
set_tests_properties(shop_pace_smoke PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS run)
