#pragma once
// Turns an android/penlogger CSV into an ordered list of pen commands that can be fed to
// PenInjector. Pure C++ (no Win32) so it is unit-testable.

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace tabink::diag {

enum class CmdKind { Hover, Down, Move, Up, Cancel, Leave };

const char* ToString(CmdKind k);

struct PenCmd {
    CmdKind kind;
    int64_t timeNs;     // Android CLOCK_MONOTONIC
    float x, y;         // Android view pixels
    float pressure;     // raw MotionEvent pressure (≈0..1)
    bool barrel;
    bool eraser;
};

struct ReplayPlan {
    int viewWidth = 0;   // from the "# view WxH" header line; 0 if missing
    int viewHeight = 0;
    std::vector<PenCmd> cmds;
    uint64_t rowsRead = 0;
    uint64_t penRows = 0;
    uint64_t skippedRows = 0;  // non-pen tools, button events, unusable rows
    uint64_t repairs = 0;      // inconsistencies fixed (e.g. DOWN while already down → cancel first)
    std::string error;         // non-empty if the file could not be parsed
};

ReplayPlan ParsePenLog(std::istream& in);

struct Rect { long left, top, width, height; };

// Uniform (aspect-preserving) fit of the Android view into `target`, centred.
// Returns the target pixel for an Android view coordinate.
void MapViewToTarget(float vx, float vy, int viewW, int viewH, const Rect& target, long* outX, long* outY);

// MotionEvent pressure (≈0..1) → POINTER_PEN_INFO pressure 0..1024. Clamped.
uint32_t ToWindowsPressure(float p);

}  // namespace tabink::diag
