#include "frame_stats.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace tabink::diag {

const char* ToString(FrameKind k) {
    switch (k) {
        case FrameKind::Down: return "down";
        case FrameKind::Move: return "move";
        case FrameKind::Up: return "up";
        case FrameKind::Cancel: return "cancel";
        case FrameKind::Hover: return "hover";
        case FrameKind::Leave: return "leave";
        case FrameKind::Other: return "other";
    }
    return "?";
}

FrameKind Classify(POINTER_FLAGS f) {
    if (f & POINTER_FLAG_DOWN) return FrameKind::Down;
    if (f & POINTER_FLAG_UP) return (f & POINTER_FLAG_CANCELED) ? FrameKind::Cancel : FrameKind::Up;
    if (f & POINTER_FLAG_INCONTACT) return FrameKind::Move;
    if (f & POINTER_FLAG_INRANGE) return FrameKind::Hover;
    if (f & POINTER_FLAG_UPDATE) return FrameKind::Leave;
    return FrameKind::Other;
}

void FrameCounts::Add(const Frame& f) {
    const FrameKind k = Classify(f.pointerFlags);
    switch (k) {
        case FrameKind::Down: ++down; break;
        case FrameKind::Move: ++move; break;
        case FrameKind::Up: ++up; break;
        case FrameKind::Cancel: ++cancel; break;
        case FrameKind::Hover: ++hover; break;
        case FrameKind::Leave: ++leave; break;
        case FrameKind::Other: ++other; break;
    }
    if (f.penFlags & PEN_FLAG_BARREL) ++barrel;
    if (f.penFlags & PEN_FLAG_ERASER) ++eraser;
    if (f.penFlags & PEN_FLAG_INVERTED) ++inverted;
    if (k == FrameKind::Down || k == FrameKind::Move) {
        maxPressure = std::max(maxPressure, f.pressure);
        if (f.pressure > 0) minContactPressure = std::min(minContactPressure, f.pressure);
    }
}

std::string FrameCounts::ToString() const {
    std::ostringstream o;
    o << "down " << down << ", move " << move << ", up " << up << ", cancel " << cancel
      << ", hover " << hover << ", leave " << leave;
    if (other) o << ", other " << other;
    o << " | barrel " << barrel << ", eraser " << eraser << ", inverted " << inverted
      << " | pressure " << (minContactPressure == UINT32_MAX ? 0 : minContactPressure) << ".." << maxPressure;
    return o.str();
}

FrameCounts Count(const std::vector<Frame>& frames) {
    FrameCounts c;
    for (const auto& f : frames) c.Add(f);
    return c;
}

static bool IsContact(const Frame& f) {
    const FrameKind k = Classify(f.pointerFlags);
    return k == FrameKind::Down || k == FrameKind::Move || k == FrameKind::Up || k == FrameKind::Cancel;
}

Comparison Compare(const std::vector<Frame>& sent, const std::vector<Frame>& received) {
    Comparison r;
    const FrameCounts s = Count(sent), v = Count(received);
    r.countsMatch = s.down == v.down && s.move == v.move && s.up == v.up && s.cancel == v.cancel;

    std::vector<const Frame*> a, b;
    for (const auto& f : sent) if (IsContact(f)) a.push_back(&f);
    for (const auto& f : received) if (IsContact(f)) b.push_back(&f);
    const size_t n = std::min(a.size(), b.size());
    r.contactCompared = n;
    for (size_t i = 0; i < n; ++i) {
        const long dx = std::labs(a[i]->x - b[i]->x), dy = std::labs(a[i]->y - b[i]->y);
        const FrameKind ka = Classify(a[i]->pointerFlags);
        // Pressure is only meaningful on down/move; up frames carry 0 by construction.
        const uint32_t dp = (ka == FrameKind::Down || ka == FrameKind::Move)
            ? static_cast<uint32_t>(std::abs(static_cast<long>(a[i]->pressure) - static_cast<long>(b[i]->pressure)))
            : 0;
        if (dx || dy) ++r.positionMismatches;
        if (dp > 1) ++r.pressureMismatches;
        r.maxDx = std::max(r.maxDx, dx);
        r.maxDy = std::max(r.maxDy, dy);
        r.maxDp = std::max(r.maxDp, dp);
    }
    std::ostringstream o;
    o << "sent:     " << s.ToString() << "\n"
      << "received: " << v.ToString() << "\n"
      << "contact frames compared " << n << " (sent " << a.size() << ", received " << b.size() << ")"
      << ", position mismatches " << r.positionMismatches << " (max dx " << r.maxDx << ", dy " << r.maxDy << ")"
      << ", pressure mismatches " << r.pressureMismatches << " (max dp " << r.maxDp << ")";
    r.detail = o.str();
    return r;
}

}  // namespace tabink::diag
