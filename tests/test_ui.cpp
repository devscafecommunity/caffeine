#include "catch.hpp"
#include "../src/Caffeine.hpp"
#include "../src/ui/UIComponents.hpp"
#include "../src/ui/UISystem.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::UI;

static constexpr f32 kEps = 0.001f;
static bool approxEq(f32 a, f32 b) { return std::fabs(a - b) < kEps; }

TEST_CASE("UIColor - white preset", "[ui]") {
    auto c = UIColor::white();
    REQUIRE(approxEq(c.r, 1.0f));
    REQUIRE(approxEq(c.g, 1.0f));
    REQUIRE(approxEq(c.b, 1.0f));
    REQUIRE(approxEq(c.a, 1.0f));
}

TEST_CASE("UIColor - black preset", "[ui]") {
    auto c = UIColor::black();
    REQUIRE(approxEq(c.r, 0.0f));
    REQUIRE(approxEq(c.g, 0.0f));
    REQUIRE(approxEq(c.b, 0.0f));
    REQUIRE(approxEq(c.a, 1.0f));
}

TEST_CASE("UIColor - transparent preset", "[ui]") {
    auto c = UIColor::transparent();
    REQUIRE(approxEq(c.a, 0.0f));
}

TEST_CASE("UIColor - equality operator", "[ui]") {
    UIColor a = UIColor::white();
    UIColor b = UIColor::white();
    REQUIRE(a == b);
    b.r = 0.5f;
    REQUIRE(!(a == b));
}

TEST_CASE("UIRect - contains point inside", "[ui]") {
    UIRect r;
    r.position = {10.0f, 10.0f};
    r.size     = {100.0f, 50.0f};
    REQUIRE(r.contains({50.0f, 30.0f}));
}

TEST_CASE("UIRect - contains point outside", "[ui]") {
    UIRect r;
    r.position = {10.0f, 10.0f};
    r.size     = {100.0f, 50.0f};
    REQUIRE(!r.contains({5.0f, 30.0f}));
}

TEST_CASE("UIRect - contains point on edge", "[ui]") {
    UIRect r;
    r.position = {0.0f, 0.0f};
    r.size     = {100.0f, 100.0f};
    REQUIRE(r.contains({0.0f, 0.0f}));
    REQUIRE(r.contains({100.0f, 100.0f}));
}

TEST_CASE("UIRect - isValid", "[ui]") {
    UIRect valid;
    valid.size = {10.0f, 10.0f};
    REQUIRE(valid.isValid());

    UIRect invalid;
    invalid.size = {0.0f, 0.0f};
    REQUIRE(!invalid.isValid());
}

// ── Default component values ──────────────────────────────────────────────────

TEST_CASE("UIWidget - defaults", "[ui]") {
    UIWidget w;
    REQUIRE(w.type         == UIWidgetType::Panel);
    REQUIRE(w.parentId     == kUIInvalidParent);
    REQUIRE(w.visible      == true);
    REQUIRE(w.interactable == true);
    REQUIRE(w.siblingOrder == 0);
}

TEST_CASE("UIButton - default colors", "[ui]") {
    UIButton btn;
    REQUIRE(approxEq(btn.idleColor.r, 0.2f));
    REQUIRE(btn.isHovered == false);
    REQUIRE(btn.isPressed == false);
}

TEST_CASE("UILabel - default wordWrap false", "[ui]") {
    UILabel lbl;
    REQUIRE(lbl.wordWrap == false);
}

TEST_CASE("UIProgressBar - default values", "[ui]") {
    UIProgressBar pb;
    REQUIRE(approxEq(pb.minValue,     0.0f));
    REQUIRE(approxEq(pb.maxValue,     100.0f));
    REQUIRE(approxEq(pb.currentValue, 50.0f));
    REQUIRE(pb.showText == false);
}

TEST_CASE("UISlider - default values", "[ui]") {
    UISlider sl;
    REQUIRE(approxEq(sl.minValue,     0.0f));
    REQUIRE(approxEq(sl.maxValue,     1.0f));
    REQUIRE(approxEq(sl.currentValue, 0.5f));
    REQUIRE(sl.snapToInt == false);
}

TEST_CASE("UICheckbox - default unchecked", "[ui]") {
    UICheckbox cb;
    REQUIRE(cb.checked == false);
}

// ── UISystem factory helpers ──────────────────────────────────────────────────

TEST_CASE("UISystem - createCanvas returns valid entity with Canvas type", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    REQUIRE(canvas.isValid());
    auto* w = world.get<UIWidget>(canvas);
    REQUIRE(w != nullptr);
    REQUIRE(w->type     == UIWidgetType::Canvas);
    REQUIRE(w->parentId == kUIInvalidParent);
    REQUIRE(approxEq(w->computedRect.size.x, 1280.0f));
    REQUIRE(approxEq(w->computedRect.size.y, 720.0f));
}

