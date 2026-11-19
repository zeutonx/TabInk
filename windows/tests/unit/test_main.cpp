// Unit tests for the Windows-side pieces that don't need a real pen device.
// Minimal harness (no external framework yet; see roadmap D5).

#include <windows.h>

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "../../core/injection/pen_injector.h"
#include "../pen-diagnostics/frame_stats.h"
#include "../pen-diagnostics/patterns.h"
#include "../pen-diagnostics/replay.h"

using namespace tabink;
using namespace tabink::diag;
using injection::PenInjector;
using injection::PenPoint;
using injection::PenState;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                           \
    } while (0)

#define TEST(name) static void name()
#define RUN(name) (std::printf("%s\n", #name), name())

// --- PenInjector -----------------------------------------------------------------------

struct Recorder {
    std::vector<POINTER_TYPE_INFO> frames;
    bool failNext = false;
    injection::InjectFn Fn() {
        return [this](const POINTER_TYPE_INFO& f, DWORD* err) {
            if (failNext) { failNext = false; *err = 87; return false; }
            frames.push_back(f);
            return true;
        };
    }
    POINTER_FLAGS flags(size_t i) const { return frames[i].penInfo.pointerInfo.pointerFlags; }
};

constexpr POINTER_FLAGS kHover = POINTER_FLAG_INRANGE | POINTER_FLAG_UPDATE;
constexpr POINTER_FLAGS kDown = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN;
constexpr POINTER_FLAGS kMove = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_UPDATE;
constexpr POINTER_FLAGS kUp = POINTER_FLAG_INRANGE | POINTER_FLAG_UP;
constexpr POINTER_FLAGS kCancel = POINTER_FLAG_INRANGE | POINTER_FLAG_UP | POINTER_FLAG_CANCELED;
constexpr POINTER_FLAGS kLeave = POINTER_FLAG_UPDATE;

TEST(DownFromOutOfRangeHoversFirst) {
    Recorder r;
    PenInjector inj(r.Fn());
    CHECK(inj.Down({10, 20, 500}));
    CHECK(r.frames.size() == 2);
    CHECK(r.flags(0) == kHover);
    CHECK(r.flags(1) == kDown);
    CHECK(r.frames[0].penInfo.pressure == 0);
    CHECK(r.frames[1].penInfo.pressure == 500);
    CHECK(r.frames[1].type == PT_PEN);
    CHECK(r.frames[1].penInfo.pointerInfo.ptPixelLocation.x == 10);
    CHECK(inj.state() == PenState::InContact);
}

TEST(FullStrokeSequence) {
    Recorder r;
    {
        PenInjector inj(r.Fn());
        CHECK(inj.Hover({0, 0}));
        CHECK(inj.Down({1, 1, 100}));
        CHECK(inj.Move({2, 2, 200}));
        CHECK(inj.Up({3, 3, 300}));
        CHECK(inj.state() == PenState::Hovering);
        CHECK(inj.LeaveRange());
        CHECK(inj.state() == PenState::OutOfRange);
        CHECK(inj.stats().strokes == 1 && inj.stats().ups == 1 && inj.stats().violations == 0);
    }
    const POINTER_FLAGS want[] = {kHover, kDown, kMove, kUp, kLeave};
    CHECK(r.frames.size() == 5);
    for (size_t i = 0; i < 5 && i < r.frames.size(); ++i) CHECK(r.flags(i) == want[i]);
    CHECK(r.frames[3].penInfo.pressure == 0);  // up carries no pressure
}

TEST(IllegalTransitionsAreRejected) {
    Recorder r;
    PenInjector inj(r.Fn());
    CHECK(!inj.Move({0, 0, 1}));       // move without down
    CHECK(!inj.Up({0, 0}));            // up without down
    CHECK(!inj.Cancel());              // cancel without down
    CHECK(inj.Down({0, 0, 1}));
    CHECK(!inj.Down({0, 0, 1}));       // double down
    CHECK(!inj.Hover({0, 0}));         // hover while touching
    CHECK(inj.stats().violations == 5);
    CHECK(r.frames.size() == 2);       // only hover + down were emitted
}

