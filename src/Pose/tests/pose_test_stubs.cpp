// Minimal symbol provider so ball_detector.cpp links in the standalone pose
// test harness WITHOUT the whisper-dependent pp_debug.cpp (same pattern as
// src/Analysis/tests/imu_test_stubs.cpp): PpLogStream messages are swallowed.

#include "pp_debug.h"

PpLogStream::PpLogStream(QtMsgType t) : m_type(t) { m_dbg.emplace(&m_buf); }
PpLogStream::~PpLogStream() = default;