TEST_CASE("UISystem - createButton returns entity with UIWidget and UIButton", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world);
    Entity btn    = sys.createButton(world, canvas.id(), "Play", {100.0f, 200.0f});
    REQUIRE(btn.isValid());
    auto* w = world.get<UIWidget>(btn);
    REQUIRE(w != nullptr);
    REQUIRE(w->type == UIWidgetType::Button);
    auto* b = world.get<UIButton>(btn);
    REQUIRE(b != nullptr);
    REQUIRE(std::string(b->labelText.c_str()) == "Play");
}

TEST_CASE("UISystem - createLabel returns entity with UILabel", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world);
    Entity lbl    = sys.createLabel(world, canvas.id(), "Hello", {0.0f, 0.0f});
    REQUIRE(lbl.isValid());
    auto* l = world.get<UILabel>(lbl);
    REQUIRE(l != nullptr);
    REQUIRE(std::string(l->text.c_str()) == "Hello");
}

TEST_CASE("UISystem - createProgressBar returns entity with UIProgressBar", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world);
    Entity pb     = sys.createProgressBar(world, canvas.id(), {0.0f, 0.0f});
    REQUIRE(pb.isValid());
    REQUIRE(world.get<UIProgressBar>(pb) != nullptr);
}

TEST_CASE("UISystem - createSlider returns entity with UISlider", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world);
    Entity sl     = sys.createSlider(world, canvas.id(), {0.0f, 0.0f});
    REQUIRE(sl.isValid());
    REQUIRE(world.get<UISlider>(sl) != nullptr);
}

TEST_CASE("UISystem - createPanel returns valid entity", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world);
    UIRect rect;
    rect.position = {0.0f, 0.0f};
    rect.size     = {200.0f, 100.0f};
    Entity panel  = sys.createPanel(world, canvas.id(), rect);
    REQUIRE(panel.isValid());
    auto* w = world.get<UIWidget>(panel);
    REQUIRE(w != nullptr);
    REQUIRE(w->type == UIWidgetType::Panel);
}

// ── onUpdate ─────────────────────────────────────────────────────────────────

TEST_CASE("UISystem - onUpdate empty world does not crash", "[ui]") {
    World world;
    UISystem sys;
    REQUIRE_NOTHROW(sys.onUpdate(world, 0.016f));
}

TEST_CASE("UISystem - multiple canvases onUpdate does not crash", "[ui]") {
    World world;
    UISystem sys;
    sys.createCanvas(world, {800.0f, 600.0f});
    sys.createCanvas(world, {1920.0f, 1080.0f});
    REQUIRE_NOTHROW(sys.onUpdate(world, 0.016f));
}

// ── hitTest ──────────────────────────────────────────────────────────────────

TEST_CASE("UISystem - hitTest returns INVALID when no widgets", "[ui]") {
    World world;
    UISystem sys;
    Entity result = sys.hitTest(world, {100.0f, 100.0f});
    REQUIRE(!result.isValid());
}

TEST_CASE("UISystem - hitTest returns entity when click is inside its rect", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    Entity btn    = sys.createButton(world, canvas.id(), "OK", {50.0f, 50.0f}, {100.0f, 40.0f});
    sys.onUpdate(world, 0.0f);  // run layout
    Entity hit = sys.hitTest(world, {100.0f, 70.0f});
    REQUIRE(hit.isValid());
    REQUIRE(hit.id() == btn.id());
}

TEST_CASE("UISystem - hitTest returns INVALID when click is outside all rects", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    sys.createButton(world, canvas.id(), "OK", {50.0f, 50.0f}, {100.0f, 40.0f});
    sys.onUpdate(world, 0.0f);
    Entity hit = sys.hitTest(world, {500.0f, 500.0f});
    REQUIRE(!hit.isValid());
}

TEST_CASE("UISystem - invisible widget not returned by hitTest", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    Entity btn    = sys.createButton(world, canvas.id(), "Hidden", {50.0f, 50.0f}, {100.0f, 40.0f});
    sys.onUpdate(world, 0.0f);
    auto* w = world.get<UIWidget>(btn);
    w->visible = false;
    Entity hit = sys.hitTest(world, {100.0f, 70.0f});
    REQUIRE(!hit.isValid());
}

// ── Input callbacks ───────────────────────────────────────────────────────────

TEST_CASE("UISystem - onClick fires when mouse clicked on widget", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    Entity btn    = sys.createButton(world, canvas.id(), "Click", {50.0f, 50.0f}, {100.0f, 40.0f});
    sys.onUpdate(world, 0.0f);

    bool fired = false;
    world.get<UIWidget>(btn)->onClick = [&](Entity) { fired = true; };

    sys.injectMousePosition({100.0f, 70.0f});
    sys.injectMouseClick(true);
    sys.onUpdate(world, 0.016f);
    REQUIRE(fired);
}

