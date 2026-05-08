#pragma once

#include "ui/UIComponents.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/ComponentQuery.hpp"
#include "events/EventBus.hpp"

#include <vector>
#include <unordered_map>
#include <functional>
#include <climits>

namespace Caffeine::UI {

using namespace Caffeine;

class UISystem : public ECS::ISystem {
public:
    explicit UISystem(Events::EventBus* eventBus = nullptr)
        : m_eventBus(eventBus) {}

    void onUpdate(ECS::World& world, f32 dt) override {
        (void)dt;
        updateBindings(world);
        layoutWidgets(world);
        processInput(world);
    }

    ECS::Entity createCanvas(ECS::World& world, Vec2 size = {1280.0f, 720.0f}) {
        ECS::Entity e = world.create();
        UIWidget w;
        w.type = UIWidgetType::Canvas;
        w.parentId = kUIInvalidParent;
        w.computedRect = {Vec2{0.0f, 0.0f}, size};
        world.add<UIWidget>(e, w);
        return e;
    }

    ECS::Entity createPanel(ECS::World& world, u32 parentId, UIRect rect) {
        ECS::Entity e = world.create();
        UIWidget w;
        w.type = UIWidgetType::Panel;
        w.parentId = parentId;
        w.transform.anchorMin = {0.0f, 0.0f};
        w.transform.anchorMax = {0.0f, 0.0f};
        w.transform.offsetMin = rect.position;
        w.transform.offsetMax = {rect.position.x + rect.size.x, rect.position.y + rect.size.y};
        world.add<UIWidget>(e, w);
        return e;
    }

    ECS::Entity createButton(ECS::World& world, u32 parentId, const char* text,
                              Vec2 pos, Vec2 size = {120.0f, 40.0f}) {
        ECS::Entity e = world.create();
        UIWidget w;
        w.type = UIWidgetType::Button;
        w.parentId = parentId;
        w.transform.anchorMin = {0.0f, 0.0f};
        w.transform.anchorMax = {0.0f, 0.0f};
        w.transform.offsetMin = pos;
        w.transform.offsetMax = {pos.x + size.x, pos.y + size.y};
        world.add<UIWidget>(e, w);
        UIButton btn;
        btn.labelText = FixedString<64>(text);
        world.add<UIButton>(e, btn);
        return e;
    }

    ECS::Entity createLabel(ECS::World& world, u32 parentId, const char* text, Vec2 pos) {
        ECS::Entity e = world.create();
        UIWidget w;
        w.type = UIWidgetType::Label;
        w.parentId = parentId;
        w.interactable = false;
        w.transform.anchorMin = {0.0f, 0.0f};
        w.transform.anchorMax = {0.0f, 0.0f};
        w.transform.offsetMin = pos;
        w.transform.offsetMax = {pos.x + 200.0f, pos.y + 24.0f};
        world.add<UIWidget>(e, w);
        UILabel lbl;
        lbl.text = FixedString<256>(text);
        world.add<UILabel>(e, lbl);
        return e;
    }

    ECS::Entity createProgressBar(ECS::World& world, u32 parentId,
                                   Vec2 pos, Vec2 size = {200.0f, 20.0f}) {
        ECS::Entity e = world.create();
        UIWidget w;
        w.type = UIWidgetType::ProgressBar;
        w.parentId = parentId;
        w.interactable = false;
        w.transform.anchorMin = {0.0f, 0.0f};
        w.transform.anchorMax = {0.0f, 0.0f};
        w.transform.offsetMin = pos;
        w.transform.offsetMax = {pos.x + size.x, pos.y + size.y};
        world.add<UIWidget>(e, w);
        world.add<UIProgressBar>(e);
        return e;
    }

    ECS::Entity createSlider(ECS::World& world, u32 parentId,
                              Vec2 pos, Vec2 size = {200.0f, 20.0f}) {
        ECS::Entity e = world.create();
        UIWidget w;
        w.type = UIWidgetType::Slider;
        w.parentId = parentId;
        w.transform.anchorMin = {0.0f, 0.0f};
        w.transform.anchorMax = {0.0f, 0.0f};
        w.transform.offsetMin = pos;
        w.transform.offsetMax = {pos.x + size.x, pos.y + size.y};
        world.add<UIWidget>(e, w);
        world.add<UISlider>(e);
        return e;
    }

    ECS::Entity createCheckbox(ECS::World& world, u32 parentId, Vec2 pos) {
        ECS::Entity e = world.create();
        UIWidget w;
        w.type = UIWidgetType::Checkbox;
        w.parentId = parentId;
        w.transform.anchorMin = {0.0f, 0.0f};
        w.transform.anchorMax = {0.0f, 0.0f};
        w.transform.offsetMin = pos;
        w.transform.offsetMax = {pos.x + 24.0f, pos.y + 24.0f};
        world.add<UIWidget>(e, w);
        world.add<UICheckbox>(e);
        return e;
    }

    void bindValue(ECS::Entity widget, std::function<f32(ECS::World&)> getter) {
        ValueBinding b;
        b.widgetId = widget.id();
        b.getter   = std::move(getter);
        m_bindings.push_back(std::move(b));
    }

