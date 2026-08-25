# The game executable.
#
# Phase 2 material: in phase 1 the core is exercised through tests/ only, and
# there is no UE layer to start. Kept wired up so that dropping main.cpp here
# and configuring with -DKOLKHOZ_BUILD_GAME=ON is all it takes.
#
# The executable must stay a thin entry point: no simulation logic here.

if(NOT KOLKHOZ_BUILD_GAME)
  return()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
  message(STATUS "src/: KOLKHOZ_BUILD_GAME is ON but main.cpp is absent — skipped")
  return()
endif()

file(GLOB_RECURSE kolkhoz_game_sources CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kolkhoz ${kolkhoz_game_sources})
kolkhoz_configure_target(kolkhoz)

if(TARGET core)
  target_link_libraries(kolkhoz PRIVATE core)
endif()
