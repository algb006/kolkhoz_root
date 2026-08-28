// Placeholder translation unit. It exists so that the module target and the
// dependency graph build before the first real source lands, and so that the
// archiver sees one symbol instead of warning about an empty member
// (MSVC LNK4221). Delete this file when the first real .cpp arrives.

namespace core {

extern const char* const kCoreTimePlaceholder;

const char* const kCoreTimePlaceholder = "core_time";

}  // namespace core
