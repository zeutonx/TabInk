#include "diag_target.h"

#include <algorithm>

namespace tabink::diag {

namespace {
constexpr wchar_t kClassName[] = L"TabInkPenDiagTarget";

int64_t Qpc() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
}  // namespace

bool DiagTarget::Start(int clientW, int clientH, const RECT* monitor) {
    const POINT center = monitor ? POINT{(monitor->left + monitor->right) / 2, (monitor->top + monitor->bottom) / 2}
                                 : POINT{0, 0};  // (0,0) is always on the primary monitor
    thread_ = std::thread([this, clientW, clientH, center] { Run(clientW, clientH, center); });
    while (!ready_ && !failed_) Sleep(5);
    return !failed_;
}

void DiagTarget::Stop() {
    if (HWND h = hwnd_.load()) PostMessageW(h, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
}

RECT DiagTarget::clientScreenRect() const {
    RECT rc{};
    HWND h = hwnd_.load();
    GetClientRect(h, &rc);
    POINT tl{rc.left, rc.top}, br{rc.right, rc.bottom};
    ClientToScreen(h, &tl);
    ClientToScreen(h, &br);
    return RECT{tl.x, tl.y, br.x, br.y};
}

std::vector<Frame> DiagTarget::Received() const {
    std::lock_guard<std::mutex> lock(mu_);
    return received_;
}

std::vector<Frame> DiagTarget::TakeReceived() {
    std::vector<Frame> out;
    {
        std::lock_guard<std::mutex> lock(mu_);
        out.swap(received_);
        seenFrameIds_.clear();
    }
    if (HWND h = hwnd_.load()) InvalidateRect(h, nullptr, TRUE);
    return out;
}

void DiagTarget::Run(int clientW, int clientH, POINT monitorCenter) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_TOPMOST;
    RECT r{0, 0, clientW, clientH};
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, GetDpiForSystem());

    MONITORINFO mi{sizeof(mi)};
    HMONITOR mon = MonitorFromPoint(monitorCenter, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoW(mon, &mi);
    // Size the frame for that monitor's DPI (the window is created there).
    UINT dpiX = 96, dpiY = 96;
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    if (auto fn = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor"))) {
        fn(mon, 0, &dpiX, &dpiY);
        r = RECT{0, 0, clientW, clientH};
        AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpiX);
    }
    const int w = r.right - r.left, h = r.bottom - r.top;
    const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;

    HWND hwnd = CreateWindowExW(exStyle, kClassName, L"TabInk pen diagnostics - self-test target",
                                style, x, y, w, h, nullptr, nullptr, inst, this);
    if (!hwnd) {
        failed_ = true;
        return;
    }
    // Client size may differ after DPI adjustment on another monitor; fix it up exactly.
    RECT cr{};
    GetClientRect(hwnd, &cr);
    if (cr.right != clientW || cr.bottom != clientH) {
        SetWindowPos(hwnd, nullptr, 0, 0, w + (clientW - cr.right), h + (clientH - cr.bottom),
                     SWP_NOMOVE | SWP_NOZORDER);
    }
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);
    hwnd_ = hwnd;
    ready_ = true;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    hwnd_ = nullptr;
}

LRESULT CALLBACK DiagTarget::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<DiagTarget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_POINTERENTER:
        case WM_POINTERLEAVE:
        case WM_POINTERDOWN:
        case WM_POINTERUP:
        case WM_POINTERUPDATE: {
            const UINT32 id = GET_POINTERID_WPARAM(wp);
            POINTER_INPUT_TYPE type = PT_POINTER;
            if (self) ++self->rawPointer_;
            if (!GetPointerType(id, &type)) {
                if (self) ++self->typeFailed_;
                break;
            }
            if (self && type == PT_PEN) {
                self->OnPen(msg, id);
                return 0;  // handled: no mouse promotion, no gestures
            }
            if (self) { ++self->nonPen_; self->lastNonPenType_ = type; }
            break;
        }
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            if (self) ++self->mouse_;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void DiagTarget::OnPen(UINT /*msg*/, UINT32 pointerId) {
    ++messages_;
    const int64_t now = Qpc();
    POINTER_PEN_INFO cur{};
    if (!GetPointerPenInfo(pointerId, &cur)) return;

    UINT32 count = std::max<UINT32>(1, cur.pointerInfo.historyCount);
    std::vector<POINTER_PEN_INFO> hist(count);
    if (count > 1) {
        if (!GetPointerPenInfoHistory(pointerId, &count, hist.data())) {
            count = 1;
            hist[0] = cur;
        }
    } else {
        hist[0] = cur;
    }
    historyEntries_ += count;

    std::lock_guard<std::mutex> lock(mu_);
    // History is newest-first; record oldest-first. One injected frame can be reported by
    // several messages (ENTER + UPDATE, coalesced history), so dedupe on frameId.
    for (UINT32 i = count; i-- > 0;) {
        const POINTER_PEN_INFO& p = hist[i];
        const UINT32 fid = p.pointerInfo.frameId;
        if (fid != 0) {
            sawNonZeroFrameId_ = true;
            if (!seenFrameIds_.insert(fid).second) continue;
        }
        Frame f;
        f.pointerFlags = p.pointerInfo.pointerFlags;
        f.penFlags = p.penFlags;
        f.pressure = (p.penMask & PEN_MASK_PRESSURE) ? p.pressure : 0;
        f.x = p.pointerInfo.ptPixelLocation.x;
        f.y = p.pointerInfo.ptPixelLocation.y;
        f.qpc = now;
        f.sysQpc = static_cast<int64_t>(p.pointerInfo.PerformanceCount);
        received_.push_back(f);
        DrawSegment(f);
    }
}

void DiagTarget::DrawSegment(const Frame& f) {
    HWND h = hwnd_.load();
    if (!h) return;
    POINT pt{f.x, f.y};
    ScreenToClient(h, &pt);
    const FrameKind k = Classify(f.pointerFlags);
    if (k == FrameKind::Down || k == FrameKind::Move) {
        HDC dc = GetDC(h);
        const int width = 1 + static_cast<int>(f.pressure * 4 / 1024);
        const COLORREF color = (f.penFlags & PEN_FLAG_ERASER) ? RGB(220, 40, 40)
                             : (f.penFlags & PEN_FLAG_BARREL) ? RGB(30, 110, 230) : RGB(0, 0, 0);
        HPEN pen = CreatePen(PS_SOLID, width, color);
        HGDIOBJ old = SelectObject(dc, pen);
        if (k == FrameKind::Down || !havePrev_) {
            MoveToEx(dc, pt.x, pt.y, nullptr);
            LineTo(dc, pt.x + 1, pt.y);
        } else {
            MoveToEx(dc, prev_.x, prev_.y, nullptr);
            LineTo(dc, pt.x, pt.y);
        }
        SelectObject(dc, old);
        DeleteObject(pen);
        ReleaseDC(h, dc);
        prev_ = pt;
        havePrev_ = true;
    } else {
        havePrev_ = false;
    }
}

}  // namespace tabink::diag
