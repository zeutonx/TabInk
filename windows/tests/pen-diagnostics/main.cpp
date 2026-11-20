// TabInk P0.3 synthetic pen diagnostic (docs/roadmap.md).
//
//   pen_diag info                         environment + can we create a synthetic pen?
//   pen_diag self-test [--rate HZ]        inject into our own window, verify what Windows delivers
//   pen_diag soak [--minutes 30]          go/no-go endurance: repeat the self-test patterns, check every round
//   pen_diag patterns  [--delay S] [--rate HZ] [--only a,b,...]
//                                         draw test patterns into the app you switch to
//   pen_diag replay <pen.csv> [--delay S] [--speed X]
//                                         replay an android/penlogger CSV into the app you switch to
//
// Safety (docs/threat-model.md): injects only into the window that was foreground after the
// countdown, refuses elevated/locked/unknown targets, stops on Esc, Ctrl+C or focus change,
// and always ends with pen-up + leave-range.

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "../../core/injection/pen_injector.h"
#include "diag_target.h"
#include "frame_stats.h"
#include "patterns.h"
#include "pen_space.h"
#include "replay.h"

using namespace tabink;
using namespace tabink::diag;
using injection::PenInjector;
using injection::PenPoint;

namespace {

std::atomic<bool> g_abort{false};

BOOL WINAPI OnCtrl(DWORD) {
    g_abort = true;
    return TRUE;
}

int64_t QpcFreq() {
    static const int64_t f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x.QuadPart; }();
    return f;
}
int64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
double QpcToMs(int64_t d) { return 1000.0 * static_cast<double>(d) / static_cast<double>(QpcFreq()); }

// Absolute-schedule pacer: high-resolution timer for the bulk, short spin for the tail.
class Pacer {
public:
    Pacer() : timer_(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)),
              next_(Qpc()) {}
    ~Pacer() { if (timer_) CloseHandle(timer_); }
    void Reset() { next_ = Qpc(); }
    void WaitUs(int64_t us) {
        next_ += us * QpcFreq() / 1'000'000;
        for (;;) {
            const int64_t remaining = next_ - Qpc();
            if (remaining <= 0) return;
            const double ms = QpcToMs(remaining);
            if (ms > 1.5 && timer_) {
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>((ms - 1.0) * 10'000);  // 100 ns units, relative
                SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(timer_, INFINITE);
            } else {
                YieldProcessor();
            }
        }
    }
private:
    HANDLE timer_;
    int64_t next_;
};

bool EscPressed() { return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0; }

HWND RootAt(POINT pt) {
    HWND h = WindowFromPoint(pt);
    return h ? GetAncestor(h, GA_ROOT) : nullptr;
}

// Injection is allowed only while the pen is over the chosen target (and, for external apps,
// while it stays foreground).
struct Guard {
    HWND root = nullptr;
    bool requireForeground = true;
    std::string reason;

    bool Ok(POINT pt) {
        if (g_abort) { reason = "Ctrl+C"; return false; }
        if (EscPressed()) { reason = "Esc pressed"; return false; }
        if (requireForeground && GetAncestor(GetForegroundWindow(), GA_ROOT) != root) {
            reason = "target window lost focus"; return false;
        }
        if (RootAt(pt) != root) { reason = "pen position is not over the target window"; return false; }
        return true;
    }
};

// Steps are in screen px; the synthetic pen wants its own display's px (see pen_space.h).
bool Execute(PenInjector& inj, const PenStep& s, const PenSpace& space) {
    const POINT d = space.ToDevice(POINT{s.x, s.y});
    PenPoint p{d.x, d.y, s.pressure, s.barrel, s.eraser};
    switch (s.kind) {
        case CmdKind::Hover: return inj.Hover(p);
        case CmdKind::Down: return inj.Down(p);
        case CmdKind::Move: return inj.Move(p);
        case CmdKind::Up: return inj.Up(p);
        case CmdKind::Cancel: return inj.Cancel();
        case CmdKind::Leave: return inj.LeaveRange();
    }
    return false;
}

// Runs steps with pacing and guard checks. Returns false if aborted.
bool Run(PenInjector& inj, const std::vector<PenStep>& steps, Guard& guard, const PenSpace& space) {
    Pacer pacer;
    const char* section = nullptr;
    size_t sinceCheck = 0;
    for (size_t i = 0; i < steps.size(); ++i) {
        const PenStep& s = steps[i];
        if (s.section != section && s.section && *s.section) {
            section = s.section;
            std::printf("  - %s\n", section);
        }
        pacer.WaitUs(s.delayUs);
        const bool check = s.kind == CmdKind::Down || s.kind == CmdKind::Hover || ++sinceCheck >= 16;
        if (check) {
            sinceCheck = 0;
            if (!guard.Ok(POINT{s.x, s.y})) {
                inj.EndAll();
                std::printf("ABORTED at step %zu/%zu: %s. Pen lifted and out of range.\n", i, steps.size(), guard.reason.c_str());
                return false;
            }
        }
        if (!Execute(inj, s, space) && inj.stats().framesFailed > 0 && inj.stats().framesFailed <= 5) {
            std::printf("    inject failed at step %zu (%s): error %lu\n", i, ToString(s.kind), inj.stats().lastError);
        }
    }
    inj.EndAll();
    return true;
}

