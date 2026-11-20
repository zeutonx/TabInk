#pragma once
// A self-owned diagnostic target window. It records every pen frame Windows delivers
// (including coalesced history) so injected and received input can be compared.

#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "frame_stats.h"

namespace tabink::diag {

class DiagTarget {
public:
    // Creates a topmost window whose client area is `clientW` x `clientH` physical px,
    // centred on `monitor` (default: primary), on its own UI thread.
    bool Start(int clientW, int clientH, const RECT* monitor = nullptr);
    void Stop();
    ~DiagTarget() { Stop(); }

    HWND hwnd() const { return hwnd_; }
    RECT clientScreenRect() const;  // physical screen px

    std::vector<Frame> Received() const;
    // Returns everything received so far and starts a fresh round (also wipes the drawing).
    // Call only while the pen is out of range, so no history spans the boundary.
    std::vector<Frame> TakeReceived();
    uint64_t messages() const { return messages_; }
    uint64_t historyEntries() const { return historyEntries_; }
    bool framesHaveIds() const { return sawNonZeroFrameId_; }

    // Raw delivery counters, to tell "nothing arrived" from "arrived but not as pen".
    struct Raw { uint64_t pointerMsgs = 0, typeFailed = 0, nonPen = 0, mouseMsgs = 0; uint32_t lastNonPenType = 0; };
    Raw raw() const { return Raw{rawPointer_, typeFailed_, nonPen_, mouse_, lastNonPenType_}; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Run(int clientW, int clientH, POINT monitorCenter);
    void OnPen(UINT msg, UINT32 pointerId);
    void DrawSegment(const Frame& f);

    std::thread thread_;
    std::atomic<HWND> hwnd_{nullptr};
    std::atomic<bool> ready_{false};
    std::atomic<bool> failed_{false};

    mutable std::mutex mu_;
    std::vector<Frame> received_;
    std::unordered_set<UINT32> seenFrameIds_;
    std::atomic<uint64_t> messages_{0};
    std::atomic<uint64_t> historyEntries_{0};
    std::atomic<bool> sawNonZeroFrameId_{false};
    std::atomic<uint64_t> rawPointer_{0}, typeFailed_{0}, nonPen_{0}, mouse_{0};
    std::atomic<uint32_t> lastNonPenType_{0};
    bool havePrev_ = false;
    POINT prev_{};
};

}  // namespace tabink::diag
