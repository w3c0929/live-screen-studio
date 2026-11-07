#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <random>
#include <vector>

#include "arrow_head.h"
#include "camera.h"
#include "cursor_motion.h"
#include "cursor_style.h"
#include "drag_pointer.h"
#include "smooth_damp.h"
#include "text_lines.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL %s:%d  %s\n", __FILE__, line, expression);
    ++g_failures;
}

void checkNear(double a, double b, double eps, const char* expression, int line) {
    if (std::abs(a - b) <= eps) return;
    std::printf("FAIL %s:%d  %s: %.17g vs %.17g (eps %g)\n", __FILE__, line, expression, a, b, eps);
    ++g_failures;
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
#define CHECK_NEAR(a, b, eps) checkNear((a), (b), (eps), #a " ~= " #b, __LINE__)

using namespace lz;

constexpr double kFrame = 1.0 / 60;

double run(ZoomCamera& camera, Point cursor, double start, double seconds,
           const std::function<void(double)>& each = {}) {
    double t = start;
    while (t < start + seconds) {
        t += kFrame;
        camera.update(cursor, t, kFrame);
        if (each) each(t);
    }
    return t;
}

void assertInsideDisplay(const ZoomCamera& camera, Size display) {
    const Rect r = camera.viewport();
    constexpr double eps = 0.001;
    CHECK(r.minX() >= -eps);
    CHECK(r.minY() >= -eps);
    CHECK(r.maxX() <= display.width + eps);
    CHECK(r.maxY() <= display.height + eps);
}

void cameraTests() {
    const Size display{2304, 1536};

    {
        std::mt19937 rng(12345);
        std::uniform_real_distribution<double> zoomDist(1, 4);
        std::uniform_real_distribution<double> xDist(-500, 3000);
        std::uniform_real_distribution<double> yDist(-500, 2000);
        for (int i = 0; i < 1000; ++i) {
            const double zoom = zoomDist(rng);
            const Point c{xDist(rng), yDist(rng)};
            const Point clamped = ViewportMath::clampCenter(c, zoom, display);
            const Rect r = ViewportMath::rect(clamped, zoom, display);
            CHECK(r.minX() >= -0.001);
            CHECK(r.minY() >= -0.001);
            CHECK(r.maxX() <= display.width + 0.001);
            CHECK(r.maxY() <= display.height + 0.001);
        }
    }

    {
        ZoomCamera camera(display);
        const Point cursor{1200, 800};
        camera.zoomIn(2, cursor, 0);
        CHECK(camera.phase() == Phase::ZoomingIn);
        run(camera, cursor, 0, 0.7, [&](double) {
            const Point p = camera.screenPoint(cursor);
            CHECK_NEAR(p.x, cursor.x, 0.01);
            CHECK_NEAR(p.y, cursor.y, 0.01);
        });
        CHECK(camera.phase() == Phase::Zoomed);
        CHECK_NEAR(camera.zoom(), 2, 1e-9);
    }

    {
        ZoomCamera camera(display);
        const Point cursor{10, 1530};
        camera.zoomIn(3, cursor, 0);
        run(camera, cursor, 0, 1, [&](double) { assertInsideDisplay(camera, display); });
        CHECK(camera.viewport().insetBy(-0.01, -0.01).contains(cursor));
    }

    {
        ZoomCamera camera(display);
        const Point cursor{300, 200};
        camera.zoomIn(2, cursor, 0);
        Point last = camera.center();
        double lastStep = 0;
        double maxAcceleration = 0;
        run(camera, cursor, 0, 1.5, [&](double) {
            const double step = hypot(camera.center(), last);
            maxAcceleration = std::max(maxAcceleration, std::abs(step - lastStep));
            last = camera.center();
            lastStep = step;
        });
        CHECK(camera.phase() == Phase::Zoomed);
        const Point settled = camera.center();
        run(camera, cursor, 1.5, 1);
        CHECK_NEAR(camera.center().x, settled.x, 0.001);
        CHECK_NEAR(camera.center().y, settled.y, 0.001);
        CHECK(maxAcceleration < 10);
    }

    {
        ZoomCamera camera(display);
        const Point start{1152, 768};
        camera.zoomIn(2, start, 0);
        double t = run(camera, start, 0, 1);
        const Point settled = camera.center();
        for (const Point offset : {Point{200, 0}, Point{-250, 150}, Point{0, -180}}) {
            const Point cursor{start.x + offset.x, start.y + offset.y};
            t = run(camera, cursor, t, 0.5);
            CHECK_NEAR(camera.center().x, settled.x, 0.001);
            CHECK_NEAR(camera.center().y, settled.y, 0.001);
        }
    }

    {
        ZoomCamera camera(display);
        camera.zoomIn(2, {1152, 768}, 0);
        double t = run(camera, {1152, 768}, 0, 1);
        const Point cursor{1700, 768};
        Point last = camera.center();
        double maxVisualSpeed = 0;
        t = run(camera, cursor, t, 3, [&](double) {
            const double step = hypot(camera.center(), last);
            maxVisualSpeed = std::max(maxVisualSpeed, step * camera.zoom() / kFrame);
            last = camera.center();
        });
        CHECK(maxVisualSpeed <= FollowPreset::calm.maxVisualSpeed + 0.5);
        CHECK(maxVisualSpeed > 0);
        (void)t;
    }

    {
        ZoomCamera camera(display);
        camera.zoomIn(2, {400, 400}, 0);
        double t = run(camera, {400, 400}, 0, 1);
        const Point far{2200, 1400};
        Point last = camera.center();
        double maxVisualSpeed = 0;
        std::optional<double> visibleAfter;
        const double start = t;
        t = run(camera, far, t, 4, [&](double now) {
            const double step = hypot(camera.center(), last);
            maxVisualSpeed = std::max(maxVisualSpeed, step * camera.zoom() / kFrame);
            last = camera.center();
            if (!visibleAfter && camera.viewport().contains(far)) visibleAfter = now - start;
        });
        const double limit = FollowPreset::calm.maxVisualSpeed * camera.config.catchUpLimit;
        CHECK(maxVisualSpeed <= limit + 0.5);
        CHECK(visibleAfter.has_value());
        CHECK(visibleAfter.value() < 1.5);
        const Point settled = ViewportMath::applySafeZone(
            camera.center(), far, camera.zoom(), display, FollowPreset::calm.margin);
        CHECK_NEAR(ViewportMath::clampCenter(settled, camera.zoom(), display).x,
                   camera.center().x, 0.5);
    }

    {
        ZoomCamera camera(display);
        const Point start{1152, 768};
        camera.zoomIn(2, start, 0);
        double t = run(camera, start, 0, 1);
        const Point crossing{1460, 768};
        int moving = 0;
        int subPixelFrames = 0;
        Point last = camera.center();
        t = run(camera, crossing, t, 4, [&](double) {
            const double step = hypot(camera.center(), last) * camera.zoom();
            if (step > 0) ++moving;
            if (step > 0 && step < 0.5) ++subPixelFrames;
            last = camera.center();
        });
        CHECK(subPixelFrames <= 1);
        const Rect vp = camera.viewport();
        CHECK_NEAR((vp.maxX() - crossing.x) / vp.width, FollowPreset::calm.settleMargin, 0.01);
        CHECK(moving < 150);
        const Point rested = camera.center();
        for (const double dx : {-60.0, 60.0, -30.0, 30.0}) {
            t = run(camera, {crossing.x + dx, 768}, t, 0.3);
        }
        CHECK(camera.center() == rested);
    }

    {
        ZoomCamera camera(display);
        camera.zoomIn(2, {1152, 768}, 0);
        double t = run(camera, {1152, 768}, 0, 1);
        const Point held = camera.center();
        camera.setFollowPaused(true);
        t = run(camera, {1720, 1100}, t, 1);
        CHECK(camera.center() == held);
        camera.setFollowPaused(false);
        run(camera, {1720, 1100}, t, 2);
        CHECK(!(camera.center() == held));
    }

    {
        ZoomCamera camera(display);
        const Point cursor{1152, 768};
        camera.zoomIn(2, cursor, 0);
        double lastLog = std::log(camera.zoom());
        double maxStep = 0;
        double t = 0;
        for (int i = 0; i < 180; ++i) {
            t += kFrame;
            if (i >= 12 && i <= 16) camera.adjustZoom(1.1);
            camera.update(cursor, t, kFrame);
            maxStep = std::max(maxStep, std::abs(std::log(camera.zoom()) - lastLog));
            lastLog = std::log(camera.zoom());
        }
        CHECK(maxStep < 0.07);
        CHECK_NEAR(camera.zoom(), 2 * std::pow(1.1, 5), 1e-6);
    }

    {
        ZoomCamera camera(display);
        camera.zoomIn(2, {400, 400}, 0);
        double t = run(camera, {400, 400}, 0, 1);
        Point last = camera.center();
        double lastSpeed = 0;
        double maxDrop = 0;
        t = run(camera, {2200, 1400}, t, 4, [&](double) {
            const double speed = hypot(camera.center(), last) * camera.zoom() / kFrame;
            maxDrop = std::max(maxDrop, lastSpeed - speed);
            lastSpeed = speed;
            last = camera.center();
        });
        CHECK(maxDrop < 150);
        (void)t;
    }

    {
        ZoomCamera camera(display);
        const Point cursor{1152, 768};
        camera.zoomIn(2, cursor, 0);
        const double t = run(camera, cursor, 0, 1);
        camera.adjustZoom(1.1);
        double lastZoom = camera.zoom();
        int tinyFrames = 0;
        run(camera, cursor, t, 2, [&](double) {
            const double edgeMove = std::abs(std::log(camera.zoom()) - std::log(lastZoom)) * display.width;
            if (edgeMove > 0 && edgeMove < 0.5) ++tinyFrames;
            lastZoom = camera.zoom();
        });
        CHECK(tinyFrames <= 1);
        CHECK_NEAR(camera.zoom(), 2.2, 1e-9);
    }

    {
        ZoomCamera camera(display);
        const Point cursor{1800, 300};
        camera.zoomIn(2.5, cursor, 0);
        double t = run(camera, cursor, 0, 1);
        camera.zoomOut(cursor, t);
        CHECK(camera.phase() == Phase::ZoomingOut);
        t = run(camera, cursor, t, 0.6, [&](double) { assertInsideDisplay(camera, display); });
        CHECK(camera.phase() == Phase::Idle);
        CHECK(camera.zoom() == 1);
        CHECK(camera.center() == Point{display.width / 2, display.height / 2});
        CHECK(camera.viewport() == Rect{0, 0, display.width, display.height});
    }

    {
        ZoomCamera camera(display);
        const Point cursor{700, 900};
        camera.zoomIn(2, cursor, 0);
        double t = run(camera, cursor, 0, 0.25);
        const double zoomBefore = camera.zoom();
        const Point centerBefore = camera.center();
        camera.zoomOut(cursor, t);
        t += kFrame;
        camera.update(cursor, t, kFrame);
        CHECK_NEAR(camera.zoom(), zoomBefore, 0.01);
        CHECK_NEAR(camera.center().x, centerBefore.x, 2);
        CHECK_NEAR(camera.center().y, centerBefore.y, 2);
        run(camera, cursor, t, 0.6);
        CHECK(camera.phase() == Phase::Idle);
    }

    {
        ZoomCamera camera(display);
        const Point cursor{1152, 768};
        camera.zoomIn(2, cursor, 0);
        double t = run(camera, cursor, 0, 1);
        const Point before = camera.screenPoint(cursor);
        camera.adjustZoom(1.5);
        t = run(camera, cursor, t, 1, [&](double) {
            const Point p = camera.screenPoint(cursor);
            CHECK_NEAR(p.x, before.x, 0.5);
            CHECK_NEAR(p.y, before.y, 0.5);
        });
        CHECK_NEAR(camera.zoom(), 3, 1e-6);
        camera.adjustZoom(10);
        run(camera, cursor, t, 1);
        CHECK_NEAR(camera.zoom(), camera.config.maxZoom, 1e-6);
    }

    {
        Vector velocity;
        Point p;
        const Point target{100, -50};
        for (int i = 0; i < 600; ++i) {
            p = SmoothDamp::step(p, target, velocity, 0.3, 10'000, kFrame);
            CHECK(p.x <= target.x + 1e-9);
            CHECK(p.y >= target.y - 1e-9);
        }
        CHECK_NEAR(p.x, target.x, 0.01);
        CHECK_NEAR(p.y, target.y, 0.01);
    }

    {
        for (const double zoom : {1.5, 2.0, 1.75}) {
            const Size size = ViewportMath::size(display, zoom);
            for (double x = 100.0; x < 101.0; x += 0.037) {
                const Rect aligned = ViewportMath::pixelAligned(
                    Rect{x, 333.3, size.width, size.height}, zoom, 2, display);
                const double pixels = aligned.minX() * zoom * 2;
                CHECK_NEAR(pixels, std::round(pixels), 1e-6);
                const double rows = aligned.minY() * zoom * 2;
                CHECK_NEAR(rows, std::round(rows), 1e-6);
                CHECK(std::abs(aligned.minX() - x) * zoom * 2 <= 0.5 + 1e-9);
            }
            const Rect right = ViewportMath::pixelAligned(
                Rect{display.width - size.width, 0, size.width, size.height}, zoom, 2, display);
            CHECK(right.maxX() <= display.width + 1e-9);
            CHECK(right.maxX() > display.width - 1 / (zoom * 2) - 1e-9);
            const Rect left = ViewportMath::pixelAligned(
                Rect{0, 0, size.width, size.height}, zoom, 2, display);
            CHECK(left.x == 0);
            CHECK(left.y == 0);
        }
    }
}

void cursorMotionTests() {
    {
        CursorMotion motion({0, 0}, 0);
        motion.update({300, 40}, false, 1.0 / 60, 1.0 / 60);
        CHECK(motion.position == Point{300, 40});
    }
    {
        CursorMotion motion({0, 0}, 0);
        motion.smoothTime = 0.05;
        const Point target{200, 0};
        double t = 0;
        motion.update(target, false, t + 1.0 / 60, 1.0 / 60);
        CHECK(motion.position.x < 200);
        for (int i = 0; i < 60; ++i) {
            t += 1.0 / 60;
            motion.update(target, false, t, 1.0 / 60);
            CHECK(motion.position.x <= 200);
        }
        CHECK_NEAR(motion.position.x, 200, 0.5);
    }
    {
        CursorMotion motion({0, 0}, 0);
        motion.idleDelay = 2;
        double t = 0;
        while (t < 3) {
            t += 1.0 / 60;
            motion.update({0, 0}, false, t, 1.0 / 60);
            if (t < 2) CHECK(motion.opacity == 1);
        }
        CHECK(motion.opacity == 0);
        for (int i = 0; i < 10; ++i) {
            t += 1.0 / 60;
            motion.update({5, 5}, false, t, 1.0 / 60);
        }
        CHECK_NEAR(motion.opacity, 1, 1e-9);
    }
    {
        CursorMotion motion({0, 0}, 0);
        motion.idleDelay = 1;
        double t = 0;
        while (t < 3) {
            t += 1.0 / 60;
            motion.update({0, 0}, true, t, 1.0 / 60);
        }
        CHECK(motion.opacity == 1);
    }
    {
        for (const ClickEffect effect : {ClickEffect::Ripple, ClickEffect::Halo}) {
            double lastRadius = 0;
            double lastOpacity = 1;
            double age = 0;
            while (const auto look = ClickEffectMath::appearance(effect, age)) {
                CHECK(look->radius >= lastRadius);
                CHECK(look->opacity <= lastOpacity);
                lastRadius = look->radius;
                lastOpacity = look->opacity;
                age += 1.0 / 60;
            }
            CHECK_NEAR(age, ClickEffectMath::duration(effect), 1.0 / 30);
        }
        CHECK(!ClickEffectMath::appearance(ClickEffect::None, 0).has_value());
    }
}

void dragPointerTests() {
    {
        const Point start{300, 200};
        DragPointer pointer(start, std::nullopt);
        for (const Point sample : {Point{302, 201}, Point{320, 210}, Point{370, 250}}) {
            pointer.move(sample, std::nullopt);
            CHECK(pointer.location == sample);
        }
        CHECK(!(pointer.location == start));
    }
    {
        const Rect bounds{-1920, -1080, 1920, 1080};
        DragPointer pointer({-10, -10}, bounds);
        pointer.move({50, 80}, bounds);
        CHECK(pointer.location == Point{0, 0});
        pointer.move({150, 180}, bounds);
        CHECK(pointer.location == Point{0, 0});
        pointer.move({147, 176}, bounds);
        CHECK(pointer.location == Point{-3, -4});
    }
    {
        const Rect bounds{1920, -1080, 1920, 1080};
        for (const Point sample : {Point{5000, 100}, Point{1800, -1200}}) {
            DragPointer pointer({3000, -500}, bounds);
            pointer.move(sample, bounds);
            CHECK(pointer.location.x >= bounds.minX());
            CHECK(pointer.location.x <= bounds.maxX());
            CHECK(pointer.location.y >= bounds.minY());
            CHECK(pointer.location.y <= bounds.maxY());
        }
    }
    {
        const Point start{-500, 200};
        DragPointer pointer(start, std::nullopt);
        pointer.move({-400, 300}, std::nullopt);
        pointer.move({-400, 300}, std::nullopt);
        CHECK(pointer.location == Point{-400, 300});
        pointer = DragPointer(start, std::nullopt);
        pointer.move({-499, 198}, std::nullopt);
        CHECK(pointer.location == Point{-499, 198});
    }
    {
        const Rect bounds{0, 0, 100, 100};
        DragPointer pointer({103, 50}, bounds);
        CHECK(pointer.location == Point{100, 50});
        pointer.move({102, 50}, bounds);
        CHECK(pointer.location == Point{99, 50});
    }
}

void textLineTests() {
    const std::vector<Rect> segments = {
        {40, 100, 600, 30},
        {680, 101, 320, 29},
        {40, 140, 900, 30},
        {1600, 100, 400, 30},
    };
    {
        const auto line = TextLines::line({300, 115}, segments);
        CHECK(line.has_value());
        CHECK(line.value() == Rect{40, 100, 960, 30});
    }
    {
        const auto line = TextLines::line({1050, 112}, segments);
        CHECK(line.has_value());
        CHECK(line->minY() == 100);
        CHECK(line->maxX() == 1000);
    }
    {
        const auto line = TextLines::line({300, 132}, segments);
        CHECK(line.has_value());
        CHECK(line->minY() == 100);
    }
    {
        const std::vector<Rect> rows = {{137, 192, 140, 17}, {128, 213, 753, 21}};
        const auto line = TextLines::line({518, 209}, rows);
        CHECK(line.has_value());
        CHECK(line->minY() == 213);
    }
    {
        const std::vector<Rect> row = {
            {128, 213, 753, 21}, {913, 215, 10, 13}, {947, 213, 373, 17}};
        const auto line = TextLines::line({518, 220}, row);
        CHECK(line.has_value());
        CHECK(line.value() == row[0]);
    }
    CHECK(!TextLines::line({300, 260}, segments).has_value());
    CHECK(!TextLines::line({1300, 115}, segments).has_value());
    CHECK(!TextLines::line({300, 115}, {}).has_value());
}

void arrowHeadTests() {
    {
        std::vector<Point> points;
        for (int i = 0; i <= 20; ++i) points.push_back({i * 10.0, 50});
        const auto barbs = ArrowHead::barbs(points, 20);
        CHECK(barbs.has_value());
        const auto [a, b] = barbs.value();
        CHECK(a.x < 200);
        CHECK(b.x < 200);
        CHECK_NEAR(a.y - 50, -(b.y - 50), 0.001);
        CHECK_NEAR(std::hypot(a.x - 200, a.y - 50), 20, 0.001);
    }
    {
        std::vector<Point> points;
        for (int i = 0; i <= 20; ++i) points.push_back({i * 10.0, 50});
        points.push_back({200, 48});
        const auto barbs = ArrowHead::barbs(points, 24);
        CHECK(barbs.has_value());
        CHECK(std::max(barbs->first.x, barbs->second.x) < 200);
    }
    CHECK(!ArrowHead::barbs({{5, 5}}, 20).has_value());
    CHECK(!ArrowHead::barbs({{5, 5}, {6, 5}}, 20).has_value());
}

}  // namespace

int main() {
    cameraTests();
    cursorMotionTests();
    dragPointerTests();
    textLineTests();
    arrowHeadTests();

    if (g_failures == 0) {
        std::printf("core_tests: all checks passed\n");
        return 0;
    }
    std::printf("core_tests: %d failure(s)\n", g_failures);
    return 1;
}