void PrintInjectorStats(const PenInjector& inj) {
    const auto& st = inj.stats();
    std::printf("injector: frames ok %llu, failed %llu (last error %lu), strokes %llu, ups %llu, cancels %llu, state violations %llu\n",
                st.framesInjected, st.framesFailed, st.lastError, st.strokes, st.ups, st.cancels, st.violations);
}

std::string ProcessName(DWORD pid) {
    std::string name = "?";
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        char buf[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameA(h, 0, buf, &n)) {
            name = buf;
            const auto slash = name.find_last_of("\\/");
            if (slash != std::string::npos) name = name.substr(slash + 1);
        }
        CloseHandle(h);
    }
    return name;
}

// 1 = elevated, 0 = not elevated, -1 = unknown (treated as unsafe).
int IsElevated(DWORD pid) {
    int result = -1;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        HANDLE tok = nullptr;
        if (OpenProcessToken(h, TOKEN_QUERY, &tok)) {
            TOKEN_ELEVATION e{};
            DWORD n = 0;
            if (GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &n)) result = e.TokenIsElevated ? 1 : 0;
            CloseHandle(tok);
        }
        CloseHandle(h);
    }
    return result;
}

// OpenInputDesktop fails (access denied) while the lock screen / secure desktop is active.
bool DesktopUnlocked() {
    HDESK d = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!d) return false;
    CloseDesktop(d);
    return true;
}

// Countdown, then take the foreground window as the target. Returns nullptr if unsafe.
HWND PickExternalTarget(int delaySec, RECT* clientScreen) {
    const HWND before = GetAncestor(GetForegroundWindow(), GA_ROOT);
    std::printf("Switch to the target app (pen tool selected) now. Starting in %d s. Esc or Ctrl+C aborts.\n", delaySec);
    for (int i = delaySec; i > 0 && !g_abort; --i) {
        std::printf("  %d...\n", i);
        Sleep(1000);
    }
    if (g_abort) return nullptr;
    HWND target = GetAncestor(GetForegroundWindow(), GA_ROOT);
    if (!target || target == before || target == GetAncestor(GetConsoleWindow(), GA_ROOT)) {
        std::printf("REFUSED: foreground window did not change to another app.\n");
        return nullptr;
    }
    if (!DesktopUnlocked()) { std::printf("REFUSED: desktop is locked or on the secure desktop.\n"); return nullptr; }
    if (IsIconic(target) || !IsWindowVisible(target)) { std::printf("REFUSED: target is minimized or hidden.\n"); return nullptr; }
    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    const int elevated = IsElevated(pid);
    if (elevated != 0) {
        std::printf("REFUSED: target process is %s.\n", elevated == 1 ? "elevated (admin)" : "not inspectable (treated as elevated)");
        return nullptr;
    }
    char cls[128] = {};
    GetClassNameA(target, cls, sizeof(cls));
    RECT rc{};
    GetClientRect(target, &rc);
    POINT tl{rc.left, rc.top}, br{rc.right, rc.bottom};
    ClientToScreen(target, &tl);
    ClientToScreen(target, &br);
    *clientScreen = RECT{tl.x, tl.y, br.x, br.y};
    std::printf("Target: %s (class %s), client %ldx%ld at (%ld,%ld), DPI %u\n", ProcessName(pid).c_str(), cls,
                br.x - tl.x, br.y - tl.y, tl.x, tl.y, GetDpiForWindow(target));
    return target;
}

std::map<std::string, std::string> ParseFlags(int argc, char** argv, int start, std::vector<std::string>* positional) {
    std::map<std::string, std::string> f;
    for (int i = start; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0) {
            const std::string v = (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) ? argv[++i] : "1";
            f[a.substr(2)] = v;
        } else if (positional) {
            positional->push_back(a);
        }
    }
    return f;
}

std::vector<std::string> SplitComma(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t c = s.find(',', start);
        const std::string part = s.substr(start, c == std::string::npos ? std::string::npos : c - start);
        if (!part.empty()) out.push_back(part);
        if (c == std::string::npos) break;
        start = c + 1;
    }
    return out;
}

uint32_t RateToUs(const std::map<std::string, std::string>& f) {
    const double hz = f.count("rate") ? std::atof(f.at("rate").c_str()) : 240.0;
    return static_cast<uint32_t>(1'000'000.0 / std::clamp(hz, 10.0, 2000.0));
}

