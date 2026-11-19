#pragma once
// P0 finding: a synthetic PT_PEN device is mapped to ONE display (on a laptop with a pen
// digitizer, the built-in panel), and InjectSyntheticPointerInput's ptPixelLocation is
// interpreted in that display's own pixel space, not virtual-desktop coordinates.
// Points outside that display are ignored. PenSpace measures the mapping at runtime.

#include <windows.h>

#include <string>

#include "../../core/injection/pen_injector.h"

namespace tabink::diag {

struct PenSpace {
    bool valid = false;
    RECT monitor{};          // screen rect (physical px) of the display the pen reaches
    double originX = 0, originY = 0;  // screen position of device (0,0)
    double scaleX = 1, scaleY = 1;    // screen px per device unit
    std::string error;

    POINT ToDevice(POINT screen) const;
    POINT ToScreen(POINT device) const;
    bool Reaches(const RECT& screenRect) const;  // fully inside the mapped display
};

// Hovers the pen at two device points and reads where the cursor lands. Moves the cursor
// briefly and restores it. Leaves the pen out of range.
PenSpace CalibratePenSpace(injection::PenInjector& inj);

std::string MonitorName(const RECT& r);

}  // namespace tabink::diag
