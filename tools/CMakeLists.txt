# core_layout — the published layout report (see the source for why).
#
# Built always, not under KOLKHOZ_BUILD_TESTS: the Windows publish runs it,
# and the publish is exactly the thing it exists to check.

add_executable(core_layout core_layout.cpp)
kolkhoz_configure_target(core_layout)
target_link_libraries(core_layout PRIVATE core_common)
