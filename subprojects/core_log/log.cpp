// Implementation of core_log (include/core_log/log.h, task O4): console
// always, file when initialized. The stream state is module-internal and
// touched only from the sim thread (header @threading); the simulation never
// reads anything back, so this state is outside the determinism domain.

#include "core_log/log.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace core {
namespace {

/// The one log file of the process; closed = console-only mode.
/// Function-local static: initialized on first use, no global-init order
/// concerns.
std::ofstream& LogFileStream() {
  static std::ofstream stream;
  return stream;
}

std::string_view LevelTag(LogLevel level) {
  switch (level) {
    case LogLevel::kInfo:
      return "[info] ";
    case LogLevel::kWarning:
      return "[warn] ";
    case LogLevel::kError:
      return "[error] ";
  }
  return "[?] ";
}

}  // namespace

bool InitLogFile(std::string_view file_path) {
  std::ofstream& file = LogFileStream();
  if (file.is_open()) {
    file.close();
  }
  file.open(std::string(file_path), std::ios::trunc);
  return file.is_open();
}

void ShutdownLogFile() {
  std::ofstream& file = LogFileStream();
  if (file.is_open()) {
    file.flush();
    file.close();
  }
}

void LogMessage(LogLevel level, std::string_view message) {
  const std::string_view tag = LevelTag(level);
  std::ostream& console = level == LogLevel::kInfo ? std::cout : std::cerr;
  console << tag << message << '\n';
  std::ofstream& file = LogFileStream();
  if (file.is_open()) {
    file << tag << message << '\n';
  }
}

}  // namespace core