TEST_CASE("UISystem - onClick does not fire when clicking outside", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    Entity btn    = sys.createButton(world, canvas.id(), "Click", {50.0f, 50.0f}, {100.0f, 40.0f});
    sys.onUpdate(world, 0.0f);

    bool fired = false;
    world.get<UIWidget>(btn)->onClick = [&](Entity) { fired = true; };

    sys.injectMousePosition({400.0f, 400.0f});
    sys.injectMouseClick(true);
    sys.onUpdate(world, 0.016f);
    REQUIRE(!fired);
}

TEST_CASE("UISystem - onHoverEnter fires when mouse enters widget", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    Entity btn    = sys.createButton(world, canvas.id(), "Hover", {50.0f, 50.0f}, {100.0f, 40.0f});
    sys.onUpdate(world, 0.0f);

    bool entered = false;
    world.get<UIWidget>(btn)->onHoverEnter = [&](Entity) { entered = true; };

    sys.injectMousePosition({100.0f, 70.0f});
    sys.injectMouseClick(false);
    sys.onUpdate(world, 0.016f);
    REQUIRE(entered);
}

// ── bindValue ────────────────────────────────────────────────────────────────

TEST_CASE("UISystem - bindValue updates ProgressBar currentValue", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    Entity pb     = sys.createProgressBar(world, canvas.id(), {0.0f, 0.0f});

    f32 source = 75.0f;
    sys.bindValue(pb, [&](World&) { return source; });
    sys.onUpdate(world, 0.016f);

    auto* bar = world.get<UIProgressBar>(pb);
    REQUIRE(bar != nullptr);
    REQUIRE(approxEq(bar->currentValue, 75.0f));
}

// ── Layout ───────────────────────────────────────────────────────────────────

TEST_CASE("UISystem - layout computes rect for direct child of canvas", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    Entity btn    = sys.createButton(world, canvas.id(), "X", {100.0f, 200.0f}, {120.0f, 40.0f});
    sys.onUpdate(world, 0.0f);

    auto* w = world.get<UIWidget>(btn);
    REQUIRE(w != nullptr);
    REQUIRE(approxEq(w->computedRect.position.x, 100.0f));
    REQUIRE(approxEq(w->computedRect.position.y, 200.0f));
    REQUIRE(approxEq(w->computedRect.size.x, 120.0f));
    REQUIRE(approxEq(w->computedRect.size.y, 40.0f));
}

// ── siblingOrder / interaction ────────────────────────────────────────────────

TEST_CASE("UISystem - siblingOrder: higher order wins hitTest", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});

    Entity low  = sys.createButton(world, canvas.id(), "Low",  {50.0f, 50.0f}, {200.0f, 200.0f});
    Entity high = sys.createButton(world, canvas.id(), "High", {50.0f, 50.0f}, {200.0f, 200.0f});

    world.get<UIWidget>(low)->siblingOrder  = 0;
    world.get<UIWidget>(high)->siblingOrder = 1;

    sys.onUpdate(world, 0.0f);
    Entity hit = sys.hitTest(world, {100.0f, 100.0f});
    REQUIRE(hit.isValid());
    REQUIRE(hit.id() == high.id());
}

TEST_CASE("UISystem - button isHovered state set on hover", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    Entity btn    = sys.createButton(world, canvas.id(), "Hover", {50.0f, 50.0f}, {100.0f, 40.0f});
    sys.onUpdate(world, 0.0f);

    sys.injectMousePosition({100.0f, 70.0f});
    sys.onUpdate(world, 0.016f);

    auto* b = world.get<UIButton>(btn);
    REQUIRE(b != nullptr);
    REQUIRE(b->isHovered == true);
}

TEST_CASE("UISystem - non-interactable widget not hit", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    Entity lbl    = sys.createLabel(world, canvas.id(), "Info", {50.0f, 50.0f});
    sys.onUpdate(world, 0.0f);

    world.get<UIWidget>(lbl)->interactable = false;
    Entity hit = sys.hitTest(world, {100.0f, 60.0f});
    REQUIRE(!hit.isValid());
}

TEST_CASE("UISystem - createCheckbox returns entity with UICheckbox", "[ui]") {
    World world;
    UISystem sys;
    Entity canvas = sys.createCanvas(world, {1280.0f, 720.0f});
    Entity cb     = sys.createCheckbox(world, canvas.id(), {100.0f, 100.0f});
    REQUIRE(cb.isValid());
    auto* c = world.get<UICheckbox>(cb);
    REQUIRE(c != nullptr);
    REQUIRE(c->checked == false);
}
