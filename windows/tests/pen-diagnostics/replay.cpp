#include "replay.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace tabink::diag {

const char* ToString(CmdKind k) {
    switch (k) {
        case CmdKind::Hover: return "Hover";
        case CmdKind::Down: return "Down";
        case CmdKind::Move: return "Move";
        case CmdKind::Up: return "Up";
        case CmdKind::Cancel: return "Cancel";
        case CmdKind::Leave: return "Leave";
    }
    return "?";
}

namespace {

constexpr int kButtonStylusPrimary = 32;  // MotionEvent.BUTTON_STYLUS_PRIMARY
constexpr int64_t kHoverExitBeforeDownNs = 100'000'000;  // Android sends HOVER_EXIT just before DOWN

std::vector<std::string> SplitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

}  // namespace

ReplayPlan ParsePenLog(std::istream& in) {
    ReplayPlan plan;
    std::unordered_map<std::string, size_t> col;
    std::string line;

    // Pending HOVER_EXIT: emitted only if the next pen command is not a DOWN shortly after.
    bool pendingLeave = false;
    PenCmd leaveCmd{};
    bool inContact = false;

    auto push = [&](PenCmd c) {
        if (pendingLeave) {
            const bool exitThenDown = c.kind == CmdKind::Down && c.timeNs - leaveCmd.timeNs <= kHoverExitBeforeDownNs;
            if (!exitThenDown) {
                plan.cmds.push_back(leaveCmd);
            }
            pendingLeave = false;
        }
        switch (c.kind) {
            case CmdKind::Down:
                if (inContact) {  // lost UP in the log: cancel the old stroke rather than join them
                    plan.cmds.push_back({CmdKind::Cancel, c.timeNs, c.x, c.y, 0, c.barrel, c.eraser});
                    ++plan.repairs;
                }
                inContact = true;
                break;
            case CmdKind::Move:
                if (!inContact) { ++plan.skippedRows; return; }
                break;
            case CmdKind::Up:
            case CmdKind::Cancel:
                if (!inContact) { ++plan.skippedRows; return; }
                inContact = false;
                break;
            case CmdKind::Hover:
                if (inContact) {  // hover while "down": the UP was lost
                    plan.cmds.push_back({CmdKind::Cancel, c.timeNs, c.x, c.y, 0, c.barrel, c.eraser});
                    ++plan.repairs;
                    inContact = false;
                }
                break;
            case CmdKind::Leave:
                break;
        }
        plan.cmds.push_back(c);
    };

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            // "# view WxH ..." written by penlogger's CSV header.
            constexpr char kView[] = "# view ";
            if (line.rfind(kView, 0) == 0) {
                const char* s = line.c_str() + sizeof(kView) - 1;
                char* end = nullptr;
                const long w = std::strtol(s, &end, 10);
                if (end && *end == 'x') {
                    const long h = std::strtol(end + 1, nullptr, 10);
                    if (w > 0 && h > 0) {
                        plan.viewWidth = static_cast<int>(w);
                        plan.viewHeight = static_cast<int>(h);
                    }
                }
            }
            continue;
        }
        auto f = SplitCsv(line);
        if (col.empty()) {
            for (size_t i = 0; i < f.size(); ++i) col[f[i]] = i;
            for (const char* need : {"sample_time_ns", "source", "action", "action_index", "pointer_index",
                                     "tool", "x", "y", "pressure", "button_state"}) {
                if (!col.count(need)) {
                    plan.error = std::string("missing column: ") + need;
                    return plan;
                }
            }
            continue;
        }
        ++plan.rowsRead;
        auto get = [&](const char* name) -> const std::string& {
            static const std::string empty;
            auto it = col.find(name);
            return (it == col.end() || it->second >= f.size()) ? empty : f[it->second];
        };

        const std::string& tool = get("tool");
        const bool eraser = tool == "eraser";
        if (tool != "stylus" && !eraser) { ++plan.skippedRows; continue; }
        ++plan.penRows;

        PenCmd c{};
        try {
            c.timeNs = std::stoll(get("sample_time_ns"));
            c.x = std::stof(get("x"));
            c.y = std::stof(get("y"));
            c.pressure = std::stof(get("pressure"));
            c.barrel = (std::stoi(get("button_state")) & kButtonStylusPrimary) != 0;
        } catch (...) {
            ++plan.skippedRows;
            continue;
        }
        c.eraser = eraser;
        const bool historical = get("source") == "H";
        const bool transition = !historical && get("action_index") == get("pointer_index");
        const std::string& a = get("action");

        if (a == "MOVE" || (!transition && (a == "POINTER_DOWN" || a == "POINTER_UP"))) {
            c.kind = CmdKind::Move;
        } else if (a == "DOWN" || a == "POINTER_DOWN") {
            c.kind = historical ? CmdKind::Move : CmdKind::Down;
        } else if (a == "UP" || a == "POINTER_UP") {
            c.kind = historical ? CmdKind::Move : CmdKind::Up;
        } else if (a == "CANCEL") {
            c.kind = CmdKind::Cancel;
        } else if (a == "HOVER_ENTER" || a == "HOVER_MOVE") {
            c.kind = CmdKind::Hover;
        } else if (a == "HOVER_EXIT") {
            if (historical) { c.kind = CmdKind::Hover; }
            else {
                if (pendingLeave) plan.cmds.push_back(leaveCmd);  // two exits in a row: keep the first
                c.kind = CmdKind::Leave;
                leaveCmd = c;
                pendingLeave = true;
                continue;
            }
        } else {
            ++plan.skippedRows;  // BUTTON_PRESS/RELEASE, SCROLL…; button state rides on samples
            continue;
        }
        push(c);
    }
    if (pendingLeave) plan.cmds.push_back(leaveCmd);
    if (inContact && !plan.cmds.empty()) {  // log ended mid-stroke: never leave the pen down
        PenCmd last = plan.cmds.back();
        plan.cmds.push_back({CmdKind::Cancel, last.timeNs, last.x, last.y, 0, last.barrel, last.eraser});
        ++plan.repairs;
    }
    if (plan.error.empty() && col.empty()) plan.error = "no CSV header found";
    return plan;
}

void MapViewToTarget(float vx, float vy, int viewW, int viewH, const Rect& t, long* outX, long* outY) {
    if (viewW <= 0 || viewH <= 0) {
        *outX = t.left + static_cast<long>(std::lround(vx));
        *outY = t.top + static_cast<long>(std::lround(vy));
        return;
    }
    const double scale = std::min(static_cast<double>(t.width) / viewW, static_cast<double>(t.height) / viewH);
    const double offX = (t.width - viewW * scale) / 2.0;
    const double offY = (t.height - viewH * scale) / 2.0;
    *outX = t.left + static_cast<long>(std::lround(offX + vx * scale));
    *outY = t.top + static_cast<long>(std::lround(offY + vy * scale));
}

uint32_t ToWindowsPressure(float p) {
    if (!(p > 0.0f)) return 0;
    if (p >= 1.0f) return 1024;
    return static_cast<uint32_t>(std::lround(p * 1024.0f));
}

}  // namespace tabink::diag
