#pragma once

/**
 * Caffeine UI — cubic curve editor.
 *
 * CurveEditor draws a 0..1 curve with two draggable Bezier handles. The host
 * supplies the sampler used for the displayed line and receives the new handle
 * positions, so animation/curve types stay outside the kit.
 */

#include "caffeine/ui/Element.hpp"

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace Caffeine::UIKit::Elements {

class CurveEditor : public Element {
public:
    explicit CurveEditor(std::string id = {}) {
        if (!id.empty()) setId(std::move(id));
    }

    const char* type() const override { return "CurveEditor"; }

    CurveEditor& setCurve(float x1, float y1, float x2, float y2) {
        m_x1 = x1;
        m_y1 = y1;
        m_x2 = x2;
        m_y2 = y2;
        return *this;
    }
    CurveEditor& setShowHandles(bool show) {
        m_showHandles = show;
        return *this;
    }
    CurveEditor& setSampler(std::function<float(float)> sampler) {
        m_sampler = std::move(sampler);
        return *this;
    }
    CurveEditor& setOnChange(std::function<void(float, float, float, float)> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override {
        const float scale = std::max(ctx.scale(), 0.001f);
        float width = resolvedWidth() * scale;
        float height = size().height * scale;
        if (width <= 0.0f) width = 240.0f * scale;
        if (height <= 0.0f) height = 120.0f * scale;
        width = std::max(width, 80.0f * scale);
        height = std::max(height, 60.0f * scale);

        ImGui::PushID(id().c_str());
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##curve", ImVec2(width, height));
        const bool active = ImGui::IsItemActive();
        const ImGuiIO& io = ImGui::GetIO();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const float pad = 10.0f * scale;
        const float vMin = -0.25f;
        const float vMax = 1.25f;
        auto toScreen = [&](float u, float v) {
            return ImVec2(pos.x + pad + u * (width - pad * 2.0f),
                          pos.y + height - pad -
                              ((v - vMin) / (vMax - vMin)) * (height - pad * 2.0f));
        };

        drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                                IM_COL32(26, 28, 34, 255), 4.0f);
        drawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                          IM_COL32(60, 64, 74, 255), 4.0f);
        drawList->AddLine(toScreen(0.0f, 0.0f), toScreen(1.0f, 0.0f),
                          IM_COL32(70, 74, 86, 255));
        drawList->AddLine(toScreen(0.0f, 1.0f), toScreen(1.0f, 1.0f),
                          IM_COL32(70, 74, 86, 255));
        drawList->AddLine(toScreen(0.0f, 0.0f), toScreen(1.0f, 1.0f),
                          IM_COL32(70, 74, 86, 140));

        const ImVec2 h1 = toScreen(m_x1, m_y1);
        const ImVec2 h2 = toScreen(m_x2, m_y2);
        if (m_showHandles) {
            drawList->AddLine(toScreen(0.0f, 0.0f), h1, IM_COL32(120, 180, 255, 200), 1.2f);
            drawList->AddLine(toScreen(1.0f, 1.0f), h2, IM_COL32(120, 180, 255, 200), 1.2f);
        }

        constexpr int kSamples = 49;
        ImVec2 points[kSamples];
        for (int i = 0; i < kSamples; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kSamples - 1);
            const float v = m_sampler ? m_sampler(u) : u;
            points[i] = toScreen(u, std::clamp(v, -0.2f, 1.2f));
        }
        drawList->AddPolyline(points, kSamples, IM_COL32(255, 210, 60, 255), 0, 2.0f);

        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID handleKey = ImGui::GetID("##handle");
        int handle = storage->GetInt(handleKey, 0);
        if (m_showHandles) {
            drawList->AddCircleFilled(h1, handle == 1 ? 6.0f * scale : 5.0f * scale,
                                      IM_COL32(160, 205, 255, 255), 16);
            drawList->AddCircleFilled(h2, handle == 2 ? 6.0f * scale : 5.0f * scale,
                                      IM_COL32(160, 205, 255, 255), 16);
        }

        if (ImGui::IsItemClicked() && m_showHandles) {
            auto distance2 = [&](ImVec2 p) {
                const float dx = io.MousePos.x - p.x;
                const float dy = io.MousePos.y - p.y;
                return dx * dx + dy * dy;
            };
            const float d1 = distance2(h1);
            const float d2 = distance2(h2);
            const float pick = 14.0f * scale;
            handle = std::min(d1, d2) < pick * pick ? (d1 <= d2 ? 1 : 2) : 0;
        }
        if (!active) handle = 0;

        bool changed = false;
        if (active && handle != 0) {
            const float u = std::clamp(
                (io.MousePos.x - pos.x - pad) / (width - pad * 2.0f), 0.0f, 1.0f);
            const float v = std::clamp(
                vMin + ((pos.y + height - pad - io.MousePos.y) / (height - pad * 2.0f)) *
                           (vMax - vMin),
                vMin, vMax);
            if (handle == 1) {
                m_x1 = u;
                m_y1 = v;
            } else {
                m_x2 = u;
                m_y2 = v;
            }
            changed = true;
        }
        storage->SetInt(handleKey, handle);

        setChanged(changed);
        setHovered(ImGui::IsItemHovered());
        setFocused(ImGui::IsItemFocused());
        if (changed && m_onChange) m_onChange(m_x1, m_y1, m_x2, m_y2);
        ImGui::PopID();
    }

private:
    float m_x1 = 0.33f;
    float m_y1 = 0.33f;
    float m_x2 = 0.66f;
    float m_y2 = 0.66f;
    bool m_showHandles = true;
    std::function<float(float)> m_sampler;
    std::function<void(float, float, float, float)> m_onChange;
};

}  // namespace Caffeine::UIKit::Elements