std::unique_ptr<PenInjector> MakeInjector(injection::FrameObserver obs = nullptr) {
    DWORD err = 0;
    auto inj = PenInjector::CreateSystem(&err, std::move(obs));
    if (!inj) std::printf("CreateSyntheticPointerDevice(PT_PEN) failed: error %lu\n", err);
    return inj;
}

// ---------------------------------------------------------------------------------------

void PrintPointerDevices();

int CmdInfo() {
    OSVERSIONINFOEXW v{sizeof(v)};
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) fn(&v);
    std::printf("Windows %lu.%lu build %lu\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    std::printf("DPI awareness: per-monitor v2 = %s\n",
                AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) ? "yes" : "NO");
    std::printf("Virtual desktop: (%d,%d) %dx%d\n", GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT r, LPARAM) -> BOOL {
        UINT dx = 0, dy = 0;
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static auto fn = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor"));
        if (fn) fn(m, 0, &dx, &dy);
        MONITORINFOEXA mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(m, &mi);
        std::printf("  monitor %s: (%ld,%ld) %ldx%ld, DPI %u (%u%%)%s\n", mi.szDevice, r->left, r->top, r->right - r->left,
                    r->bottom - r->top, dx, dx * 100 / 96, (mi.dwFlags & MONITORINFOF_PRIMARY) ? " primary" : "");
        return TRUE;
    }, 0);
    std::printf("Pen/touch digitizers reported: 0x%x (SM_DIGITIZER)\n", GetSystemMetrics(SM_DIGITIZER));

    // Which monitor is the built-in panel? Windows maps pen digitizers to a display.
    UINT32 nPaths = 0, nModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) == ERROR_SUCCESS) {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPaths);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(nModes);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPaths, paths.data(), &nModes, modes.data(), nullptr) == ERROR_SUCCESS) {
            for (UINT32 i = 0; i < nPaths; ++i) {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
                src.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(src), paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id};
                DisplayConfigGetDeviceInfo(&src.header);
                const auto tech = paths[i].targetInfo.outputTechnology;
                const bool internal = tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
                                      tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
                                      tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
                std::printf("  %ls: output technology %d%s\n", src.viewGdiDeviceName, static_cast<int>(tech),
                            internal ? " (built-in panel)" : "");
            }
        }
    }

    // Injection only reaches the user's screen if we run on the interactive input desktop.
    auto objName = [](HANDLE h) {
        char buf[256] = {};
        DWORD n = 0;
        return (h && GetUserObjectInformationA(h, UOI_NAME, buf, sizeof(buf), &n)) ? std::string(buf) : std::string("?");
    };
    HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    const std::string winsta = objName(GetProcessWindowStation());
    const std::string mine = objName(GetThreadDesktop(GetCurrentThreadId()));
    const std::string inputName = input ? objName(input) : std::string("(not accessible)");
    if (input) CloseDesktop(input);
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    std::printf("Session %lu, window station %s, desktop %s, input desktop %s, console session %lu\n", session,
                winsta.c_str(), mine.c_str(), inputName.c_str(), WTSGetActiveConsoleSessionId());
    const bool interactive = winsta == "WinSta0" && mine == inputName && session == WTSGetActiveConsoleSessionId();
    std::printf("Interactive input desktop: %s\n", interactive ? "yes" : "NO - injected input will not reach the screen");

    // Integrity level and job-object UI limits can silently drop injected input.
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        BYTE buf[64] = {};
        DWORD n = 0;
        if (GetTokenInformation(tok, TokenIntegrityLevel, buf, sizeof(buf), &n)) {
            auto* til = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf);
            const DWORD rid = *GetSidSubAuthority(til->Label.Sid, *GetSidSubAuthorityCount(til->Label.Sid) - 1);
            const char* level = rid >= SECURITY_MANDATORY_HIGH_RID ? "high" : rid >= SECURITY_MANDATORY_MEDIUM_RID ? "medium"
                              : rid >= SECURITY_MANDATORY_LOW_RID ? "low" : "untrusted";
            std::printf("Integrity level: %s (0x%lx)\n", level, rid);
        }
        CloseHandle(tok);
    }
    BOOL inJob = FALSE;
    IsProcessInJob(GetCurrentProcess(), nullptr, &inJob);
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    const bool haveUi = inJob && QueryInformationJobObject(nullptr, JobObjectBasicUIRestrictions, &ui, sizeof(ui), nullptr);
    std::printf("In job object: %s%s", inJob ? "yes" : "no", haveUi ? "" : "\n");
    if (haveUi) std::printf(", UI restrictions 0x%lx%s\n", ui.UIRestrictionsClass, ui.UIRestrictionsClass ? " (input may be blocked)" : "");
    auto inj = MakeInjector();
    std::printf("Synthetic PT_PEN device: %s\n", inj ? "created OK" : "FAILED");
    PrintPointerDevices();
    return inj ? 0 : 1;
}

