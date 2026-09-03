# Kolkhoz Chairman — simulation core.
#
# One output: the static library `core`. Modules live in subprojects/ as CMake
# OBJECT targets and are archived into that single library. See CLAUDE.md §7.
#
# The core never links, includes or knows about Unreal Engine.

cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# Versions — read from files, never hardcoded here
# ---------------------------------------------------------------------------
#
#   VERSION       the core library, semantic versioning
#   VERSION_SAVE  the on-disk layout of a saved game, a plain integer
#
# They are separate on purpose: a save format outlives many library releases,
# and a save-breaking change has to be visible on its own. Rules for bumping
# them are in manual/setup/57-versioning.md.
#
# The game has no version here. It does not exist yet and will be versioned by
# the UE side, on its own scale — the core version is not the game's version.

file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" KOLKHOZ_VERSION)
string(STRIP "${KOLKHOZ_VERSION}" KOLKHOZ_VERSION)
if(NOT KOLKHOZ_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must be major.minor.patch, got '${KOLKHOZ_VERSION}'")
endif()

file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION_SAVE" KOLKHOZ_SAVE_VERSION)
string(STRIP "${KOLKHOZ_SAVE_VERSION}" KOLKHOZ_SAVE_VERSION)
if(NOT KOLKHOZ_SAVE_VERSION MATCHES "^[0-9]+$")
  message(FATAL_ERROR "VERSION_SAVE must be a plain integer, got '${KOLKHOZ_SAVE_VERSION}'")
endif()

# Editing either file must re-run configure, otherwise the generated header
# keeps the old numbers and the build looks fine while shipping a lie.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/VERSION"
             "${CMAKE_CURRENT_LIST_DIR}/VERSION_SAVE")

project(kolkhoz_core
        VERSION ${KOLKHOZ_VERSION}
        DESCRIPTION "Kolkhoz Chairman simulation core"
        LANGUAGES C CXX)  # C only for third-party sources in depends/

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are forbidden. Use: cmake -B build")
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(KOLKHOZ_BUILD_TESTS   "Build console test and simulation-run apps"  ON)
option(KOLKHOZ_BUILD_GAME    "Build the game executable from src/ (phase 2)" OFF)
option(KOLKHOZ_WERROR        "Treat compiler warnings as errors"           ON)
set(KOLKHOZ_SANITIZERS "" CACHE STRING
    "Semicolon-separated sanitizers, e.g. address;undefined (Clang only)")

# ---------------------------------------------------------------------------
# Language and layout
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)            # Not 23: the engine does not support it.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # For clang-tidy and editors.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Prebuilt third-party archives dropped into libs/ are found from here.
set(KOLKHOZ_PREBUILT_LIB_DIR "${CMAKE_SOURCE_DIR}/libs")
list(APPEND CMAKE_PREFIX_PATH "${KOLKHOZ_PREBUILT_LIB_DIR}")

# ---------------------------------------------------------------------------
# Compiler cache
# ---------------------------------------------------------------------------

find_program(KOLKHOZ_CCACHE ccache)
if(KOLKHOZ_CCACHE)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${KOLKHOZ_CCACHE}")
  message(STATUS "ccache: ${KOLKHOZ_CCACHE}")
endif()

# Only Clang (Linux) and MSVC (Windows host) are supported toolchains.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|MSVC")
  message(WARNING
    "Compiler '${CMAKE_CXX_COMPILER_ID}' is not one of the supported two "
    "(Clang in the VM, MSVC on the Windows host). Portability is unverified.")
endif()

# ---------------------------------------------------------------------------
# Shared interface targets
# ---------------------------------------------------------------------------

# Public headers shared by every module, plus the generated ones.
#
# The version header is generated rather than tracked: two copies of the same
# number drift, and the one nobody edits is always the stale one. No git hash
# and no build timestamp go in — they would rewrite the header on every commit,
# defeat ccache and make builds non-reproducible for no gain.
configure_file("${CMAKE_SOURCE_DIR}/include/core_common/version.h.in"
               "${CMAKE_BINARY_DIR}/generated/include/core_common/version.h"
               @ONLY)

add_library(core_headers INTERFACE)
target_include_directories(core_headers INTERFACE
                           "${CMAKE_SOURCE_DIR}/include"
                           "${CMAKE_BINARY_DIR}/generated/include")

# Warnings, standard and sanitizer flags applied to our own code only.
add_library(core_options INTERFACE)

