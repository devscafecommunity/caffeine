#pragma once

/**
 * Caffeine UI — display-only elements.
 *
 * Labels, paragraphs, badges, progress bars, separators, spacers, images and
 * empty states. They never own editor state; they only render a value or a
 * message, which makes them safe to reuse in panels, cards and plugin UIs.
 */

#include "caffeine/ui/Element.hpp"
#include "caffeine/ui/Widgets.hpp"

#include <imgui.h>

#include <string>
#include <utility>

namespace Caffeine::UIKit::Elements {

enum class TextTone {
    Normal,
    Muted,
    Disabled,
    Accent,
    Success,
    Warning,
    Danger,
};

class Label : public Element {
public:
    explicit Label(std::string text, TextTone tone = TextTone::Normal, bool wrap = false)
        : m_text(std::move(text)), m_tone(tone), m_wrap(wrap) {}

    const char* type() const override { return "Label"; }

    Label& setText(std::string text) {
        m_text = std::move(text);
        return *this;
    }
    Label& setTone(TextTone tone) {
        m_tone = tone;
        return *this;
    }
    Label& setWrap(bool wrap) {
        m_wrap = wrap;
        return *this;
    }
    Label& setHeading(bool heading) {
        m_heading = heading;
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_text;
    TextTone m_tone = TextTone::Normal;
    bool m_wrap = false;
    bool m_heading = false;
};

using Text = Label;

class Paragraph : public Label {
public:
    explicit Paragraph(std::string text)
        : Label(std::move(text), TextTone::Muted, true) {}
    const char* type() const override { return "Paragraph"; }
};

class Badge : public Element {
public:
    explicit Badge(std::string text, ImVec4 color = ImVec4(0.0f, 0.0f, 0.0f, 0.0f))
        : m_text(std::move(text)), m_color(color) {}

    const char* type() const override { return "Badge"; }
    void draw(UIContext& ctx) override;

private:
    std::string m_text;
    ImVec4 m_color;
};

class StatusDot : public Element {
public:
    explicit StatusDot(ImVec4 color) : m_color(color) {}
    const char* type() const override { return "StatusDot"; }
    void draw(UIContext&) override;

private:
    ImVec4 m_color;
};

class ProgressBar : public Element {
public:
    explicit ProgressBar(float* value, std::string overlay = {})
        : m_value(value), m_overlay(std::move(overlay)) {}

    const char* type() const override { return "ProgressBar"; }
    void draw(UIContext& ctx) override;

private:
    float* m_value = nullptr;
    std::string m_overlay;
};

class Separator : public Element {
public:
    explicit Separator(bool spaced = false) : m_spaced(spaced) {}
    const char* type() const override { return "Separator"; }
    void draw(UIContext&) override;

private:
    bool m_spaced = false;
};

class Spacer : public Element {
public:
    explicit Spacer(float width = 0.0f, float height = 0.0f) {
        setWidth(width);
        setHeight(height);
    }
    const char* type() const override { return "Spacer"; }
    void draw(UIContext& ctx) override;
};

class Image : public Element {
public:
    Image(ImTextureRef texture, ImVec2 size, ImVec4 tint = ImVec4(1, 1, 1, 1))
        : m_texture(texture), m_logicalSize(size), m_tint(tint) {}

    const char* type() const override { return "Image"; }
    void draw(UIContext&) override;

private:
    ImTextureRef m_texture;
    ImVec2 m_logicalSize{0.0f, 0.0f};
    ImVec4 m_tint{1.0f, 1.0f, 1.0f, 1.0f};
};

class EmptyState : public Element {
public:
    explicit EmptyState(std::string title, std::string subtitle = {}, std::string icon = {})
        : m_title(std::move(title)),
          m_subtitle(std::move(subtitle)),
          m_icon(std::move(icon)) {}

    const char* type() const override { return "EmptyState"; }
    void draw(UIContext&) override;

private:
    std::string m_title;
    std::string m_subtitle;
    std::string m_icon;
};

class KeyValue : public Element {
public:
    KeyValue(std::string key, std::string value)
        : m_key(std::move(key)), m_value(std::move(value)) {}

    const char* type() const override { return "KeyValue"; }
    void draw(UIContext&) override;

private:
    std::string m_key;
    std::string m_value;
};

// ── Inline implementation ──────────────────────────────────────────────────

inline ImVec4 textToneColor(TextTone tone, const ThemeTokens& theme) {
    switch (tone) {
        case TextTone::Muted: return theme.textMuted;
        case TextTone::Disabled: return theme.textDisabled;
        case TextTone::Accent: return theme.accent;
        case TextTone::Success: return theme.success;
        case TextTone::Warning: return theme.warning;
        case TextTone::Danger: return theme.danger;
        case TextTone::Normal:
        default: return theme.text;
    }
}

inline void Label::draw(UIContext& ctx) {
    const ImVec4 color = textToneColor(m_heading ? TextTone::Accent : m_tone, ctx.theme());
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    if (m_wrap) {
        ImGui::TextWrapped("%s", m_text.c_str());
    } else {
        ImGui::TextUnformatted(m_text.c_str());
    }
    if (!tooltip().empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip().c_str());
    }
    ImGui::PopStyleColor();
    if (m_heading) ImGui::Separator();
}

inline void Badge::draw(UIContext& ctx) {
    const ImVec4 color =
        (m_color.w > 0.0f) ? m_color : ctx.theme().accentMuted;
    UIKit::Badge(m_text.c_str(), color);
}

inline void StatusDot::draw(UIContext&) { UIKit::StatusDot(m_color); }

inline void ProgressBar::draw(UIContext&) {
    if (!m_value) return;
    const ImVec2 size = controlSize(*this);
    ImGui::ProgressBar(*m_value, size,
                       m_overlay.empty() ? nullptr : m_overlay.c_str());
}

inline void Separator::draw(UIContext&) {
    if (m_spaced) {
        UIKit::SeparatorSpaced();
    } else {
        ImGui::Separator();
    }
}

inline void Spacer::draw(UIContext& ctx) {
    const float scale = std::max(ctx.scale(), 0.001f);
    ImGui::Dummy(ImVec2(size().width * scale, size().height * scale));
}

inline void Image::draw(UIContext& ctx) {
    const float scale = std::max(ctx.scale(), 0.001f);
    ImGui::Image(m_texture, ImVec2(m_logicalSize.x * scale, m_logicalSize.y * scale),
                 ImVec2(0, 0), ImVec2(1, 1), m_tint,
                 ImVec4(0, 0, 0, 0));
}

inline void EmptyState::draw(UIContext&) {
    UIKit::EmptyState(m_title.c_str(),
                      m_subtitle.empty() ? nullptr : m_subtitle.c_str(),
                      m_icon.empty() ? nullptr : m_icon.c_str());
}

inline void KeyValue::draw(UIContext& ctx) {
    ImGui::TextDisabled("%s", m_key.c_str());
    ImGui::SameLine();
    ImGui::TextColored(ctx.theme().text, "%s", m_value.c_str());
}

}  // namespace Caffeine::UIKit::Elements
