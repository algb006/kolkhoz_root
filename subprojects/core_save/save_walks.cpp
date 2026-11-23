/// @file
/// @brief The day's walks on disk (save 229; #2072, trail_state.h TrailWalks): today's pending
///        tread and yesterday's walkers' ways - the section «walks» after «trails».

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

#include "core_common/trail_state.h"
#include "save_rows.h"
#include "save_stream.h"

namespace core {

namespace {

/// A pending pass's bytes: four ends' cells and the passes.
constexpr std::size_t kSavedPassBytes = 20;
/// A walk pair's bytes: two positions.
constexpr std::size_t kSavedPairBytes = 16;

}  // namespace

void WriteWalks(SaveSink& sink, const TrailWalks& walks) {
  ByteWriter& out = sink.Out();
  out.WriteU8(walks.pending_over_snow ? 1U : 0U);
  out.WriteU32(static_cast<std::uint32_t>(walks.pending.size()));
  for (const TrailPass& pass : walks.pending) {
    out.WriteI32(pass.a.x);
    out.WriteI32(pass.a.y);
    out.WriteI32(pass.b.x);
    out.WriteI32(pass.b.y);
    out.WriteU32(pass.passes);
  }
  for (const std::vector<WalkPair>* list : {&walks.today, &walks.yesterday}) {
    out.WriteU32(static_cast<std::uint32_t>(list->size()));
    for (const WalkPair& pair : *list) {
      out.WriteFloat(pair.from.x);
      out.WriteFloat(pair.from.y);
      out.WriteFloat(pair.to.x);
      out.WriteFloat(pair.to.y);
    }
  }
}

TrailWalks ReadWalks(LoadSource& source) {
  ByteReader& in = source.In();
  TrailWalks walks;
  const std::uint8_t snow = in.ReadU8();
  if (snow > 1U) {
    source.Fail("the walks' snow flag is neither 0 nor 1");
    return walks;
  }
  walks.pending_over_snow = snow == 1U;
  const std::uint32_t passes = in.ReadU32();
  if (static_cast<std::size_t>(passes) * kSavedPassBytes > in.Remaining()) {
    source.Fail("the walks name more pending passes than the bytes left");
    return walks;
  }
  walks.pending.reserve(passes);
  const auto key = [](const TrailPass& pass) {
    return std::tie(pass.a.x, pass.a.y, pass.b.x, pass.b.y);
  };
  for (std::uint32_t index = 0; index < passes; ++index) {
    TrailPass pass;
    pass.a.x = in.ReadI32();
    pass.a.y = in.ReadI32();
    pass.b.x = in.ReadI32();
    pass.b.y = in.ReadI32();
    pass.passes = in.ReadU32();
    const bool ordered = walks.pending.empty() || key(walks.pending.back()) < key(pass);
    const bool lesser_first = std::tie(pass.a.x, pass.a.y) < std::tie(pass.b.x, pass.b.y);
    if (pass.passes == 0 || !ordered || !lesser_first) {
      source.Fail("a pending pass has no pass, is out of key order, or has its ends reversed");
      return walks;
    }
    walks.pending.push_back(pass);
  }
  for (std::vector<WalkPair>* list : {&walks.today, &walks.yesterday}) {
    const std::uint32_t pairs = in.ReadU32();
    if (static_cast<std::size_t>(pairs) * kSavedPairBytes > in.Remaining()) {
      source.Fail("the walks name more ways than the bytes left");
      return walks;
    }
    list->reserve(pairs);
    for (std::uint32_t index = 0; index < pairs; ++index) {
      WalkPair pair;
      pair.from.x = in.ReadFloat();
      pair.from.y = in.ReadFloat();
      pair.to.x = in.ReadFloat();
      pair.to.y = in.ReadFloat();
      if (!std::isfinite(pair.from.x) || !std::isfinite(pair.from.y) || !std::isfinite(pair.to.x) ||
          !std::isfinite(pair.to.y)) {
        source.Fail("a walk's position is not a number");
        return walks;
      }
      list->push_back(pair);
    }
  }
  return walks;
}

}  // namespace core
