#pragma once

/// Shared time base. Injected (`now` arguments / sample-derived) so the
/// logic is fully testable on the host and free of a particular clock
/// source on target.

#include <chrono>

namespace scale {

using clock_ms = std::chrono::milliseconds;

} // namespace scale
