# Kolkhoz Chairman — simulation core.
#
# One output: the static library `core`. Modules live in subprojects/ as CMake
# OBJECT targets and are archived into that single library. See CLAUDE.md §7.
#
# The core never links, includes or knows about Unreal Engine.

cmake_minimum_required(VERSION 3.25)

project(kolkhoz_core
        VERSION 0.1.0
        DESCRIPTION "Kolkhoz Chairman simulation core"
        LANGUAGES CXX)

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

# Public headers shared by every module.
add_library(core_headers INTERFACE)
target_include_directories(core_headers INTERFACE "${CMAKE_SOURCE_DIR}/include")

# Warnings, standard and sanitizer flags applied to our own code only.
add_library(core_options INTERFACE)

if(MSVC)
  target_compile_options(core_options INTERFACE
      /W4 /permissive- /utf-8 /Zc:preprocessor /Zc:__cplusplus /EHsc)
  if(KOLKHOZ_WERROR)
    target_compile_options(core_options INTERFACE /WX)
  endif()
else()
  target_compile_options(core_options INTERFACE
      -Wall -Wextra -Wpedantic
      -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Woverloaded-virtual
      -Wnull-dereference -Wdouble-promotion -Wformat=2)
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
function(kolkhoz_add_test_app app_name)
  add_executable(${app_name} ${ARGN})
  kolkhoz_configure_target(${app_name})
  if(TARGET core)
    target_link_libraries(${app_name} PRIVATE core)
  endif()
  add_test(NAME ${app_name} COMMAND ${app_name})
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

add_subdirectory(src)          # game executable, phase 2

message(STATUS "kolkhoz_core ${PROJECT_VERSION} | ${CMAKE_CXX_COMPILER_ID} "
               "| ${CMAKE_BUILD_TYPE} | tests=${KOLKHOZ_BUILD_TESTS} "
               "game=${KOLKHOZ_BUILD_GAME}")
