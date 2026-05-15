#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "ui/UISystem.hpp"
#include <cstdio>
#include <cmath>
#include <string>

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::UI;

static constexpr f32 kEps = 0.001f;
static bool approxEq(f32 a, f32 b) { return std::fabs(a - b) < kEps; }

static int check(bool ok, const char* msg) {
    std::printf("  %s  %s\n", ok ? "✓" : "✗", msg);
    return ok ? 0 : 1;
}

int main() {
    int failures = 0;

    std::printf("═══ caffeine-combined: Core + UI ═══\n\n");

    // ── Core types ──────────────────────────────────────────────
    std::printf("[core] Types\n");
    failures += check(sizeof(u32) == 4, "u32 is 4 bytes");
    failures += check(sizeof(f32) == 4, "f32 is 4 bytes");

    Vec2 v(3.0f, 4.0f);
    failures += check(approxEq(v.length(), 5.0f), "Vec2(3,4).length() == 5");

    // ── UI components ───────────────────────────────────────────
    std::printf("\n[ui] Components\n");

    UIColor c = UIColor::white();
    failures += check(approxEq(c.r, 1.0f) && approxEq(c.a, 1.0f),
                      "UIColor::white()");

    UIRect r;
    r.position = {10.0f, 10.0f};
    r.size     = {100.0f, 50.0f};
    failures += check(r.contains({50.0f, 30.0f}),
                      "UIRect::contains inside");
    failures += check(!r.contains({5.0f, 30.0f}),
                      "UIRect::contains outside");

    // ── UISystem + World (core + UI together) ──────────────────
    std::printf("\n[combined] UISystem + World\n");

    World world;
    UISystem sys;

    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    failures += check(canvas.isValid(), "createCanvas returns valid entity");

    auto* w = world.get<UIWidget>(canvas);
    failures += check(w != nullptr && w->type == UIWidgetType::Canvas,
                      "canvas widget type == Canvas");

    Entity btn = sys.createButton(world, canvas.id(), "Play", {100.0f, 200.0f});
    failures += check(btn.isValid(), "createButton returns valid entity");

    auto* b = world.get<UIButton>(btn);
    failures += check(b != nullptr &&
                      std::string(b->labelText.cStr()) == "Play",
                      "button label == 'Play'");

    // Layout pass
    sys.onUpdate(world, 0.0f);

    auto* btnWidget = world.get<UIWidget>(btn);
    failures += check(btnWidget != nullptr &&
                      approxEq(btnWidget->computedRect.position.x, 100.0f) &&
                      approxEq(btnWidget->computedRect.position.y, 200.0f),
                      "layout: button at (100, 200)");

    // Hit test
    Entity hit = sys.hitTest(world, {150.0f, 220.0f});
    failures += check(hit.isValid() && hit.id() == btn.id(),
                      "hitTest finds button inside rect");

    Entity miss = sys.hitTest(world, {0.0f, 0.0f});
    failures += check(!miss.isValid(),
                      "hitTest returns invalid outside rect");

    // Input callback
    bool clicked = false;
    btnWidget->onClick = [&](Entity) { clicked = true; };
    sys.injectMousePosition({150.0f, 220.0f});
    sys.injectMouseClick(true);
    sys.onUpdate(world, 0.016f);
    failures += check(clicked, "onClick fires on mouse click");

    // ── Summary ─────────────────────────────────────────────────
    std::printf("\n═══ Result: %d failure(s) ═══\n", failures);
    return failures;
}