TEST(DestructorLiftsOpenStroke) {
    Recorder r;
    {
        PenInjector inj(r.Fn());
        inj.Down({5, 5, 400});
        inj.Move({6, 6, 400});
    }
    CHECK(r.frames.size() == 5);
    CHECK(r.flags(3) == kCancel);
    CHECK(r.flags(4) == kLeave);
    CHECK(r.frames[3].penInfo.pointerInfo.ptPixelLocation.x == 6);  // at the last point
}

TEST(LeaveRangeFromContactCancelsFirst) {
    Recorder r;
    PenInjector inj(r.Fn());
    inj.Down({1, 1, 1});
    CHECK(inj.LeaveRange());
    CHECK(r.flags(r.frames.size() - 2) == kCancel);
    CHECK(r.flags(r.frames.size() - 1) == kLeave);
    CHECK(inj.stats().cancels == 1);
    inj.EndAll();  // idempotent
    CHECK(r.frames.size() == 4);
}

TEST(EraserAndBarrelFlags) {
    Recorder r;
    PenInjector inj(r.Fn());
    inj.Hover({0, 0, 0, true, true});
    inj.Down({0, 0, 300, true, true});
    CHECK(r.frames[0].penInfo.penFlags == (PEN_FLAG_BARREL | PEN_FLAG_INVERTED));
    CHECK(r.frames[1].penInfo.penFlags == (PEN_FLAG_BARREL | PEN_FLAG_INVERTED | PEN_FLAG_ERASER));
    CHECK(r.frames[1].penInfo.penMask == PEN_MASK_PRESSURE);
}

TEST(EraserFlipLeavesRangeFirst) {
    Recorder r;
    PenInjector inj(r.Fn());
    inj.Down({0, 0, 300});              // tip: hover, down
    inj.Up({0, 0});                     // up (still in range)
    inj.Down({5, 5, 300, false, true}); // eraser: must leave range, re-enter inverted, then down
    CHECK(r.frames.size() == 6);
    if (r.frames.size() == 6) {
        CHECK(r.flags(3) == kLeave);
        CHECK(r.flags(4) == kHover && r.frames[4].penInfo.penFlags == PEN_FLAG_INVERTED);
        CHECK(r.flags(5) == kDown);
    }
    CHECK(inj.stats().endFlips == 1 && inj.stats().violations == 0);
}

TEST(PressureIsClamped) {
    POINTER_TYPE_INFO f = injection::MakePenFrame({0, 0, 5000}, kMove);
    CHECK(f.penInfo.pressure == 1024);
    f = injection::MakePenFrame({0, 0, 700}, kHover);
    CHECK(f.penInfo.pressure == 0);  // hover never carries pressure
}

TEST(FailedUpStillLeavesContact) {
    Recorder r;
    PenInjector inj(r.Fn());
    inj.Down({0, 0, 10});
    r.failNext = true;
    CHECK(!inj.Up({0, 0}));
    CHECK(inj.state() == PenState::Hovering);  // never stuck down
    CHECK(inj.stats().framesFailed == 1 && inj.stats().lastError == 87);
}

// --- Replay parsing --------------------------------------------------------------------

static const char* kHeader =
    "# view 1000x500 unbuffered false\n"
    "sample_time_ns,receive_time_ns,event_seq,source,action,action_index,pointer_index,pointer_id,tool,"
    "x,y,pressure,distance,tilt,orientation,button_state,history_size,unbuffered\n";

static std::string Row(long long t, const char* src, const char* action, const char* tool, float x, float y,
                       float p, int buttons = 0, int actionIndex = 0, int pointerIndex = 0) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%lld,%lld,1,%s,%s,%d,%d,%d,%s,%g,%g,%g,0,0,0,%d,0,0\n", t, t, src, action,
                  actionIndex, pointerIndex, pointerIndex, tool, x, y, p, buttons);
    return buf;
}

static ReplayPlan Parse(const std::string& s) {
    std::istringstream in(s);
    return ParsePenLog(in);
}

