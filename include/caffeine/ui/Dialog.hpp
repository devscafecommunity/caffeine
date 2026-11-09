#pragma once

/**
 * Caffeine UI — modal dialogs.
 *
 * A Dialog is a popup that owns a body element tree and a close lifecycle. The
 * host calls open()/close(); the dialog opens an ImGui popup on the next frame,
 * draws its children, and invokes onClose when the user closes it with the title
 * bar X, Escape or a footer button.
 *
 * Command Palette, File Picker, Build and Project Startup use this element
 * instead of hand-rolled BeginPopupModal blocks.
 */

#include "caffeine/ui/Container.hpp"

#include <imgui.h>

#include <functional>
#include <string>
#include <utility>

namespace Caffeine::UIKit::Elements {

class Dialog : public Container {
public:
    explicit Dialog(std::string title)
        : Container(Axis::Vertical), m_title(std::move(title)) {
        setId("Dialog:" + m_title);
    }

    const char* type() const override { return "Dialog"; }

    Dialog& setInitialSize(ImVec2 size) {
        m_initialSize = size;
        return *this;
    }
    Dialog& setCenterTop(float ratio) {
        m_centerTop = true;
        m_topRatio = ratio;
        return *this;
    }
    Dialog& setOnClose(std::function<void()> callback) {
        m_onClose = std::move(callback);
        return *this;
    }

    void open() {
        m_open = true;
        m_requestOpen = true;
    }
    void close() {
        m_open = false;
        m_requestClose = true;
    }
    bool isOpen() const { return m_open; }

    void draw(UIContext& ctx) override;

private:
    std::string m_title;
    bool m_open = false;
    bool m_requestOpen = false;
    bool m_requestClose = false;
    bool m_centerTop = false;
    float m_topRatio = 0.22f;
    ImVec2 m_initialSize{0.0f, 0.0f};
    std::function<void()> m_onClose;
};

inline void Dialog::draw(UIContext& ctx) {
    if (m_requestOpen) {
        ImGui::OpenPopup(m_title.c_str());
        m_requestOpen = false;
    }
    if (!m_open) return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (m_centerTop) {
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * m_topRatio),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    } else {
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }
    if (m_initialSize.x > 0.0f && m_initialSize.y > 0.0f) {
        ImGui::SetNextWindowSize(m_initialSize, ImGuiCond_Appearing);
    }

    bool open = m_open;
    if (ImGui::BeginPopupModal(m_title.c_str(), &open,
                               ImGuiWindowFlags_NoSavedSettings)) {
        if (m_requestClose) {
            ImGui::CloseCurrentPopup();
            m_requestClose = false;
        }
        drawChildren(ctx);
        ImGui::EndPopup();
    }
    if (!open) {
        const bool wasOpen = m_open;
        m_open = false;
        if (wasOpen && m_onClose) m_onClose();
    }
}

}  // namespace Caffeine::UIKit::Elements
