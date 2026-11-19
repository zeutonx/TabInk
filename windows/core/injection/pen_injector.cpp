#include "pen_injector.h"

namespace tabink::injection {

const char* ToString(PenState s) {
    switch (s) {
        case PenState::OutOfRange: return "OutOfRange";
        case PenState::Hovering: return "Hovering";
        case PenState::InContact: return "InContact";
    }
    return "?";
}

POINTER_TYPE_INFO MakePenFrame(const PenPoint& p, POINTER_FLAGS pointerFlags) {
    POINTER_TYPE_INFO info{};
    info.type = PT_PEN;
    POINTER_PEN_INFO& pen = info.penInfo;
    pen.pointerInfo.pointerType = PT_PEN;
    pen.pointerInfo.pointerId = 0;
    pen.pointerInfo.pointerFlags = pointerFlags;
    pen.pointerInfo.ptPixelLocation = POINT{p.x, p.y};
    // dwTime / PerformanceCount left 0: the system timestamps injected frames.

    const bool inContact = (pointerFlags & POINTER_FLAG_INCONTACT) != 0;
    PEN_FLAGS penFlags = PEN_FLAG_NONE;
    if (p.barrel) penFlags |= PEN_FLAG_BARREL;
    // Matches hardware: eraser end in range = INVERTED; touching = INVERTED | ERASER.
    if (p.eraser) penFlags |= inContact ? (PEN_FLAG_INVERTED | PEN_FLAG_ERASER) : PEN_FLAG_INVERTED;
    pen.penFlags = penFlags;
    pen.penMask = PEN_MASK_PRESSURE;
    pen.pressure = inContact ? (p.pressure > 1024 ? 1024u : p.pressure) : 0u;
    return info;
}

std::unique_ptr<PenInjector> PenInjector::CreateSystem(DWORD* error, FrameObserver observer) {
    HSYNTHETICPOINTERDEVICE dev = CreateSyntheticPointerDevice(PT_PEN, 1, POINTER_FEEDBACK_DEFAULT);
    if (!dev) {
        if (error) *error = GetLastError();
        return nullptr;
    }
    auto inj = std::unique_ptr<PenInjector>(new PenInjector(
        [dev, observer = std::move(observer)](const POINTER_TYPE_INFO& info, DWORD* err) {
            const bool ok = InjectSyntheticPointerInput(dev, &info, 1) != FALSE;
            if (!ok && err) *err = GetLastError();
            if (observer) observer(info, ok);
            return ok;
        }));
    inj->device_ = dev;
    return inj;
}

PenInjector::PenInjector(InjectFn fn) : inject_(std::move(fn)) {}

PenInjector::~PenInjector() {
    EndAll();
    if (device_) DestroySyntheticPointerDevice(device_);
}

bool PenInjector::Emit(const PenPoint& p, POINTER_FLAGS pointerFlags) {
    const POINTER_TYPE_INFO info = MakePenFrame(p, pointerFlags);
    DWORD err = 0;
    if (inject_(info, &err)) {
        ++stats_.framesInjected;
        last_ = p;
        return true;
    }
    ++stats_.framesFailed;
    stats_.lastError = err;
    return false;
}

bool PenInjector::Violation() {
    ++stats_.violations;
    return false;
}

bool PenInjector::Hover(const PenPoint& p) {
    if (state_ == PenState::InContact) return Violation();
    // Windows ignores a tip/eraser flip while in range (P0.3 finding): like a physical pen,
    // leave range and re-enter with the other end.
    if (state_ == PenState::Hovering && p.eraser != last_.eraser) {
        LeaveRange();
        ++stats_.endFlips;
    }
    PenPoint h = p;
    h.pressure = 0;
    const bool ok = Emit(h, POINTER_FLAG_INRANGE | POINTER_FLAG_UPDATE);
    if (ok) state_ = PenState::Hovering;
    return ok;
}

bool PenInjector::Down(const PenPoint& p) {
    if (state_ == PenState::InContact) return Violation();
    if ((state_ == PenState::OutOfRange || p.eraser != last_.eraser) && !Hover(p)) return false;
    const bool ok = Emit(p, POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN);
    if (ok) {
        state_ = PenState::InContact;
        ++stats_.strokes;
    }
    return ok;
}

bool PenInjector::Move(const PenPoint& p) {
    if (state_ != PenState::InContact) return Violation();
    return Emit(p, POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_UPDATE);
}

bool PenInjector::Up(const PenPoint& p) {
    if (state_ != PenState::InContact) return Violation();
    PenPoint u = p;
    u.pressure = 0;
    // Leave the state machine out of contact even if the OS call fails: never "stick" down.
    const bool ok = Emit(u, POINTER_FLAG_INRANGE | POINTER_FLAG_UP);
    state_ = PenState::Hovering;
    ++stats_.ups;
    return ok;
}

bool PenInjector::Cancel() {
    if (state_ != PenState::InContact) return Violation();
    PenPoint c = last_;
    c.pressure = 0;
    const bool ok = Emit(c, POINTER_FLAG_INRANGE | POINTER_FLAG_UP | POINTER_FLAG_CANCELED);
    state_ = PenState::Hovering;
    ++stats_.cancels;
    return ok;
}

bool PenInjector::LeaveRange() {
    if (state_ == PenState::OutOfRange) return true;
    bool ok = true;
    if (state_ == PenState::InContact) ok = Cancel();
    PenPoint l = last_;
    l.pressure = 0;
    ok = Emit(l, POINTER_FLAG_UPDATE) && ok;
    state_ = PenState::OutOfRange;
    return ok;
}

void PenInjector::EndAll() {
    if (state_ != PenState::OutOfRange) LeaveRange();
}

}  // namespace tabink::injection
