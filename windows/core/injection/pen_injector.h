#pragma once
// Synthetic Windows pen (PT_PEN) injector with a strict stroke state machine.
// architecture.md §5: CreateSyntheticPointerDevice + InjectSyntheticPointerInput, count=1,
// chronological order, explicit in-range / contact / up transitions, never lose pen-up.
//
// Coordinates are physical virtual-desktop pixels (the process must be per-monitor DPI v2).

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace tabink::injection {

struct PenPoint {
    LONG x = 0;
    LONG y = 0;
    uint32_t pressure = 0;  // 0..1024 (POINTER_PEN_INFO range)
    bool barrel = false;    // side button held
    bool eraser = false;    // eraser end / eraser tool
};

enum class PenState { OutOfRange, Hovering, InContact };

const char* ToString(PenState s);

struct InjectorStats {
    uint64_t framesInjected = 0;  // successful InjectSyntheticPointerInput calls
    uint64_t framesFailed = 0;
    uint64_t strokes = 0;          // downs injected
    uint64_t ups = 0;
    uint64_t cancels = 0;
    uint64_t violations = 0;       // calls rejected by the state machine (caller bug)
    uint64_t endFlips = 0;         // tip<->eraser changes that forced a leave/re-enter
    DWORD lastError = 0;
};

// Receives each frame. Returns true on success; on failure, sets *error.
using InjectFn = std::function<bool(const POINTER_TYPE_INFO&, DWORD* error)>;
// Optional: told about every frame after the OS call, with its result (diagnostics/logging).
using FrameObserver = std::function<void(const POINTER_TYPE_INFO&, bool ok)>;

class PenInjector {
public:
    // Real device: CreateSyntheticPointerDevice(PT_PEN, 1, POINTER_FEEDBACK_DEFAULT).
    // Returns nullptr and sets *error if the device can't be created.
    static std::unique_ptr<PenInjector> CreateSystem(DWORD* error, FrameObserver observer = nullptr);

    // Test/diagnostic seam: frames go to `fn` instead of the OS.
    explicit PenInjector(InjectFn fn);
    ~PenInjector();  // ends any open stroke (cancel) and leaves range

    PenInjector(const PenInjector&) = delete;
    PenInjector& operator=(const PenInjector&) = delete;

    // Pen in range, not touching. From OutOfRange this is the "enter" frame.
    bool Hover(const PenPoint& p);
    // Pen touches. From OutOfRange a hover frame is injected first at the same point.
    bool Down(const PenPoint& p);
    // Contact sample. Only valid while InContact.
    bool Move(const PenPoint& p);
    // Pen lifts normally at p; stays in range (Hovering).
    bool Up(const PenPoint& p);
    // Abort the stroke without committing more ink (UP|CANCELED at the last point).
    bool Cancel();
    // Pen leaves range. Cancels first if still in contact.
    bool LeaveRange();
    // Safe teardown: Cancel if in contact, then LeaveRange. Idempotent.
    void EndAll();

    PenState state() const { return state_; }
    const InjectorStats& stats() const { return stats_; }
    const PenPoint& lastPoint() const { return last_; }

private:
    bool Emit(const PenPoint& p, POINTER_FLAGS pointerFlags);
    bool Violation();

    InjectFn inject_;
    HSYNTHETICPOINTERDEVICE device_ = nullptr;
    PenState state_ = PenState::OutOfRange;
    PenPoint last_{};
    InjectorStats stats_{};
};

// Builds the POINTER_TYPE_INFO for one frame. Exposed for tests.
POINTER_TYPE_INFO MakePenFrame(const PenPoint& p, POINTER_FLAGS pointerFlags);

}  // namespace tabink::injection
