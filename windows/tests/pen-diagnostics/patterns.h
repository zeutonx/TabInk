#pragma once
// Synthetic pen test patterns for P0.3. Generated in physical screen pixels inside a region.

#include <cstdint>
#include <string>
#include <vector>

#include "replay.h"  // CmdKind

namespace tabink::diag {

struct PenStep {
    CmdKind kind;
    long x, y;           // physical screen px
    uint32_t pressure;   // 0..1024
    bool barrel = false;
    bool eraser = false;
    uint32_t delayUs;    // wait before this step
    const char* section; // label for progress output
};

struct Region { long left, top, width, height; };

// Sections: "ramp", "taps", "tiny", "glyphs", "hover", "barrel", "rapid", "eraser".
// `sampleUs` is the pacing for normal strokes (4167 ≈ 240 Hz).
std::vector<PenStep> BuildPatterns(const Region& r, uint32_t sampleUs, const std::vector<std::string>& only);

const std::vector<std::string>& AllSections();

}  // namespace tabink::diag
