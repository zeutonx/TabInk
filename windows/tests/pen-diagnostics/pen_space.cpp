#include "pen_space.h"

#include <cmath>

namespace tabink::diag {

POINT PenSpace::ToDevice(POINT s) const {
    return POINT{static_cast<LONG>(std::lround((s.x - originX) / scaleX)),
                 static_cast<LONG>(std::lround((s.y - originY) / scaleY))};
}

POINT PenSpace::ToScreen(POINT d) const {
    return POINT{static_cast<LONG>(std::lround(originX + d.x * scaleX)),
                 static_cast<LONG>(std::lround(originY + d.y * scaleY))};
}

bool PenSpace::Reaches(const RECT& r) const {
    return valid && r.left >= monitor.left && r.top >= monitor.top && r.right <= monitor.right && r.bottom <= monitor.bottom;
}

std::string MonitorName(const RECT& r) {
    MONITORINFOEXA mi{};
    mi.cbSize = sizeof(mi);
    POINT c{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    if (GetMonitorInfoA(MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST), &mi)) return mi.szDevice;
    return "?";
}

PenSpace CalibratePenSpace(injection::PenInjector& inj) {
    PenSpace ps;
    POINT saved{};
    GetCursorPos(&saved);

    auto probe = [&](LONG x, LONG y, POINT* out) {
        // Two frames so the cursor settles; the first may only "enter" range.
        inj.Hover({x, y, 0});
        Sleep(30);
        inj.Hover({x, y, 0});
        Sleep(60);
        return GetCursorPos(out) != FALSE;
    };
    const POINT d1{100, 100}, d2{600, 400};
    POINT c1{}, c2{};
    const bool ok = probe(d1.x, d1.y, &c1) && probe(d2.x, d2.y, &c2);
    inj.EndAll();
    SetCursorPos(saved.x, saved.y);

    if (!ok) { ps.error = "could not read cursor position"; return ps; }
    if (c1.x == c2.x || c1.y == c2.y) {
        ps.error = "cursor did not follow injected pen hover (injection not reaching the desktop?)";
        return ps;
    }
    ps.scaleX = static_cast<double>(c2.x - c1.x) / (d2.x - d1.x);
    ps.scaleY = static_cast<double>(c2.y - c1.y) / (d2.y - d1.y);
    ps.originX = c1.x - d1.x * ps.scaleX;
    ps.originY = c1.y - d1.y * ps.scaleY;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(c1, MONITOR_DEFAULTTONEAREST), &mi);
    ps.monitor = mi.rcMonitor;
    ps.valid = true;
    return ps;
}

}  // namespace tabink::diag
