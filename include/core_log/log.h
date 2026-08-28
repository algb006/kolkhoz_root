/// @file
/// @brief Logging: to the console and, once initialized, to a file.
/// @threading SINGLE_THREADED
/// Call only from the sim thread (or before the simulation exists — setup,
/// loaders, tests). Phase code does not log: the hot path stays free of I/O,
/// and worker threads never enter this module. If a later stage needs
/// worker-side diagnostics, that will be a new, buffered interface — not a
/// mutex here.
///
/// Logging never influences the simulation: no simulation code reads
/// anything back from this module, so determinism is untouched by whether
/// or where the log goes.
///
/// Plain free functions over hidden stream state — deliberately the
/// smallest thing that satisfies "simple, to file and console" (plan §3).
/// Without InitLogFile the messages go to the console alone, which is what
/// unit tests and quick runs want; ShutdownLogFile closes the file and
/// returns to console-only.
///
/// Do not log from destructors of static-duration objects: the module's own
/// stream state is a function-local static and may already be destroyed
/// during program termination.

#ifndef CORE_LOG_LOG_H_
#define CORE_LOG_LOG_H_

#include <cstdint>
#include <string_view>

namespace core {

/// @brief Message importance. kError means the operation failed; kWarning
/// means it went through but somebody should look; kInfo is narration.
enum class LogLevel : std::uint8_t {
  kInfo = 0,
  kWarning,
  kError,
};

/// @brief Opens (truncates) `file_path` and mirrors every later message
/// into it, in addition to the console.
/// @return false if the file cannot be opened; console logging continues.
bool InitLogFile(std::string_view file_path);

/// @brief Flushes and closes the log file; console logging continues.
/// Safe to call without a prior InitLogFile.
void ShutdownLogFile();

/// @brief Writes one message: "[level] message" plus a newline.
/// Errors and warnings go to stderr, info to stdout; all three are mirrored
/// to the file when one is open.
void LogMessage(LogLevel level, std::string_view message);

inline void LogInfo(std::string_view message) {
  LogMessage(LogLevel::kInfo, message);
}

inline void LogWarning(std::string_view message) {
  LogMessage(LogLevel::kWarning, message);
}

inline void LogError(std::string_view message) {
  LogMessage(LogLevel::kError, message);
}

}  // namespace core

#endif  // CORE_LOG_LOG_H_
