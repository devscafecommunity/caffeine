#include "editor/UiGalleryPanel.hpp"

#include <imgui.h>

#include <cmath>
#include <string>
#include <vector>

namespace Caffeine::Editor {

using namespace Caffeine::UIKit;
using namespace Caffeine::UIKit::UI;

UiGalleryPanel::UiGalleryPanel() = default;
UiGalleryPanel::~UiGalleryPanel() = default;

void UiGalleryPanel::build() {
    m_root = std::make_unique<Panel>("UI Gallery", "Composable element kit", "effects",
                                     &m_open);

    auto& toolbar = m_root->emplace<Toolbar>("gallery_toolbar");
    toolbar.emplace<Button>("Primary", ButtonVariant::Primary)
        .setOnClick([this] { m_status = "Primary clicked"; });
    toolbar.emplace<Button>("Secondary")
        .setOnClick([this] { m_status = "Secondary clicked"; });
    toolbar.emplace<Button>("Danger", ButtonVariant::Danger)
        .setOnClick([this] { m_status = "Danger clicked"; });
    toolbar.emplace<IconButton>("arrows-diagonal", "Refresh preview")
        .setOnClick([this] { m_status = "Preview refreshed"; });
    toolbar.emplace<IconButton>("bell", "Search")
        .setOnClick([this] { m_status = "Search"; });

    auto& content = m_root->emplace<Row>("gallery_content");

    auto& controls = content.emplace<Card>("Controls");
    controls.setGrow(true);
    controls.emplace<Label>("Interactive controls", TextTone::Accent)
        .setHeading(true);
    auto& buttonRow = controls.emplace<Row>("button_row");
    buttonRow.emplace<Button>("Default");
    buttonRow.emplace<Button>("Primary", ButtonVariant::Primary);
    buttonRow.emplace<Button>("Danger", ButtonVariant::Danger);
    controls.emplace<Checkbox>("Enable feature", &m_toggle);
    controls.emplace<SliderFloat>("Amount", &m_slider, 0.0f, 1.0f, "%.2f");
    auto& modeCombo = controls.emplace<UI::Combo>(
        "Mode", &m_mode, std::vector<std::string>{"One", "Two", "Three"});
    (void)modeCombo;
    controls.emplace<SegmentedControl>("segment", &m_segment,
                                       std::vector<std::string>{"2D", "3D", "Iso"});
    controls.emplace<UI::SearchField>(m_search, "Search elements...");
    controls.emplace<ColorPicker>("Tint", m_color);
    m_statusLabel = &controls.emplace<Label>(m_status, TextTone::Muted);

    auto& display = content.emplace<Card>("Display");
    display.setGrow(true);
    display.emplace<Label>("Display elements", TextTone::Accent).setHeading(true);
    display.emplace<Label>("Normal text");
    display.emplace<Label>("Muted text", TextTone::Muted);
    display.emplace<Label>("Success text", TextTone::Success);
    display.emplace<Label>("Warning text", TextTone::Warning);
    display.emplace<Label>("Danger text", TextTone::Danger);
    auto& badgeRow = display.emplace<Row>("badges");
    badgeRow.emplace<UI::Badge>("NEW", tokens().accent);
    badgeRow.emplace<UI::Badge>("BETA", tokens().info);
    display.emplace<ProgressBar>(&m_progress, "Progress");
    display.emplace<UI::Paragraph>(
        "Paragraphs wrap and use the muted token. Elements compose vertically "
        "inside a card and horizontally inside a row.");
    display.emplace<KeyValue>("Selected entity", "Natural Water");
    display.emplace<KeyValue>("Elements", "36");

    auto& properties = m_root->emplace<Card>("Properties");
    properties.emplace<Label>("Typed property grid", TextTone::Accent).setHeading(true);
    auto& grid = properties.emplace<PropertyGrid>("gallery_properties");
    grid.typedRow<FloatProperty>("Wind", m_slider, 0.0f, 1.0f, 0.01f, "%.2f", true);
    grid.typedRow<IntProperty>("Resolution", m_count, 8, 256);
    grid.typedRow<BoolProperty>("Enabled", m_toggle);
    grid.typedRow<ChoiceProperty>("Mode", m_mode,
                                  std::vector<std::string>{"One", "Two", "Three"});
    grid.typedRow<ColorFieldProperty>("Tint", m_color);
    grid.typedRow<TextFieldProperty>("Name", m_name, 128);

    auto& canvasRow = m_root->emplace<Row>("canvas_row");

    auto& canvasCard = canvasRow.emplace<Card>("Canvas");
    canvasCard.setGrow(true);
    canvasCard.emplace<Label>("Canvas element", TextTone::Accent).setHeading(true);
    auto& canvas = canvasCard.emplace<Canvas>("gallery_canvas");
    canvas.setHeight(190.0f);
    canvas.setOnDraw([this](UIContext& ctx, ImDrawList* drawList, const ImVec2& origin,
                            const ImVec2& size) {
        const ImU32 grid = ImGui::ColorConvertFloat4ToU32(ctx.theme().border);
        const ImU32 wave = ImGui::ColorConvertFloat4ToU32(ctx.theme().accent);
        for (float x = 0.0f; x <= size.x; x += 24.0f) {
            drawList->AddLine(ImVec2(origin.x + x, origin.y),
                              ImVec2(origin.x + x, origin.y + size.y), grid);
        }
        for (float y = 0.0f; y <= size.y; y += 24.0f) {
            drawList->AddLine(ImVec2(origin.x, origin.y + y),
                              ImVec2(origin.x + size.x, origin.y + y), grid);
        }
        ImVec2 previous(origin.x, origin.y + size.y * 0.5f);
        for (float x = 1.0f; x <= size.x; x += 2.0f) {
            const float phase = x / std::max(size.x, 1.0f) * 4.0f * 3.14159265f;
            const float y = origin.y + size.y * 0.5f +
                            std::sin(phase + static_cast<float>(ImGui::GetTime()) * 2.0f) *
                                size.y * 0.25f;
            const ImVec2 point(origin.x + x, y);
            drawList->AddLine(previous, point, wave, 2.0f);
            previous = point;
        }
    });

    auto& nodeCard = canvasRow.emplace<Card>("Node canvas");
    nodeCard.setGrow(true);
    nodeCard.emplace<Label>("Node canvas element", TextTone::Accent).setHeading(true);
    auto& nodes = nodeCard.emplace<NodeCanvasView>("gallery_nodes");
    nodes.setHeight(220.0f);
    auto& start = nodes.emplace<NodeElement>(1, "On Update", "event",
                                             IM_COL32(86, 110, 160, 255));
    start.emplace<OutputPin>(1, "Out");
    start.emplace<Label>("Every frame");
    auto& apply = nodes.emplace<NodeElement>(2, "Set Position", "transform",
                                             IM_COL32(52, 96, 128, 255));
    apply.emplace<InputPin>(2, "In");
    apply.emplace<InputPin>(3, "X");
    apply.emplace<OutputPin>(4, "Out");
    apply.emplace<SliderFloat>("", &m_slider, 0.0f, 1.0f, "%.2f");

    auto& debugCard = m_root->emplace<Card>("Debug");
    debugCard.emplace<Label>("Element debugger", TextTone::Accent).setHeading(true);
    debugCard.emplace<Checkbox>("Draw hit rectangles", &m_debugOverlay);
    debugCard.emplace<DebugPanel>(m_context);
    m_tickLabel = &debugCard.emplace<Label>("", TextTone::Muted);

    m_built = true;
}

void UiGalleryPanel::render() {
    if (!m_open) return;
    if (!m_built) build();

    m_progress = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 1.5f);
    if (m_statusLabel) m_statusLabel->setText(m_status);
    if (m_tickLabel) {
        m_tickLabel->setText("Elements draw every frame; the tree is retained.");
    }

    m_context.setDebugEnabled(true);
    m_context.draw(*m_root);
    if (m_debugOverlay) m_context.drawDebugOverlay(ImGui::GetForegroundDrawList());
}

}  // namespace Caffeine::Editor
