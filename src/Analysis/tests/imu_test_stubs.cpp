// Minimal symbol providers so the IMU driver/filter sources link in the standalone
// analyzer test harness WITHOUT pulling in the Buffer library or the
// whisper-dependent pp_debug.cpp (Track A, Phase 0.0 provisioning).
//
//   * EventBuffer::nowMicros() — referenced by ImuBase::fuseRawImu() (the per-sample
//     dt clock). The frame-parse / eulerToQuat goldens don't fuse, but the symbol is
//     needed at link time. A deterministic monotonic fake keeps any incidental fusion
//     reproducible.
//   * PpLogStream — the ppWarn()/ppInfo()/... RAII logger. The real impl
//     (pp_debug.cpp) drags in whisper/ggml; here we just swallow the message.

#include "event_buffer.h"
#include "pp_debug.h"

namespace pinpoint {
int64_t EventBuffer::nowMicros() noexcept
{
    static int64_t t = 0;
    t += 5000;          // 5 ms per call — deterministic, monotonic
    return t;
}
} // namespace pinpoint

// m_dbg writes into m_buf via QDebug; on destruction the message is simply dropped
// (no stderr, no PpMessageLog) — enough to satisfy operator<< and the linker.
PpLogStream::PpLogStream(QtMsgType t) : m_type(t) { m_dbg.emplace(&m_buf); }
PpLogStream::~PpLogStream() = default;
