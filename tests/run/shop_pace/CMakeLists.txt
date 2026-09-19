kolkhoz_add_test_app(shop_pace main.cpp)
set_tests_properties(shop_pace PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The yard 1.6 km from the master's house: the road's price, printed; the
# stand and the issue asserted as at his door.
add_test(NAME shop_pace_far COMMAND shop_pace --far)
set_tests_properties(shop_pace_far PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS run)
