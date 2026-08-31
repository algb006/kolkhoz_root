// Placeholder translation unit. It exists so that the module target and the
// dependency graph build before the first real source lands, and so that the
// archiver sees one symbol instead of warning about an empty member
// (MSVC LNK4221). Delete this file when the first real .cpp arrives — the
// implementing task of the boundary (project phase 2, after task A1's
// contract in include/core_boundary/session.h is accepted).

namespace core {

extern const char* const kCoreBoundaryPlaceholder;

const char* const kCoreBoundaryPlaceholder = "core_boundary";

}  // namespace core
