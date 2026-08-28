# Third-party libraries shipped as sources.
#
# One folder per library, added here with add_subdirectory(). Foreign code is
# built with its own warning settings: core_options is ours and is not applied.
# Prebuilt archives go to ../libs instead.
#
# Expected here in phase 1 (manual/plan/51-phase1-plan.md):
#   enkiTS                       — task scheduler (vendored: v1.11, commit 6ffccbd,
#                                  .git and example/ stripped; C interface and
#                                  examples switched off below)
#   moodycamel::ReaderWriterQueue — queue on the core/presentation boundary
#   Dear ImGui                   — simulation-run UI, stage 7

set(ENKITS_BUILD_C_INTERFACE OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# enkiTS says cmake_minimum_required(3.0); CMake 4.x (the MSVC host) refuses
# anything below 3.5 outright. Treat the foreign minimum as 3.5 instead of
# patching the vendored file.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

file(GLOB kolkhoz_depend_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT kolkhoz_depend_dirs)

foreach(depend_dir IN LISTS kolkhoz_depend_dirs)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${depend_dir}"
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${depend_dir}/CMakeLists.txt")
    add_subdirectory("${depend_dir}" EXCLUDE_FROM_ALL)
  endif()
endforeach()

# enkiTS's own CMakeLists is old-style and exports no usage requirements;
# attach the include path here. SYSTEM keeps our warning set off foreign
# headers (they are not built with -Werror and must not fail it).
if(TARGET enkiTS)
  target_include_directories(enkiTS SYSTEM INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/enkiTS/src")
endif()
