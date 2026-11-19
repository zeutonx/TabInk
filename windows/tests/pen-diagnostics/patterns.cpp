#include "patterns.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace tabink::diag {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct P { double x, y; };

class Builder {
public:
    Builder(const Region& r, uint32_t dt) : r_(r), dt_(dt) {}

    // Absolute pixel from fractions of the region.
    long X(double fx) const { return r_.left + std::lround(fx * r_.width); }
    long Y(double fy) const { return r_.top + std::lround(fy * r_.height); }

    void Stroke(const std::vector<P>& pts, const std::function<uint32_t(size_t, size_t)>& pressure,
                bool barrel = false, bool eraser = false, uint32_t dt = 0) {
        if (pts.empty()) return;
        const uint32_t d = dt ? dt : dt_;
        const long x0 = std::lround(pts[0].x), y0 = std::lround(pts[0].y);
        // Approach in hover (lets apps see in-range before contact, like a real pen).
        Add(CmdKind::Hover, x0, y0, 0, barrel, eraser, travel_);
        Add(CmdKind::Hover, x0, y0, 0, barrel, eraser, d);
        Add(CmdKind::Down, x0, y0, pressure(0, pts.size()), barrel, eraser, d);
        for (size_t i = 1; i < pts.size(); ++i)
            Add(CmdKind::Move, std::lround(pts[i].x), std::lround(pts[i].y), pressure(i, pts.size()), barrel, eraser, d);
        const P& last = pts.back();
        Add(CmdKind::Up, std::lround(last.x), std::lround(last.y), 0, barrel, eraser, d);
        travel_ = 8 * d;  // brief hover pause between strokes
    }

    void Tap(double x, double y, uint32_t pressure) {
        Stroke({{x, y}}, [=](size_t, size_t) { return pressure; });
    }

    void HoverPath(const std::vector<P>& pts) {
        for (const P& p : pts) Add(CmdKind::Hover, std::lround(p.x), std::lround(p.y), 0, false, false, dt_);
    }

    void Leave() {
        if (!steps.empty()) {
            const PenStep& l = steps.back();
            Add(CmdKind::Leave, l.x, l.y, 0, false, false, dt_);
        }
        travel_ = 50'000;
    }

    void Section(const char* name) { section_ = name; travel_ = 100'000; }

    std::vector<PenStep> steps;

private:
    void Add(CmdKind k, long x, long y, uint32_t p, bool barrel, bool eraser, uint32_t delay) {
        steps.push_back(PenStep{k, x, y, p, barrel, eraser, delay, section_});
    }

    Region r_;
    uint32_t dt_;
    uint32_t travel_ = 100'000;
    const char* section_ = "";
};

// Polyline resampled so consecutive samples are ~`step` px apart (realistic pen spacing).
std::vector<P> Resample(const std::vector<P>& poly, double step) {
    std::vector<P> out;
    if (poly.empty()) return out;
    out.push_back(poly[0]);
    for (size_t i = 1; i < poly.size(); ++i) {
        const double dx = poly[i].x - poly[i - 1].x, dy = poly[i].y - poly[i - 1].y;
        const int n = std::max(1, static_cast<int>(std::ceil(std::hypot(dx, dy) / step)));
        for (int k = 1; k <= n; ++k) out.push_back({poly[i - 1].x + dx * k / n, poly[i - 1].y + dy * k / n});
    }
    return out;
}

// Shape in glyph units (height 1 = `size` px), placed with top-left at (ox, oy).
std::vector<P> Shape(std::initializer_list<P> pts, double ox, double oy, double size) {
    std::vector<P> poly;
    for (const P& p : pts) poly.push_back({ox + p.x * size, oy + p.y * size});
    return Resample(poly, std::max(1.0, size / 12.0));
}

std::vector<P> Circle(double cx, double cy, double radius) {
    const int n = std::max(8, static_cast<int>(2 * kPi * radius / 1.5));
    std::vector<P> pts;
    for (int i = 0; i <= n; ++i) {
        const double a = 2 * kPi * i / n;
        pts.push_back({cx + radius * std::cos(a), cy + radius * std::sin(a)});
    }
    return pts;
}

uint32_t Mid(size_t, size_t) { return 600; }

// Small math-like glyphs: i, j, prime, minus, equals, plus, integral, x_{n+1}.
void Glyphs(Builder& b, double ox, double oy, double s) {
    double x = ox;
    auto next = [&](double w) { x += w * s; };
    // i
    b.Stroke(Shape({{0, 0.4}, {0, 1.0}}, x, oy, s), Mid); b.Tap(x, oy + 0.15 * s, 500); next(0.6);
    // j
    b.Stroke(Shape({{0.2, 0.4}, {0.2, 1.15}, {0.05, 1.3}, {-0.1, 1.2}}, x, oy, s), Mid);
    b.Tap(x + 0.2 * s, oy + 0.15 * s, 500); next(0.8);
    // x' (x then prime)
    b.Stroke(Shape({{0, 0.4}, {0.5, 1.0}}, x, oy, s), Mid);
    b.Stroke(Shape({{0.5, 0.4}, {0, 1.0}}, x, oy, s), Mid);
    b.Stroke(Shape({{0.7, 0.1}, {0.6, 0.4}}, x, oy, s), Mid); next(1.1);
    // minus, equals, plus
    b.Stroke(Shape({{0, 0.7}, {0.5, 0.7}}, x, oy, s), Mid); next(0.8);
    b.Stroke(Shape({{0, 0.6}, {0.5, 0.6}}, x, oy, s), Mid);
    b.Stroke(Shape({{0, 0.85}, {0.5, 0.85}}, x, oy, s), Mid); next(0.8);
    b.Stroke(Shape({{0, 0.7}, {0.5, 0.7}}, x, oy, s), Mid);
    b.Stroke(Shape({{0.25, 0.45}, {0.25, 0.95}}, x, oy, s), Mid); next(0.8);
    // integral
    b.Stroke(Shape({{0.45, 0.0}, {0.35, -0.05}, {0.25, 0.05}, {0.2, 0.5}, {0.15, 1.1}, {0.05, 1.25}, {-0.05, 1.15}}, x, oy, s), Mid);
    next(0.8);
    // x_{n+1}
    b.Stroke(Shape({{0, 0.4}, {0.5, 1.0}}, x, oy, s), Mid);
    b.Stroke(Shape({{0.5, 0.4}, {0, 1.0}}, x, oy, s), Mid);
    const double sx = x + 0.6 * s, sy = oy + 0.8 * s, ss = s * 0.5;  // subscript at half size
    b.Stroke(Shape({{0, 1.0}, {0, 0.5}, {0.1, 0.4}, {0.3, 0.4}, {0.4, 0.5}, {0.4, 1.0}}, sx, sy, ss), Mid);
    b.Stroke(Shape({{0.6, 0.7}, {1.0, 0.7}}, sx, sy, ss), Mid);
    b.Stroke(Shape({{0.8, 0.5}, {0.8, 0.9}}, sx, sy, ss), Mid);
    b.Stroke(Shape({{1.2, 0.5}, {1.3, 0.4}, {1.3, 1.0}}, sx, sy, ss), Mid);
}

}  // namespace