void PrintPointerDevices() {
    // Every pointer device and the display area Windows maps it to (the synthetic pen included).
    // A missing monitor or unexpected display rect explains "inject succeeds, cursor doesn't follow".
    UINT32 nDev = 0;
    GetPointerDevices(&nDev, nullptr);
    std::vector<POINTER_DEVICE_INFO> devs(nDev);
    if (nDev && GetPointerDevices(&nDev, devs.data())) {
        std::printf("Pointer devices (%u):\n", nDev);
        for (const auto& d : devs) {
            RECT dev{}, disp{};
            const bool rects = GetPointerDeviceRects(d.device, &dev, &disp) != FALSE;
            MONITORINFOEXA mi{};
            mi.cbSize = sizeof(mi);
            const bool haveMon = d.monitor && GetMonitorInfoA(d.monitor, &mi);
            const char* type = d.pointerDeviceType == POINTER_DEVICE_TYPE_INTEGRATED_PEN ? "integrated pen"
                             : d.pointerDeviceType == POINTER_DEVICE_TYPE_EXTERNAL_PEN ? "external pen"
                             : d.pointerDeviceType == POINTER_DEVICE_TYPE_TOUCH ? "touch"
                             : d.pointerDeviceType == POINTER_DEVICE_TYPE_TOUCH_PAD ? "touchpad" : "other";
            std::printf("  %-14s %-40ls monitor %s", type, d.productString, haveMon ? mi.szDevice : "(none)");
            if (rects) {
                std::printf(", device (%ld,%ld)-(%ld,%ld) -> display (%ld,%ld)-(%ld,%ld)%s", dev.left, dev.top, dev.right,
                            dev.bottom, disp.left, disp.top, disp.right, disp.bottom,
                            (disp.right <= disp.left || disp.bottom <= disp.top) ? "  <-- EMPTY display rect" : "");
            }
            std::printf("\n");
        }
    }
}

bool RequireReachable(const PenSpace& space, const RECT& client) {
    if (space.Reaches(client)) return true;
    std::printf("REFUSED: the target window is not fully on %s, the only display the synthetic pen can reach.\n"
                "  Move the app window onto that display, or change which display pen input maps to\n"
                "  (Control Panel > Tablet PC Settings > Setup), then try again.\n",
                MonitorName(space.monitor).c_str());
    return false;
}

// Measures the pen mapping and prints it. Returns an invalid space on failure.
PenSpace Calibrate(PenInjector& inj) {
    PenSpace s = CalibratePenSpace(inj);
    if (!s.valid) {
        std::printf("Pen mapping calibration FAILED: %s\n", s.error.c_str());
        std::printf("Windows' pointer-device mapping right now (the synthetic pen is usually the last pen listed):\n");
        PrintPointerDevices();
        std::printf("If the synthetic pen shows 'monitor (none)', Windows has no display for it: run Control Panel >\n"
                    "Tablet PC Settings > Setup > Pen input, and pick the screen to draw on. Nothing was drawn.\n");
        return s;
    }
    std::printf("Pen mapping: synthetic pen reaches %s (%ld,%ld %ldx%ld); device (0,0) = screen (%.0f,%.0f), scale %.3f x %.3f\n",
                MonitorName(s.monitor).c_str(), s.monitor.left, s.monitor.top, s.monitor.right - s.monitor.left,
                s.monitor.bottom - s.monitor.top, s.originX, s.originY, s.scaleX, s.scaleY);
    return s;
}

