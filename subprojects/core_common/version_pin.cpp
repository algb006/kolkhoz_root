// The library's half of the version pin (version_pin.h).
//
// This translation unit is compiled INTO core.lib, so the version.h it sees
// is the one generated for the library's own build. That is the whole trick:
// the header's copy of the number reaches the consumer's object file, this
// copy stays in the archive, and a mismatched pair has two answers to one
// question.

#include "core_common/version_pin.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace core {
namespace {

/// Names both numbers and stops. File-local: a caller has no reason to reach
/// the failure half on its own, and CheckCoreVersionPin is the whole contract.
[[noreturn]] void FailOnCoreVersionMismatch(const char* headers_version) {
  // stderr and not the core's log: the log belongs to core_log, and
  // core_common depends on nothing (CLAUDE.md §7). This has to work before
  // anything is set up, which is exactly when it is called.
  //
  // The return value is deliberately dropped (cert-err33-c): the next
  // statement is abort(), so a failed write to stderr leaves nothing to do
  // about it and nowhere to report it to.
  static_cast<void>(
      std::fprintf(stderr,
                   "core: version pin broken — headers %s, library %s.\n"
                   "core: these came from different publishes. Take include/ and lib/ from "
                   "the SAME publish/<version>/<Config>/ directory.\n",
                   headers_version == nullptr ? "(none)" : headers_version,
                   CoreLibraryVersion()));
  std::abort();
}

}  // namespace

const char* CoreLibraryVersion() {
  return kCoreVersionString;
}

bool CoreVersionPinHolds(const char* headers_version) {
  if (headers_version == nullptr) {
    return false;  // a caller that cannot say what it built against has no pin
  }
  return std::strcmp(headers_version, CoreLibraryVersion()) == 0;
}

void CheckCoreVersionPin(const char* headers_version) {
  if (CoreVersionPinHolds(headers_version)) {
    // The success line exists because SILENCE IS THE FAILURE MODE: a pin that
    // says nothing when it holds is indistinguishable from a pin that was
    // never called, and that is exactly what a broken one looks like.
    static_cast<void>(std::fprintf(stderr, "core: version %s\n", CoreLibraryVersion()));
    return;
  }
  FailOnCoreVersionMismatch(headers_version);
}

}  // namespace core
