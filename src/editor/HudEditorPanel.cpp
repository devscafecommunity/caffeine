#include "editor/HudEditorPanel.hpp"

#include "scene/SceneComponents.hpp"
#include "ui/UIRenderer.hpp"
#include "ui/UISystem.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <algorithm>
#include <cstdio>
#include <vector>

namespace Caffeine::Editor {
namespace {

constexpr f32 kRefW = 1280.0f;
constexpr f32 kRefH = 720.0f;

const char* widgetTypeName(UI::UIWidgetType type) {
    switch (type) {
        case UI::UIWidgetType::Canvas: return "Canvas";
        case UI::UIWidgetType::Panel: return "Panel";
        case UI::UIWidgetType::Button: return "Button";
        case UI::UIWidgetType::Label: return "Label";
        case UI::UIWidgetType::ProgressBar: return "Progress";
        case UI::UIWidgetType::Checkbox: return "Checkbox";
        case UI::UIWidgetType::Slider: return "Slider";
    }
    return "Widget";
}

const char* widgetCaption(ECS::World& world, ECS::Entity entity, const UI::UIWidget& widget) {
    if (widget.type == UI::UIWidgetType::Button) {
        if (const UI::UIButton* button = world.get<UI::UIButton>(entity)) {
            if (!button->labelText.empty()) return button->labelText.cStr();
        }
    }
    if (widget.type == UI::UIWidgetType::Label) {
        if (const UI::UILabel* label = world.get<UI::UILabel>(entity)) {
            if (!label->text.empty()) return label->text.cStr();
        }
    }
    return widgetTypeName(widget.type);
}

ECS::Entity findCanvas(ECS::World& world) {
    ECS::Entity found = ECS::Entity::INVALID;
    ECS::ComponentQuery query;
    query.with<UI::UIWidget>();
    world.forEach<UI::UIWidget>(query, [&](ECS::Entity entity, UI::UIWidget& widget) {
        if (!found.isValid() && widget.type == UI::UIWidgetType::Canvas) found = entity;
    });
    return found;
}

Vec2 canvasSizeOf(ECS::World& world) {
    ECS::Entity canvas = findCanvas(world);
    if (canvas.isValid()) {
        if (const UI::UIWidget* widget = world.get<UI::UIWidget>(canvas)) {
            if (widget->computedRect.size.x > 1.0f && widget->computedRect.size.y > 1.0f) {
                return widget->computedRect.size;
            }
        }
    }
    return {kRefW, kRefH};
}

void parentWidget(ECS::World& world, ECS::Entity child, ECS::Entity parent) {
    if (!child.isValid() || !parent.isValid()) return;
    if (UI::UIWidget* widget = world.get<UI::UIWidget>(child)) {
        widget->parentId = parent.id();
    }
    Scene::Parent& link = world.add<Scene::Parent>(child);
    link.parent = parent;
    link.dirty = true;
}

ECS::Entity parentForNewWidget(ECS::World& world, const EditorContext& ctx) {
    if (ctx.selectedEntity.isValid()) {
        if (const UI::UIWidget* widget = world.get<UI::UIWidget>(ctx.selectedEntity)) {
            if (widget->type == UI::UIWidgetType::Canvas || widget->type == UI::UIWidgetType::Panel) {
                return ctx.selectedEntity;
            }
            if (widget->parentId != UI::kUIInvalidParent) {
                ECS::Entity parent(widget->parentId, &world);
                if (parent.isValid() && world.get<UI::UIWidget>(parent)) return parent;
            }
        }
    }
    return findCanvas(world);
}

void moveWidget(UI::UIWidget& widget, Vec2 delta) {
    widget.transform.offsetMin.x += delta.x;
    widget.transform.offsetMin.y += delta.y;
    widget.transform.offsetMax.x += delta.x;
    widget.transform.offsetMax.y += delta.y;
}

void resizeWidget(UI::UIWidget& widget, Vec2 delta) {
    widget.transform.offsetMax.x += delta.x;
    widget.transform.offsetMax.y += delta.y;
    const f32 minW = widget.transform.offsetMin.x + 8.0f;
    const f32 minH = widget.transform.offsetMin.y + 8.0f;
    if (widget.transform.offsetMax.x < minW) widget.transform.offsetMax.x = minW;
    if (widget.transform.offsetMax.y < minH) widget.transform.offsetMax.y = minH;
}

ECS::Entity hitWidget(ECS::World& world, Vec2 canvasPoint) {
    ECS::Entity result = ECS::Entity::INVALID;
    i32 bestOrder = -1000000;
    ECS::ComponentQuery query;
    query.with<UI::UIWidget>();
    world.forEach<UI::UIWidget>(query, [&](ECS::Entity entity, UI::UIWidget& widget) {
        if (!widget.visible || widget.type == UI::UIWidgetType::Canvas) return;
        if (!widget.computedRect.contains(canvasPoint)) return;
        if (widget.siblingOrder >= bestOrder) {
            bestOrder = widget.siblingOrder;
            result = entity;
        }
    });
    return result;
}

}  // namespace

void HudEditorPanel::onImGuiRender(EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    if (!m_open) return;
    ImGui::Begin("HUD Editor", &m_open);
    ECS::World* world = ctx.activeWorld;
    if (!world) {
        ImGui::TextDisabled("Open a scene to edit the HUD.");
        ImGui::End();
        return;
    }

    UI::UISystem layout;
    layout.layout(*world);
    const Vec2 reference = canvasSizeOf(*world);

    UI::UISystem factory;
    auto finishCreate = [&](ECS::Entity entity, ECS::Entity parent) {
        if (parent.isValid() && entity.isValid() && entity.id() != parent.id()) {
            parentWidget(*world, entity, parent);
        }
        ctx.selectEntity(entity);
        ctx.isDirty = true;
        ctx.endUndo(*world);
        layout.layout(*world);
    };

    if (ImGui::Button("Canvas")) {
        ctx.beginUndo(EditorCommand::AddEntity, u32_max, *world);
        ECS::Entity canvas = factory.createCanvas(*world, {kRefW, kRefH});
        setEntityName(*world, canvas, "HUD");
        finishCreate(canvas, ECS::Entity::INVALID);
    }
    ImGui::SameLine();
    if (ImGui::Button("Panel")) {
        ctx.beginUndo(EditorCommand::AddEntity, u32_max, *world);
        ECS::Entity parent = parentForNewWidget(*world, ctx);
        if (!parent.isValid()) {
            parent = factory.createCanvas(*world, {kRefW, kRefH});
            setEntityName(*world, parent, "HUD");
        }
        ECS::Entity panel = factory.createPanel(*world, parent.id(), {{24.0f, 24.0f}, {280.0f, 120.0f}});
        setEntityName(*world, panel, "Panel");
        finishCreate(panel, parent);
    }
    ImGui::SameLine();
    if (ImGui::Button("Label")) {
        ctx.beginUndo(EditorCommand::AddEntity, u32_max, *world);
        ECS::Entity parent = parentForNewWidget(*world, ctx);
        if (!parent.isValid()) {
            parent = factory.createCanvas(*world, {kRefW, kRefH});
            setEntityName(*world, parent, "HUD");
        }
        ECS::Entity label = factory.createLabel(*world, parent.id(), "Label", {32.0f, 32.0f});
        setEntityName(*world, label, "Label");
        finishCreate(label, parent);
    }
    ImGui::SameLine();
    if (ImGui::Button("Button")) {
        ctx.beginUndo(EditorCommand::AddEntity, u32_max, *world);
        ECS::Entity parent = parentForNewWidget(*world, ctx);
        if (!parent.isValid()) {
            parent = factory.createCanvas(*world, {kRefW, kRefH});
            setEntityName(*world, parent, "HUD");
        }
        ECS::Entity button = factory.createButton(*world, parent.id(), "Button", {32.0f, 80.0f});
        setEntityName(*world, button, "Button");
        finishCreate(button, parent);
    }
    ImGui::SameLine();
    if (ImGui::Button("Bar")) {
        ctx.beginUndo(EditorCommand::AddEntity, u32_max, *world);
        ECS::Entity parent = parentForNewWidget(*world, ctx);
        if (!parent.isValid()) {
            parent = factory.createCanvas(*world, {kRefW, kRefH});
            setEntityName(*world, parent, "HUD");
        }
        ECS::Entity bar = factory.createProgressBar(*world, parent.id(), {32.0f, 140.0f});
        setEntityName(*world, bar, "Health");
        finishCreate(bar, parent);
    }
    ImGui::SameLine();
    if (ImGui::Button("Slider")) {
        ctx.beginUndo(EditorCommand::AddEntity, u32_max, *world);
        ECS::Entity parent = parentForNewWidget(*world, ctx);
        if (!parent.isValid()) {
            parent = factory.createCanvas(*world, {kRefW, kRefH});
            setEntityName(*world, parent, "HUD");
        }
        ECS::Entity slider = factory.createSlider(*world, parent.id(), {32.0f, 180.0f});
        setEntityName(*world, slider, "Slider");
        finishCreate(slider, parent);
    }
    ImGui::SameLine();
    if (ImGui::Button("Check")) {
        ctx.beginUndo(EditorCommand::AddEntity, u32_max, *world);
        ECS::Entity parent = parentForNewWidget(*world, ctx);
        if (!parent.isValid()) {
            parent = factory.createCanvas(*world, {kRefW, kRefH});
            setEntityName(*world, parent, "HUD");
        }
        ECS::Entity box = factory.createCheckbox(*world, parent.id(), {32.0f, 220.0f});
        setEntityName(*world, box, "Checkbox");
        finishCreate(box, parent);
    }

    ImGui::TextDisabled("Arrasta para mover. O canto inferior direito redimensiona. Cores e texto ficam no inspector.");
    ImGui::TextDisabled("Cliques e valores continuam em Lua (caffeine.ui) ou em C++ (UISystem).");

    const float listWidth = 200.0f;
    ImGui::BeginChild("hud_list", ImVec2(listWidth, 0.0f), true);
    ECS::ComponentQuery query;
    query.with<UI::UIWidget>();
    world->forEach<UI::UIWidget>(query, [&](ECS::Entity entity, UI::UIWidget& widget) {
        ImGui::PushID(static_cast<int>(entity.id()));
        const bool selected = ctx.selectedEntity.id() == entity.id();
        char row[160];
        std::snprintf(row, sizeof(row), "%s  %s", widgetTypeName(widget.type), getEntityName(*world, entity));
        if (ImGui::Selectable(row, selected)) ctx.selectEntity(entity);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", widgetCaption(*world, entity, widget));
        }
        ImGui::PopID();
    });
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("hud_stage", ImVec2(0.0f, 0.0f), true);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float aspect = reference.x / std::max(reference.y, 1.0f);
    ImVec2 canvas(avail.x, avail.x / aspect);
    if (canvas.y > avail.y) {
        canvas.y = avail.y;
        canvas.x = canvas.y * aspect;
    }
    canvas.x = std::max(canvas.x, 64.0f);
    canvas.y = std::max(canvas.y, 36.0f);

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("hud_surface", canvas);
    const bool surfaceHovered = ImGui::IsItemHovered();
    const ImVec2 origin = ImGui::GetItemRectMin();
    const f32 scaleX = canvas.x / reference.x;
    const f32 scaleY = canvas.y / reference.y;
    auto toCanvas = [&](ImVec2 screen) {
        return Vec2{(screen.x - origin.x) / scaleX, (screen.y - origin.y) / scaleY};
    };