int CmdSelfTest(const std::map<std::string, std::string>& flags) {
    std::vector<Frame> sent;
    PenSpace space;
    auto inj = MakeInjector([&sent, &space](const POINTER_TYPE_INFO& info, bool ok) {
        if (!ok || !space.valid) return;
        Frame f;
        f.pointerFlags = info.penInfo.pointerInfo.pointerFlags;
        f.penFlags = info.penInfo.penFlags;
        f.pressure = info.penInfo.pressure;
        const POINT s = space.ToScreen(info.penInfo.pointerInfo.ptPixelLocation);  // compare in screen px
        f.x = s.x;
        f.y = s.y;
        f.qpc = Qpc();
        sent.push_back(f);
    });
    if (!inj) return 1;
    space = Calibrate(*inj);
    if (!space.valid) return 1;
    sent.clear();  // calibration hovers are not part of the test

    DiagTarget target;
    if (!target.Start(1200, 800, &space.monitor)) { std::printf("Could not create the target window.\n"); return 1; }
    Sleep(300);  // let the window show and become topmost
    const RECT rc = target.clientScreenRect();
    std::printf("Self-test target: client %ldx%ld at (%ld,%ld) on %s\n", rc.right - rc.left, rc.bottom - rc.top, rc.left,
                rc.top, MonitorName(rc).c_str());
    if (!space.Reaches(rc)) { std::printf("Target window is not fully on the pen's display.\n"); return 1; }

    const Region region{rc.left + 20, rc.top + 20, (rc.right - rc.left) - 40, (rc.bottom - rc.top) - 40};
    const auto only = flags.count("only") ? SplitComma(flags.at("only")) : std::vector<std::string>{};
    const auto steps = BuildPatterns(region, RateToUs(flags), only);
    std::printf("Injecting %zu steps at %.0f Hz (Esc aborts)...\n", steps.size(), 1e6 / RateToUs(flags));
    Guard guard{target.hwnd(), /*requireForeground=*/false, {}};
    const int64_t t0 = Qpc();
    const bool completed = Run(*inj, steps, guard, space);
    const double elapsed = QpcToMs(Qpc() - t0);
    Sleep(500);  // let the target drain its queue

    const auto received = target.Received();
    PrintInjectorStats(*inj);
    std::printf("target: %llu WM_POINTER messages, %llu history entries, %zu unique frames, frameIds %s; injection took %.0f ms\n",
                target.messages(), target.historyEntries(), received.size(),
                target.framesHaveIds() ? "present" : "ABSENT (dedupe disabled)", elapsed);

    const auto raw = target.raw();
    std::printf("target raw: %llu WM_POINTER* (type lookup failed %llu, non-pen %llu, last non-pen type %u), %llu mouse msgs\n",
                raw.pointerMsgs, raw.typeFailed, raw.nonPen, raw.lastNonPenType, raw.mouseMsgs);
    std::printf("foreground is target: %s\n", GetAncestor(GetForegroundWindow(), GA_ROOT) == target.hwnd() ? "yes" : "no");

    const Comparison cmp = Compare(sent, received);
    std::printf("%s\n", cmp.detail.c_str());

    // Injection → window-procedure latency, from the system timestamp on each delivered frame.
    std::vector<double> lat;
    for (const Frame& f : received) if (f.sysQpc > 0 && f.qpc >= f.sysQpc) lat.push_back(QpcToMs(f.qpc - f.sysQpc));
    if (!lat.empty()) {
        std::sort(lat.begin(), lat.end());
        auto pct = [&](double p) { return lat[std::min(lat.size() - 1, static_cast<size_t>(p / 100.0 * lat.size()))]; };
        std::printf("system timestamp -> handler latency: p50 %.2f ms, p95 %.2f ms, max %.2f ms (n=%zu)\n",
                    pct(50), pct(95), lat.back(), lat.size());
    } else {
        std::printf("system timestamp -> handler latency: not available (PerformanceCount not set)\n");
    }

    const FrameCounts rc2 = Count(received);
    struct Check { const char* name; bool ok; };
    const Check checks[] = {
        {"run completed (not aborted)", completed},
        {"no failed injections", inj->stats().framesFailed == 0},
        {"no state-machine violations", inj->stats().violations == 0},
        {"down/move/up/cancel counts match", cmp.countsMatch},
        {"every down has an up", rc2.down == rc2.up + rc2.cancel},
        {"contact positions exact", cmp.positionMismatches == 0 && cmp.contactCompared > 0},
        {"contact pressure within 1/1024", cmp.pressureMismatches == 0 && cmp.contactCompared > 0},
        {"pressure range reaches 1024", rc2.maxPressure >= 1000},
        {"hover frames delivered", rc2.hover > 0},
        {"barrel button delivered", rc2.barrel > 0},
        {"eraser flag delivered", rc2.eraser > 0},
    };
    bool all = true;
    std::printf("\nSELF-TEST\n");
    for (const auto& c : checks) {
        std::printf("  [%s] %s\n", c.ok ? "PASS" : "FAIL", c.name);
        all = all && c.ok;
    }
    std::printf("RESULT: %s\n", all ? "PASS" : "FAIL");
    if (flags.count("keep")) {
        std::printf("Window left open (--keep). Press Enter to close.\n");
        std::getchar();
    }
    target.Stop();
    return all ? 0 : 2;
}