    ECS::Entity hitTest(ECS::World& world, Vec2 screenPos) {
        ECS::Entity result = ECS::Entity::INVALID;
        i32 bestOrder = INT_MIN;

        ECS::ComponentQuery q;
        q.with<UIWidget>();

        world.forEach<UIWidget>(q, [&](ECS::Entity e, UIWidget& w) {
            if (!w.visible || !w.interactable) return;
            if (w.type == UIWidgetType::Canvas) return;
            if (w.computedRect.contains(screenPos)) {
                if (w.siblingOrder > bestOrder) {
                    bestOrder = w.siblingOrder;
                    result    = e;
                }
            }
        });

        return result;
    }

    void injectMousePosition(Vec2 pos)  { m_mousePos  = pos; }
    void injectMouseClick(bool pressed) { m_mouseDown = pressed; }

private:
    static UIRect computeRect(const RectTransform& t, const UIRect& parent) {
        UIRect rect;
        rect.position.x = parent.position.x + parent.size.x * t.anchorMin.x + t.offsetMin.x;
        rect.position.y = parent.position.y + parent.size.y * t.anchorMin.y + t.offsetMin.y;
        rect.size.x = parent.size.x * (t.anchorMax.x - t.anchorMin.x) +
                      (t.offsetMax.x - t.offsetMin.x);
        rect.size.y = parent.size.y * (t.anchorMax.y - t.anchorMin.y) +
                      (t.offsetMax.y - t.offsetMin.y);
        return rect;
    }

    void layoutWidgets(ECS::World& world) {
        std::unordered_map<u32, UIRect> computed;

        ECS::ComponentQuery q;
        q.with<UIWidget>();

        world.forEach<UIWidget>(q, [&](ECS::Entity e, UIWidget& w) {
            if (w.type == UIWidgetType::Canvas) {
                computed[e.id()] = w.computedRect;
            }
        });

        for (int pass = 0; pass < 8; ++pass) {
            world.forEach<UIWidget>(q, [&](ECS::Entity e, UIWidget& w) {
                if (w.type == UIWidgetType::Canvas) return;
                if (!w.visible) return;
                auto it = computed.find(w.parentId);
                if (it == computed.end()) return;
                UIRect rect    = computeRect(w.transform, it->second);
                w.computedRect = rect;
                computed[e.id()] = rect;
            });
        }
    }

    void processInput(ECS::World& world) {
        bool justClicked = m_mouseDown && !m_prevMouseDown;

        ECS::Entity hovered = hitTest(world, m_mousePos);
        u32 hoveredId = hovered.isValid() ? hovered.id() : kUIInvalidParent;

        if (hoveredId != m_hoveredId) {
            if (m_hoveredId != kUIInvalidParent) {
                ECS::Entity prev(m_hoveredId, &world);
                if (auto* w = world.get<UIWidget>(prev)) {
                    if (w->onHoverExit) w->onHoverExit(prev);
                }
                if (auto* btn = world.get<UIButton>(prev)) {
                    btn->isHovered = false;
                }
            }
            if (hovered.isValid()) {
                if (auto* w = world.get<UIWidget>(hovered)) {
                    if (w->onHoverEnter) w->onHoverEnter(hovered);
                }
                if (auto* btn = world.get<UIButton>(hovered)) {
                    btn->isHovered = true;
                }
            }
            m_hoveredId = hoveredId;
        }

        if (justClicked && hovered.isValid()) {
            if (auto* w = world.get<UIWidget>(hovered)) {
                if (w->onClick) w->onClick(hovered);
            }
            if (auto* btn = world.get<UIButton>(hovered)) {
                btn->isPressed = true;
            }
        }

        if (!m_mouseDown) {
            ECS::ComponentQuery qBtn;
            qBtn.with<UIButton>();
            world.forEach<UIButton>(qBtn, [](ECS::Entity, UIButton& btn) {
                btn.isPressed = false;
            });
        }

        m_prevMouseDown = m_mouseDown;
    }

    void updateBindings(ECS::World& world) {
        for (auto& b : m_bindings) {
            ECS::Entity e(b.widgetId, &world);
            if (!e.isValid()) continue;
            f32 value = b.getter(world);
            if (auto* pb = world.get<UIProgressBar>(e)) {
                pb->currentValue = value;
                if (auto* w = world.get<UIWidget>(e)) {
                    if (w->onValueChanged) w->onValueChanged(e, value);
                }
            } else if (auto* sl = world.get<UISlider>(e)) {
                sl->currentValue = value;
                if (auto* w = world.get<UIWidget>(e)) {
                    if (w->onValueChanged) w->onValueChanged(e, value);
                }
            }
        }
    }

    struct ValueBinding {
        u32 widgetId;
        std::function<f32(ECS::World&)> getter;
    };

    Events::EventBus*         m_eventBus      = nullptr;
    Vec2                      m_mousePos      = {0.0f, 0.0f};
    bool                      m_mouseDown     = false;
    bool                      m_prevMouseDown = false;
    u32                       m_hoveredId     = kUIInvalidParent;
    std::vector<ValueBinding> m_bindings;
};

}