    const Vec2 mouse = toCanvas(ImGui::GetIO().MousePos);
    if (surfaceHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        bool resize = false;
        if (UI::UIWidget* selected = world->get<UI::UIWidget>(ctx.selectedEntity)) {
            if (selected->type != UI::UIWidgetType::Canvas && selected->computedRect.isValid()) {
                const float hx = origin.x + (selected->computedRect.position.x + selected->computedRect.size.x) * scaleX;
                const float hy = origin.y + (selected->computedRect.position.y + selected->computedRect.size.y) * scaleY;
                const ImVec2 pointer = ImGui::GetIO().MousePos;
                resize = pointer.x >= hx - 12.0f && pointer.x <= hx + 4.0f &&
                         pointer.y >= hy - 12.0f && pointer.y <= hy + 4.0f;
            }
        }
        if (resize) {
            m_resizing = true;
            m_dragging = false;
            m_dragEntity = ctx.selectedEntity.id();
        } else {
            ECS::Entity hit = hitWidget(*world, mouse);
            if (hit.isValid()) {
                ctx.selectEntity(hit);
                m_dragging = true;
                m_resizing = false;
                m_dragEntity = hit.id();
            }
        }
    }

    if ((m_dragging || m_resizing) && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ECS::Entity entity(m_dragEntity, world);
        if (UI::UIWidget* widget = world->get<UI::UIWidget>(entity)) {
            if (!m_undoOpen) {
                ctx.beginUndo(EditorCommand::MoveEntity, m_dragEntity, *world);
                m_undoOpen = true;
            }
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            const Vec2 canvasDelta{delta.x / scaleX, delta.y / scaleY};
            if (m_resizing) resizeWidget(*widget, canvasDelta);
            else moveWidget(*widget, canvasDelta);
            ctx.isDirty = true;
            layout.layout(*world);
        }
    }

