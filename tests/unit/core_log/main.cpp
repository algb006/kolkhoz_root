// Unit test of core_log: console-only mode, file mirroring, shutdown.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "core_log/log.h"

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

}  // namespace

int main() {
  int failures = 0;

  // Console-only mode works without any initialization.
  core::LogInfo("console-only message");
  core::ShutdownLogFile();  // no file open — must be harmless

  // File mirroring: init, write each level, shutdown, read back.
  const std::string log_path = "unit_core_log_output.txt";
  failures += Expect(core::InitLogFile(log_path), "the log file opens");
  core::LogInfo("first");
  core::LogWarning("second");
  core::LogError("third");
  core::ShutdownLogFile();

  std::ifstream file(log_path);
  failures += Expect(file.good(), "the log file exists after shutdown");
  std::stringstream content;
  content << file.rdbuf();
  const std::string text = content.str();
  failures += Expect(text.find("[info] first") != std::string::npos, "info line is in the file");
  failures +=
      Expect(text.find("[warn] second") != std::string::npos, "warning line is in the file");
  failures += Expect(text.find("[error] third") != std::string::npos, "error line is in the file");

  // Messages after shutdown go to the console only, not the closed file.
  core::LogInfo("after shutdown");
  std::ifstream reread(log_path);
  std::stringstream content_after;
  content_after << reread.rdbuf();
  failures += Expect(content_after.str().find("after shutdown") == std::string::npos,
                     "a closed file gains no lines");

  // A second init truncates: old lines are gone.
  failures += Expect(core::InitLogFile(log_path), "the log file reopens");
  core::LogInfo("fresh");
  core::ShutdownLogFile();
  std::ifstream fresh(log_path);
  std::stringstream fresh_content;
  fresh_content << fresh.rdbuf();
  failures += Expect(fresh_content.str().find("first") == std::string::npos, "reinit truncates");
  failures +=
      Expect(fresh_content.str().find("[info] fresh") != std::string::npos, "new lines land");

  std::remove(log_path.c_str());
  if (failures == 0) {
    std::cout << "unit_core_log: all checks passed\n";
  }
  return failures;
}