// Go/no-go endurance run (roadmap P0): draws small math notation into our own window for
// --minutes and checks every round for loss, offsets and broken pen-up. The pen moves the
// cursor on the pen's display the whole time, so run it while you are away from the PC.
int CmdSoak(const std::map<std::string, std::string>& flags) {
    const double minutes = flags.count("minutes") ? std::max(0.1, std::atof(flags.at("minutes").c_str())) : 30.0;
    const long maxRounds = flags.count("rounds") ? std::max(1L, std::atol(flags.at("rounds").c_str())) : 0;
    const uint32_t sampleUs = RateToUs(flags);

    std::vector<Frame> sent;
    PenSpace space;
    auto inj = MakeInjector([&sent, &space](const POINTER_TYPE_INFO& info, bool ok) {
        if (!ok || !space.valid) return;
        Frame f;
        f.pointerFlags = info.penInfo.pointerInfo.pointerFlags;
        f.penFlags = info.penInfo.penFlags;
        f.pressure = info.penInfo.pressure;
        const POINT s = space.ToScreen(info.penInfo.pointerInfo.ptPixelLocation);
        f.x = s.x;
        f.y = s.y;
        f.qpc = Qpc();
        sent.push_back(f);
    });
    if (!inj) return 1;
    space = Calibrate(*inj);
    if (!space.valid) return 1;

    DiagTarget target;
    if (!target.Start(1200, 800, &space.monitor)) { std::printf("Could not create the target window.\n"); return 1; }
    Sleep(300);
    const RECT rc = target.clientScreenRect();
    if (!space.Reaches(rc)) { std::printf("Target window is not fully on the pen's display.\n"); return 1; }
    target.TakeReceived();
    sent.clear();

    FILE* log = nullptr;
    if (flags.count("log")) {
        // Counts only: no coordinates, no screen content.
        if (fopen_s(&log, flags.at("log").c_str(), "w") != 0) log = nullptr;
        if (log) std::fprintf(log, "round,elapsed_s,ok,contact_sent,contact_received,position_mismatches,pressure_mismatches,counts_match,balanced,inject_failures\n");
        else std::printf("Cannot write %s; continuing without a log.\n", flags.at("log").c_str());
    }

    if (maxRounds) std::printf("Soak: %ld rounds", maxRounds);
    else std::printf("Soak: %g min", minutes);
    std::printf(" at %.0f Hz into our own window on %s. Esc stops.\n", 1e6 / sampleUs, MonitorName(rc).c_str());
    const std::vector<std::string> sections = {"ramp", "taps", "tiny", "glyphs", "barrel", "rapid", "eraser"};
    SoakTotals totals;
    std::vector<double> lat;
    Guard guard{target.hwnd(), /*requireForeground=*/false, {}};
    const int64_t t0 = Qpc();
    bool aborted = false;
    for (long round = 0;; ++round) {
        const double elapsedS = QpcToMs(Qpc() - t0) / 1000.0;
        if (maxRounds ? round >= maxRounds : elapsedS >= minutes * 60) break;

        // Shift every round so strokes don't land on identical pixels.
        const long jx = (round * 7) % 24, jy = (round * 5) % 16;
        const Region region{rc.left + 20 + jx, rc.top + 20 + jy, (rc.right - rc.left) - 64, (rc.bottom - rc.top) - 56};
        const auto steps = BuildPatterns(region, sampleUs, sections);
        const uint64_t failedBefore = inj->stats().framesFailed;
        sent.clear();
        const int64_t r0 = Qpc();
        // Run() prints section names; keep the soak output to one line per round.
        Pacer pacer;
        size_t sinceCheck = 0;
        for (const PenStep& s : steps) {
            pacer.WaitUs(s.delayUs);
            if (s.kind == CmdKind::Down || s.kind == CmdKind::Hover || ++sinceCheck >= 16) {
                sinceCheck = 0;
                if (!guard.Ok(POINT{s.x, s.y})) { aborted = true; break; }
            }
            Execute(*inj, s, space);
        }
        inj->EndAll();
        Sleep(300);  // let the window drain
        const auto received = target.TakeReceived();
        if (aborted) {
            std::printf("ABORTED in round %ld: %s. Pen lifted and out of range. Partial round not counted.\n", round + 1,
                        guard.reason.c_str());
            break;
        }
        for (const Frame& f : received) if (f.sysQpc > 0 && f.qpc >= f.sysQpc) lat.push_back(QpcToMs(f.qpc - f.sysQpc));
        const SoakRound r = totals.AddRound(sent, received, inj->stats().framesFailed - failedBefore);
        const double now = QpcToMs(Qpc() - t0) / 1000.0;
        std::printf("  round %4ld  %02d:%02d  contact %llu/%llu  pos %llu  pres %llu  %s%s%s  (%.1f s)\n", round + 1,
                    static_cast<int>(now) / 60, static_cast<int>(now) % 60, r.contactCompared, r.contactSent,
                    r.positionMismatches, r.pressureMismatches, r.ok ? "ok" : "FAIL",
                    r.countsMatch ? "" : " counts-differ", r.balanced ? "" : " down/up-unbalanced", QpcToMs(Qpc() - r0) / 1000.0);
        if (log) {
            std::fprintf(log, "%ld,%.1f,%d,%llu,%llu,%llu,%llu,%d,%d,%llu\n", round + 1, now, r.ok ? 1 : 0, r.contactSent,
                         r.contactCompared, r.positionMismatches, r.pressureMismatches, r.countsMatch ? 1 : 0,
                         r.balanced ? 1 : 0, r.injectFailures);
            std::fflush(log);
        }
    }
    if (log) std::fclose(log);
    const double total = QpcToMs(Qpc() - t0) / 1000.0;

    std::printf("\nSOAK SUMMARY (%.1f min, %llu rounds)\n", total / 60, totals.rounds);
    PrintInjectorStats(*inj);
    std::printf("  rounds failed %llu (count mismatch %llu, down/up unbalanced %llu)\n", totals.roundsFailed,
                totals.countMismatchRounds, totals.unbalancedRounds);
    std::printf("  contact frames %llu sent, %llu compared; position mismatches %llu (max dx %ld, dy %ld), pressure mismatches %llu\n",
                totals.contactSent, totals.contactCompared, totals.positionMismatches, totals.maxDx, totals.maxDy,
                totals.pressureMismatches);
    std::printf("  strokes received: down %llu, up %llu, cancel %llu\n", totals.downs, totals.ups, totals.cancels);
    if (!lat.empty()) {
        std::sort(lat.begin(), lat.end());
        auto pct = [&](double p) { return lat[std::min(lat.size() - 1, static_cast<size_t>(p / 100.0 * lat.size()))]; };
        std::printf("  injection -> window latency: p50 %.2f ms, p95 %.2f ms, p99 %.2f ms, max %.2f ms (n=%zu)\n", pct(50),
                    pct(95), pct(99), lat.back(), lat.size());
    }
    const bool fullLength = maxRounds ? totals.rounds >= static_cast<uint64_t>(maxRounds) : total >= minutes * 60 - 1;
    const bool pass = !aborted && fullLength && totals.ok() && inj->stats().violations == 0;
    std::printf("RESULT: %s%s\n", pass ? "PASS" : aborted ? "INCOMPLETE" : "FAIL",
                (!aborted && !maxRounds && minutes < 30) ? " (shorter than the 30-minute go/no-go run)" : "");
    target.Stop();
    return pass ? 0 : 2;
}