    if ((m_dragging || m_resizing) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (m_undoOpen) {
            ctx.endUndo(*world);
            m_undoOpen = false;
        }
        m_dragging = false;
        m_resizing = false;
        m_dragEntity = UI::kUIInvalidParent;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y), IM_COL32(16, 18, 24, 255));
    for (f32 x = 0.0f; x <= reference.x; x += 64.0f) {
        const float sx = origin.x + x * scaleX;
        drawList->AddLine(ImVec2(sx, origin.y), ImVec2(sx, origin.y + canvas.y), IM_COL32(40, 44, 56, 255));
    }
    for (f32 y = 0.0f; y <= reference.y; y += 64.0f) {
        const float sy = origin.y + y * scaleY;
        drawList->AddLine(ImVec2(origin.x, sy), ImVec2(origin.x + canvas.x, sy), IM_COL32(40, 44, 56, 255));
    }
    UI::drawWidgets(*world, drawList, origin, canvas);
    drawList->AddRect(origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y), IM_COL32(90, 98, 120, 255));

    if (ctx.selectedEntity.isValid()) {
        if (const UI::UIWidget* selected = world->get<UI::UIWidget>(ctx.selectedEntity)) {
            if (selected->type != UI::UIWidgetType::Canvas && selected->computedRect.isValid()) {
                const ImVec2 p0(origin.x + selected->computedRect.position.x * scaleX,
                                origin.y + selected->computedRect.position.y * scaleY);
                const ImVec2 p1(p0.x + selected->computedRect.size.x * scaleX,
                                p0.y + selected->computedRect.size.y * scaleY);
                drawList->AddRect(p0, p1, IM_COL32(120, 180, 255, 255), 0.0f, 0, 2.0f);
                drawList->AddRectFilled(ImVec2(p1.x - 8.0f, p1.y - 8.0f), p1, IM_COL32(120, 180, 255, 255));
            }
        }
    }

    if (!m_dragging && !m_resizing && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
        ctx.selectedEntity.isValid() && world->get<UI::UIWidget>(ctx.selectedEntity)) {
        ctx.beginUndo(EditorCommand::RemoveEntity, ctx.selectedEntity.id(), *world);
        world->destroy(ctx.selectedEntity);
        ctx.selectEntity(ECS::Entity::INVALID);
        ctx.isDirty = true;
        ctx.endUndo(*world);
    }

    ImGui::SetCursorScreenPos(ImVec2(cursor.x, origin.y + canvas.y + 6.0f));
    ImGui::Text("Canvas %.0f x %.0f", reference.x, reference.y);
    (void)cursor;
    ImGui::EndChild();
    ImGui::End();
#else
    (void)ctx;
#endif
}

}  // namespace Caffeine::Editor