# Floating-point determinism. The state model promises identical results
# from the single-threaded and the multi-threaded run, and comparable ones
# from the Clang build and the MSVC build (core rules §10, §14;
# manual/52-state-model.md, core_common/quantities.h). CONTRACTION is what
# would break the second promise: a * b + c fused into one FMA keeps a wider
# intermediate, so the same source gives a different last bit depending on
# what the compiler chose — and thirty simulated years amplify a last bit
# into a different village.
#
# Both documents claimed this was already handled by build flags. It was
# not: the flags below were added 3 September 2026, after a delivery cycle
# found the promise standing on nothing (RACE-001). Latent until then —
# baseline x86-64 has no FMA — but a promise that holds only because of the
# machine it happens to run on is not a promise.
if(MSVC)
  target_compile_options(core_options INTERFACE
      /W4 /permissive- /utf-8 /Zc:preprocessor /Zc:__cplusplus /EHsc
      # Precise mode: no reassociation, no unsafe transforms. Contraction is
      # off under it unless /fp:contract is asked for, and it is not.
      /fp:precise)
  if(KOLKHOZ_WERROR)
    target_compile_options(core_options INTERFACE /WX)
  endif()
else()
  target_compile_options(core_options INTERFACE
      -Wall -Wextra -Wpedantic
      -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Woverloaded-virtual
      -Wnull-dereference -Wdouble-promotion -Wformat=2
      # Clang's own default for C++ is -ffp-contract=on. This is the half of
      # the promise that is enforceable rather than merely stated.
      -ffp-contract=off)
  if(KOLKHOZ_WERROR)
    target_compile_options(core_options INTERFACE -Werror)
  endif()
  if(KOLKHOZ_SANITIZERS)
    string(REPLACE ";" "," KOLKHOZ_SANITIZER_LIST "${KOLKHOZ_SANITIZERS}")
    target_compile_options(core_options INTERFACE
        -fsanitize=${KOLKHOZ_SANITIZER_LIST} -fno-omit-frame-pointer -g)
    target_link_options(core_options INTERFACE
        -fsanitize=${KOLKHOZ_SANITIZER_LIST})
    message(STATUS "Sanitizers: ${KOLKHOZ_SANITIZER_LIST}")
  endif()
endif()

# ---------------------------------------------------------------------------
# Helpers used by module, test and game CMakeLists files
# ---------------------------------------------------------------------------

# @brief Applies the project-wide options and include paths to a target.
function(kolkhoz_configure_target target_name)
  target_link_libraries(${target_name} PUBLIC core_headers)
  target_link_libraries(${target_name} PRIVATE core_options)
endfunction()

# @brief Declares a core module: an OBJECT target archived into `core`.
#        Call from subprojects/<name>/CMakeLists.txt.
function(kolkhoz_add_module module_name)
  add_library(${module_name} OBJECT ${ARGN})
  kolkhoz_configure_target(${module_name})
  target_include_directories(${module_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  set_property(GLOBAL APPEND PROPERTY KOLKHOZ_MODULE_TARGETS ${module_name})
endfunction()

# @brief Declares a console application under tests/ and registers it with CTest.
#        Label defaults to "run"; the unit-test helper below passes "unit".
function(kolkhoz_add_test_app app_name)
  set(one_value_args LABEL)
  cmake_parse_arguments(arg "" "${one_value_args}" "" ${ARGN})
  if(NOT arg_LABEL)
    set(arg_LABEL run)
  endif()

  add_executable(${app_name} ${arg_UNPARSED_ARGUMENTS})
  kolkhoz_configure_target(${app_name})
  target_include_directories(${app_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  if(TARGET core)
    target_link_libraries(${app_name} PRIVATE core)
  endif()

  add_test(NAME ${app_name} COMMAND ${app_name})
  set_tests_properties(${app_name} PROPERTIES LABELS "${arg_LABEL}")
endfunction()

# @brief Declares the mandatory unit test of one module.
#        Call from tests/unit/<module>/CMakeLists.txt; the application is named
#        unit_<module> and carries the CTest label "unit", so `ctest -L unit`
#        runs exactly the obligatory set.
function(kolkhoz_add_unit_test module_name)
  kolkhoz_add_test_app(unit_${module_name} LABEL unit ${ARGN})
  if(TARGET ${module_name})
    target_link_libraries(unit_${module_name} PRIVATE ${module_name})
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------

add_subdirectory(depends)      # third-party sources
add_subdirectory(subprojects)  # our modules, archived into `core`

if(KOLKHOZ_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

add_subdirectory(tools)        # core_layout: the published layout report
add_subdirectory(src)          # game executable, phase 2

message(STATUS "kolkhoz_core ${PROJECT_VERSION} save=${KOLKHOZ_SAVE_VERSION} "
               "| ${CMAKE_CXX_COMPILER_ID} "
               "| ${CMAKE_BUILD_TYPE} | tests=${KOLKHOZ_BUILD_TESTS} "
               "game=${KOLKHOZ_BUILD_GAME}")