TEST(ReplayBasicStroke) {
    const long long ms = 1'000'000;
    std::string csv = kHeader;
    csv += Row(0, "C", "HOVER_ENTER", "stylus", 10, 10, 0);
    csv += Row(5 * ms, "C", "HOVER_MOVE", "stylus", 11, 11, 0);
    csv += Row(9 * ms, "C", "HOVER_EXIT", "stylus", 12, 12, 0);  // Android: exit right before down
    csv += Row(10 * ms, "C", "DOWN", "stylus", 12, 12, 0.3f);
    csv += Row(14 * ms, "H", "MOVE", "stylus", 13, 12, 0.4f);
    csv += Row(18 * ms, "C", "MOVE", "stylus", 14, 12, 0.5f);
    csv += Row(18 * ms, "C", "MOVE", "finger", 300, 300, 0.9f, 0, 0, 1);  // palm: skipped
    csv += Row(22 * ms, "C", "UP", "stylus", 15, 12, 0.5f);
    csv += Row(400 * ms, "C", "HOVER_EXIT", "stylus", 15, 12, 0);       // real leave
    const ReplayPlan p = Parse(csv);
    CHECK(p.error.empty());
    CHECK(p.viewWidth == 1000 && p.viewHeight == 500);
    const CmdKind want[] = {CmdKind::Hover, CmdKind::Hover, CmdKind::Down, CmdKind::Move,
                            CmdKind::Move, CmdKind::Up, CmdKind::Leave};
    CHECK(p.cmds.size() == 7);
    for (size_t i = 0; i < 7 && i < p.cmds.size(); ++i) CHECK(p.cmds[i].kind == want[i]);
    CHECK(p.skippedRows == 1);
    CHECK(p.repairs == 0);
}

TEST(ReplayRepairsLostUpAndTruncatedLog) {
    std::string csv = kHeader;
    csv += Row(0, "C", "DOWN", "stylus", 1, 1, 0.5f);
    csv += Row(1, "C", "DOWN", "stylus", 2, 2, 0.5f);  // previous UP lost
    csv += Row(2, "C", "MOVE", "stylus", 3, 3, 0.5f);  // log ends mid-stroke
    const ReplayPlan p = Parse(csv);
    CHECK(p.repairs == 2);
    CHECK(p.cmds.size() == 5);
    if (p.cmds.size() == 5) {
        CHECK(p.cmds[1].kind == CmdKind::Cancel);
        CHECK(p.cmds[4].kind == CmdKind::Cancel);
    }
}

TEST(ReplayBarrelAndEraser) {
    std::string csv = kHeader;
    csv += Row(0, "C", "DOWN", "eraser", 1, 1, 0.5f, 32);
    csv += Row(1, "C", "UP", "eraser", 1, 1, 0.5f, 32);
    const ReplayPlan p = Parse(csv);
    CHECK(p.cmds.size() == 2);
    CHECK(!p.cmds.empty() && p.cmds[0].barrel && p.cmds[0].eraser);
}

TEST(ReplayMissingColumnIsAnError) {
    const ReplayPlan p = Parse("sample_time_ns,x,y\n1,2,3\n");
    CHECK(!p.error.empty());
}

TEST(MapViewLetterboxes) {
    long x = 0, y = 0;
    // 1000x500 view into a 1000x1000 target at (100,200): scale 1, 250 px bars top and bottom.
    MapViewToTarget(0, 0, 1000, 500, Rect{100, 200, 1000, 1000}, &x, &y);
    CHECK(x == 100 && y == 450);
    MapViewToTarget(1000, 500, 1000, 500, Rect{100, 200, 1000, 1000}, &x, &y);
    CHECK(x == 1100 && y == 950);
    // Negative virtual-desktop origin (monitor left of primary).
    MapViewToTarget(500, 250, 1000, 500, Rect{-1920, 0, 500, 250}, &x, &y);
    CHECK(x == -1670 && y == 125);
}

TEST(PressureConversion) {
    CHECK(ToWindowsPressure(0.0f) == 0);
    CHECK(ToWindowsPressure(-1.0f) == 0);
    CHECK(ToWindowsPressure(0.5f) == 512);
    CHECK(ToWindowsPressure(1.7f) == 1024);
}