int CmdPatterns(const std::map<std::string, std::string>& flags) {
    const int delay = flags.count("delay") ? std::atoi(flags.at("delay").c_str()) : 5;
    const auto only = flags.count("only") ? SplitComma(flags.at("only")) : std::vector<std::string>{};
    for (const auto& s : only) {
        const auto& all = AllSections();
        if (std::find(all.begin(), all.end(), s) == all.end()) { std::printf("Unknown section '%s'\n", s.c_str()); return 1; }
    }
    auto inj = MakeInjector();
    if (!inj) return 1;
    const PenSpace space = Calibrate(*inj);
    if (!space.valid) return 1;
    RECT rc{};
    HWND target = PickExternalTarget(delay, &rc);
    if (!target) return 1;
    if (!RequireReachable(space, rc)) return 1;
    const long w = rc.right - rc.left, h = rc.bottom - rc.top;
    const Region region{rc.left + w / 20, rc.top + h / 20, w - w / 10, h - h / 10};
    const auto steps = BuildPatterns(region, RateToUs(flags), only);
    std::printf("Drawing %zu steps...\n", steps.size());
    Guard guard{target, true, {}};
    const bool ok = Run(*inj, steps, guard, space);
    PrintInjectorStats(*inj);
    std::printf("%s. Now check the app: see docs in windows/tests/pen-diagnostics/README.md.\n", ok ? "Done" : "Stopped early");
    return ok ? 0 : 2;
}

int CmdReplay(const std::string& path, const std::map<std::string, std::string>& flags) {
    std::ifstream in(path);
    if (!in) { std::printf("Cannot open %s\n", path.c_str()); return 1; }
    const ReplayPlan plan = ParsePenLog(in);
    if (!plan.error.empty()) { std::printf("Parse error: %s\n", plan.error.c_str()); return 1; }
    std::printf("Log: %llu rows, %llu pen rows, %zu commands, %llu skipped, %llu repairs, view %dx%d\n",
                plan.rowsRead, plan.penRows, plan.cmds.size(), plan.skippedRows, plan.repairs, plan.viewWidth, plan.viewHeight);
    if (plan.cmds.empty()) { std::printf("Nothing to replay.\n"); return 1; }

    const int delay = flags.count("delay") ? std::atoi(flags.at("delay").c_str()) : 5;
    const double speed = flags.count("speed") ? std::max(0.05, std::atof(flags.at("speed").c_str())) : 1.0;
    auto inj = MakeInjector();
    if (!inj) return 1;
    const PenSpace space = Calibrate(*inj);
    if (!space.valid) return 1;
    RECT rc{};
    HWND target = PickExternalTarget(delay, &rc);
    if (!target) return 1;
    if (!RequireReachable(space, rc)) return 1;

    const Rect t{rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top};
    std::vector<PenStep> steps;
    steps.reserve(plan.cmds.size());
    int64_t prev = plan.cmds.front().timeNs;
    for (const PenCmd& c : plan.cmds) {
        long x = 0, y = 0;
        MapViewToTarget(c.x, c.y, plan.viewWidth, plan.viewHeight, t, &x, &y);
        // Real timing from the tablet, scaled by --speed; long idle gaps capped at 1 s.
        int64_t dtUs = std::max<int64_t>(0, (c.timeNs - prev) / 1000);
        dtUs = std::min<int64_t>(static_cast<int64_t>(dtUs / speed), 1'000'000);
        prev = c.timeNs;
        steps.push_back(PenStep{c.kind, x, y, ToWindowsPressure(c.pressure), c.barrel, c.eraser,
                                static_cast<uint32_t>(dtUs), "replay"});
    }
    Guard guard{target, true, {}};
    const bool ok = Run(*inj, steps, guard, space);
    PrintInjectorStats(*inj);
    return ok ? 0 : 2;
}

