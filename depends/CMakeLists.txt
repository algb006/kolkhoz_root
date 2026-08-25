# Third-party libraries shipped as sources.
#
# One folder per library, added here with add_subdirectory(). Foreign code is
# built with its own warning settings: core_options is ours and is not applied.
# Prebuilt archives go to ../libs instead.
#
# Expected here in phase 1 (manual/plan/51-phase1-plan.md):
#   enkiTS                       — task scheduler
#   moodycamel::ReaderWriterQueue — queue on the core/presentation boundary
#   Dear ImGui                   — simulation-run UI, stage 7
#
# Empty for now.

file(GLOB kolkhoz_depend_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT kolkhoz_depend_dirs)

foreach(depend_dir IN LISTS kolkhoz_depend_dirs)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${depend_dir}"
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${depend_dir}/CMakeLists.txt")
    add_subdirectory("${depend_dir}" EXCLUDE_FROM_ALL)
  endif()
endforeach()
