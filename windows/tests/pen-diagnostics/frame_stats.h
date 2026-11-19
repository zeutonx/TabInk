#pragma once
// Classifies pen frames (injected or received) and compares the two sides.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tabink::diag {

struct Frame {
    POINTER_FLAGS pointerFlags = 0;
    PEN_FLAGS penFlags = 0;
    uint32_t pressure = 0;
    LONG x = 0, y = 0;     // screen, physical px
    int64_t qpc = 0;       // when injected (sender) or handled (receiver)
    int64_t sysQpc = 0;    // receiver only: POINTER_INFO.PerformanceCount (0 if not provided)
};

enum class FrameKind { Down, Move, Up, Cancel, Hover, Leave, Other };
const char* ToString(FrameKind k);
FrameKind Classify(POINTER_FLAGS f);

struct FrameCounts {
    uint64_t down = 0, move = 0, up = 0, cancel = 0, hover = 0, leave = 0, other = 0;
    uint64_t barrel = 0, eraser = 0, inverted = 0;  // frames carrying each pen flag
    uint32_t maxPressure = 0;
    uint32_t minContactPressure = UINT32_MAX;
    void Add(const Frame& f);
    std::string ToString() const;
};

FrameCounts Count(const std::vector<Frame>& frames);

struct Comparison {
    bool countsMatch = false;
    uint64_t contactCompared = 0;
    uint64_t positionMismatches = 0;  // |dx| or |dy| > 0
    uint64_t pressureMismatches = 0;  // |dp| > 1
    long maxDx = 0, maxDy = 0;
    uint32_t maxDp = 0;
    std::string detail;
};

// Compares contact frames (Down/Move/Up/Cancel) in order, plus all category counts.
Comparison Compare(const std::vector<Frame>& sent, const std::vector<Frame>& received);

}  // namespace tabink::diag
