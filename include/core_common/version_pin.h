/// @file
/// @brief The version pin: proof that the headers a consumer compiled against
/// and the library it linked are the same publish.
/// @threading SINGLE_THREADED
/// Meant to be called once, at start-up, before the simulation exists. Not a
/// step-phase facility; nothing here touches world state.
///
/// WHY THIS EXISTS AT ALL. The delivery contour publishes a version as a
/// stored directory — publish/<version>/<Config>/{include,lib} — precisely so
/// that headers and library travel together (manual/setup/60-windows-host.md).
/// Take both from one directory and this check can never fire. It exists for
/// the case the contour cannot forbid: somebody points the include path at one
/// version and the library path at another, on a Friday evening, to make a
/// build go through.
///
/// LAYOUT.txt does not cover that case. It compares STRUCTURE SIZES, and two
/// publishes can differ in behaviour while every size agrees — 0.15.1 against
/// 0.15.2 was exactly that, and nothing said a word. A pin nobody hears is not
/// a pin (boss, 2026-09-04).
///
/// HOW IT CATCHES IT. kCoreVersionString comes from the HEADER and is baked
/// into the caller's object file when the caller is compiled. CoreLibraryVersion()
/// is compiled into the LIBRARY and returns what the library was built from.
/// One number, read from two sides of the seam; if the two sides came from
/// different publishes, they disagree.
///
/// AND THAT RESTS ON ONE PROPERTY OF version.h: the constants there are NOT
/// `inline`, so each translation unit owns its copy. Written `inline
/// constexpr` they become one entity per program, MSVC folds the library's
/// copy away, and this check passes on a mismatched pair — measured on the
/// host, at every optimisation level, before the first delivery of this file.
/// tests/unit/core_common measures the linkage with a second translation
/// unit, because it is the only half of the mechanism a single publish can
/// see.

#ifndef CORE_COMMON_VERSION_PIN_H_
#define CORE_COMMON_VERSION_PIN_H_

#include "core_common/version.h"

namespace core {

/// @brief The version string compiled INTO THE LIBRARY.
/// @return "major.minor.patch", never null; valid for the process's lifetime
///         (a string literal).
/// @note Deliberately a function and not a constant: a constant would be
///       taken from the caller's header and would agree with itself.
const char* CoreLibraryVersion();

/// @brief Whether the headers this call was compiled against and the linked
/// library are the same publish. Silent; cheap; safe to call anywhere.
/// @param headers_version The version the caller was compiled against.
///        A null pointer means "the caller cannot say", which is not a pin
///        and is reported as a mismatch.
bool CoreVersionPinHolds(const char* headers_version);

/// @brief Says the version out loud and, on a mismatch, stops the program.
/// @param headers_version The version the caller was compiled against.
/// @note Writes to stderr EITHER WAY, and the success line is not decoration:
///       silence is indistinguishable from the call never having happened,
///       which is precisely how a broken pin looks. On a mismatch it names
///       both numbers and calls std::abort() — an ERROR, not a warning,
///       because a consumer that goes on running with mismatched headers
///       reads the world state through the wrong offsets and produces
///       confident nonsense rather than a crash.
void CheckCoreVersionPin(const char* headers_version);

/// @brief The whole check in one call, for a consumer's start-up.
///
/// It is inline ON PURPOSE: being compiled in the CALLER's translation unit
/// is what lets it capture the caller's own header version. A library-side
/// function could only ever compare the library with itself.
inline void RequireCoreVersionPin() {
  CheckCoreVersionPin(kCoreVersionString);
}

}  // namespace core

#endif  // CORE_COMMON_VERSION_PIN_H_