const std::vector<std::string>& AllSections() {
    static const std::vector<std::string> s = {"ramp", "taps", "tiny", "glyphs", "hover", "barrel", "rapid", "eraser"};
    return s;
}

std::vector<PenStep> BuildPatterns(const Region& r, uint32_t sampleUs, const std::vector<std::string>& only) {
    auto want = [&](const char* s) { return only.empty() || std::find(only.begin(), only.end(), s) != only.end(); };
    Builder b(r, sampleUs);
    const double W = r.width, H = r.height, L = r.left, T = r.top;

    if (want("ramp")) {  // pressure 32 → 1024 left to right, then 1024 → 32
        b.Section("ramp");
        auto line = [&](double fy) { return Resample({{L + 0.05 * W, T + fy * H}, {L + 0.95 * W, T + fy * H}}, 3.0); };
        b.Stroke(line(0.06), [](size_t i, size_t n) { return static_cast<uint32_t>(32 + 992.0 * i / std::max<size_t>(1, n - 1)); });
        b.Stroke(line(0.11), [](size_t i, size_t n) { return static_cast<uint32_t>(1024 - 992.0 * i / std::max<size_t>(1, n - 1)); });
    }
    if (want("taps")) {  // 20 single-sample taps at increasing pressure
        b.Section("taps");
        for (int i = 0; i < 20; ++i) b.Tap(L + (0.05 + 0.045 * i) * W, T + 0.18 * H, 100 + 45 * i);
    }
    if (want("tiny")) {  // dashes 1..12 px and circles r = 2..12 px
        b.Section("tiny");
        double x = L + 0.05 * W;
        const double y = T + 0.25 * H;
        for (int len : {1, 2, 3, 4, 6, 8, 12}) {
            b.Stroke(Resample({{x, y}, {x + len, y}}, 1.0), Mid);
            x += len + 14;
        }
        for (int rad : {2, 3, 4, 6, 8, 12}) {
            x += rad + 6;
            b.Stroke(Circle(x, y, rad), Mid);
            x += rad + 14;
        }
    }
    if (want("glyphs")) {  // three sizes: ~10, 20, 40 px glyph height
        b.Section("glyphs");
        Glyphs(b, L + 0.05 * W, T + 0.32 * H, 10);
        Glyphs(b, L + 0.05 * W, T + 0.40 * H, 20);
        Glyphs(b, L + 0.05 * W, T + 0.50 * H, 40);
    }
    if (want("hover")) {  // in-range path with no contact, then leave range
        b.Section("hover");
        std::vector<P> path;
        for (int i = 0; i <= 120; ++i) {
            const double fx = 0.05 + 0.9 * i / 120.0;
            path.push_back({L + fx * W, T + (0.70 + 0.03 * std::sin(fx * 6 * kPi)) * H});
        }
        b.HoverPath(path);
        b.Leave();
    }
    if (want("barrel")) {  // stroke with the side button held
        b.Section("barrel");
        std::vector<P> wave;
        for (int i = 0; i <= 80; ++i) {
            const double fx = 0.05 + 0.4 * i / 80.0;
            wave.push_back({L + fx * W, T + (0.78 + 0.02 * std::sin(fx * 8 * kPi)) * H});
        }
        b.Stroke(wave, Mid, /*barrel=*/true);
    }
    if (want("rapid")) {  // 60 short strokes at 1 kHz: stresses down/up ordering
        b.Section("rapid");
        for (int i = 0; i < 60; ++i) {
            const double x = L + (0.5 + 0.0075 * i) * W, y = T + 0.78 * H;
            b.Stroke({{x, y}, {x, y + 4}, {x, y + 8}, {x, y + 12}}, Mid, false, false, 1000);
        }
    }
    if (want("eraser")) {  // eraser stroke crossing the ramps (apps that support it should erase)
        b.Section("eraser");
        b.Stroke(Resample({{L + 0.5 * W, T + 0.03 * H}, {L + 0.5 * W, T + 0.14 * H}}, 2.0), Mid, false, /*eraser=*/true);
    }
    b.Leave();
    return b.steps;
}

}  // namespace tabink::diag