// Narrows down "injection succeeds but nothing arrives": does the cursor follow injected
// pen hover, and do ordinary SendInput mouse events reach the same window?
int CmdProbe() {
    DiagTarget target;
    if (!target.Start(600, 400)) return 1;
    Sleep(300);
    const RECT rc = target.clientScreenRect();
    const POINT c{(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
    POINT saved{};
    GetCursorPos(&saved);

    UINT32 nDev = 0;
    GetPointerDevices(&nDev, nullptr);
    std::printf("pointer devices before: %u\n", nDev);
    auto inj = MakeInjector();
    if (!inj) return 1;
    GetPointerDevices(&nDev, nullptr);
    std::printf("pointer devices after creating synthetic pen: %u\n", nDev);

    POINT before{}, after{};
    GetCursorPos(&before);
    for (int i = 0; i < 20; ++i) { inj->Hover({c.x + i, c.y, 0}); Sleep(5); }
    Sleep(100);
    GetCursorPos(&after);
    std::printf("pen hover: cursor %ld,%ld -> %ld,%ld (injected around %ld,%ld)\n", before.x, before.y, after.x, after.y, c.x + 19, c.y);
    inj->Down({c.x, c.y + 20, 600});
    for (int i = 1; i < 20; ++i) { inj->Move({c.x + i * 3, c.y + 20, 600}); Sleep(5); }
    inj->Up({c.x + 60, c.y + 20});
    inj->EndAll();
    Sleep(200);
    auto r1 = target.raw();
    std::printf("after pen: pointer msgs %llu, mouse msgs %llu, ok frames %llu, failed %llu\n",
                r1.pointerMsgs, r1.mouseMsgs, inj->stats().framesInjected, inj->stats().framesFailed);

    // Coordinate sweep: where does a hover at screen x actually put the cursor?
    std::printf("hover sweep (injected -> cursor):");
    for (LONG x : {-1800L, -500L, 0L, 100L, 979L, 1900L, 2500L, 3800L}) {
        inj->Hover({x, 300, 0});
        Sleep(60);
        POINT p{};
        GetCursorPos(&p);
        std::printf("  %ld->(%ld,%ld)", x, p.x, p.y);
    }
    inj->EndAll();
    std::printf("\n");

    // Control: absolute mouse move into the window.
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    in.mi.dx = static_cast<LONG>((c.x - vx) * 65535LL / (vw - 1));
    in.mi.dy = static_cast<LONG>((c.y - vy) * 65535LL / (vh - 1));
    const UINT sent = SendInput(1, &in, sizeof(in));
    Sleep(200);
    GetCursorPos(&after);
    auto r2 = target.raw();
    std::printf("SendInput mouse: sent %u, cursor now %ld,%ld, mouse msgs %llu -> %llu, pointer msgs %llu\n", sent, after.x, after.y,
                r1.mouseMsgs, r2.mouseMsgs, r2.pointerMsgs);
    SetCursorPos(saved.x, saved.y);
    target.Stop();
    return 0;
}

void Usage() {
    std::printf(
        "TabInk pen diagnostics (P0.3)\n\n"
        "  pen_diag info\n"
        "  pen_diag self-test [--rate HZ] [--keep]\n"
        "  pen_diag soak      [--minutes 30 | --rounds N] [--rate HZ] [--log soak.csv]\n"
        "  pen_diag patterns  [--delay S] [--rate HZ] [--only ramp,taps,tiny,glyphs,hover,barrel,rapid,eraser]\n"
        "  pen_diag replay <pen.csv> [--delay S] [--speed X]\n\n"
        "Esc or Ctrl+C stops at any time; the pen is always lifted.\n");
}

}  // namespace

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleCtrlHandler(OnCtrl, TRUE);
    if (argc < 2) { Usage(); return 1; }
    const std::string cmd = argv[1];
    std::vector<std::string> pos;
    const auto flags = ParseFlags(argc, argv, 2, &pos);
    if (cmd == "info") return CmdInfo();
    if (cmd == "probe") return CmdProbe();
    if (cmd == "self-test") return CmdSelfTest(flags);
    if (cmd == "soak") return CmdSoak(flags);
    if (cmd == "patterns") return CmdPatterns(flags);
    if (cmd == "replay") {
        if (pos.empty()) { Usage(); return 1; }
        return CmdReplay(pos[0], flags);
    }
    Usage();
    return 1;
}
