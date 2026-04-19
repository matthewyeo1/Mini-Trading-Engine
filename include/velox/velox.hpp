// Umbrella header for Velox trading engine public API
#pragma once

// Module-level public headers
#if defined(__has_include)
#  if __has_include("velox/feed.hpp")
#    include "velox/feed.hpp"
#  endif
#  if __has_include("velox/book.hpp")
#    include "velox/book.hpp"
#  endif
#  if __has_include("velox/matching.hpp")
#    include "velox/matching.hpp"
#  endif
#  if __has_include("velox/risk.hpp")
#    include "velox/risk.hpp"
#  endif
#  if __has_include("velox/gateway.hpp")
#    include "velox/gateway.hpp"
#  endif
#endif

namespace velox {
// Top-level API namespace. Prefer including specific module headers
// where you only need a subset of the public API.
}