// --- Frame comparison & patterns -------------------------------------------------------

TEST(ClassifyFrames) {
    CHECK(Classify(kDown) == FrameKind::Down);
    CHECK(Classify(kMove) == FrameKind::Move);
    CHECK(Classify(kUp) == FrameKind::Up);
    CHECK(Classify(kCancel) == FrameKind::Cancel);
    CHECK(Classify(kHover) == FrameKind::Hover);
    CHECK(Classify(kLeave) == FrameKind::Leave);
}

TEST(CompareDetectsMismatch) {
    std::vector<Frame> a = {{kDown, 0, 500, 1, 1}, {kMove, 0, 600, 2, 2}, {kUp, 0, 0, 2, 2}};
    std::vector<Frame> b = a;
    CHECK(Compare(a, b).countsMatch);
    CHECK(Compare(a, b).positionMismatches == 0);
    b[1].x = 4;
    b[1].pressure = 590;
    const Comparison c = Compare(a, b);
    CHECK(c.positionMismatches == 1 && c.maxDx == 2);
    CHECK(c.pressureMismatches == 1 && c.maxDp == 10);
    b.pop_back();
    CHECK(!Compare(a, b).countsMatch);
}

TEST(PatternsAreBalancedAndInsideRegion) {
    const Region r{-500, 100, 1000, 700};
    const auto steps = BuildPatterns(r, 4167, {});
    size_t downs = 0, ups = 0;
    bool inside = true;
    for (const auto& s : steps) {
        downs += s.kind == CmdKind::Down;
        ups += s.kind == CmdKind::Up;
        inside = inside && s.x >= r.left && s.x <= r.left + r.width && s.y >= r.top && s.y <= r.top + r.height;
    }
    CHECK(downs > 100);
    CHECK(downs == ups);
    CHECK(inside);
    CHECK(!steps.empty() && steps.front().kind == CmdKind::Hover && steps.back().kind == CmdKind::Leave);

    // Driving a recorder through the steps must never violate the state machine.
    Recorder rec;
    PenInjector inj(rec.Fn());
    for (const auto& s : steps) {
        const PenPoint p{s.x, s.y, s.pressure, s.barrel, s.eraser};
        switch (s.kind) {
            case CmdKind::Hover: inj.Hover(p); break;
            case CmdKind::Down: inj.Down(p); break;
            case CmdKind::Move: inj.Move(p); break;
            case CmdKind::Up: inj.Up(p); break;
            case CmdKind::Cancel: inj.Cancel(); break;
            case CmdKind::Leave: inj.LeaveRange(); break;
        }
    }
    CHECK(inj.stats().violations == 0);
    CHECK(inj.state() == PenState::OutOfRange);
}

TEST(PatternsOnlyFilter) {
    const auto steps = BuildPatterns(Region{0, 0, 800, 600}, 4167, {"taps"});
    size_t downs = 0;
    for (const auto& s : steps) downs += s.kind == CmdKind::Down;
    CHECK(downs == 20);
}

int main() {
    RUN(DownFromOutOfRangeHoversFirst);
    RUN(FullStrokeSequence);
    RUN(IllegalTransitionsAreRejected);
    RUN(DestructorLiftsOpenStroke);
    RUN(LeaveRangeFromContactCancelsFirst);
    RUN(EraserAndBarrelFlags);
    RUN(EraserFlipLeavesRangeFirst);
    RUN(PressureIsClamped);
    RUN(FailedUpStillLeavesContact);
    RUN(ReplayBasicStroke);
    RUN(ReplayRepairsLostUpAndTruncatedLog);
    RUN(ReplayBarrelAndEraser);
    RUN(ReplayMissingColumnIsAnError);
    RUN(MapViewLetterboxes);
    RUN(PressureConversion);
    RUN(ClassifyFrames);
    RUN(CompareDetectsMismatch);
    RUN(PatternsAreBalancedAndInsideRegion);
    RUN(PatternsOnlyFilter);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
