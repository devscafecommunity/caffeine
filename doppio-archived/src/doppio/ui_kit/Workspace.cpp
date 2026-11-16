#include "caffeine/ui/Workspace.hpp"

#include "caffeine/ui/PanelRegistry.hpp"

#include "Json.hpp"

#include <imgui_internal.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Caffeine::UIKit {

namespace {

DockArea parseDockArea(const std::string& value, DockArea fallback) {
    if (value == "left") return DockArea::Left;
    if (value == "right") return DockArea::Right;
    if (value == "bottom") return DockArea::Bottom;
    if (value == "center") return DockArea::Center;
    if (value == "floating") return DockArea::Floating;
    return fallback;
}

const char* dockAreaName(DockArea area) {
    switch (area) {
        case DockArea::Left: return "left";
        case DockArea::Right: return "right";
        case DockArea::Bottom: return "bottom";
        case DockArea::Floating: return "floating";
        case DockArea::Center:
        default: return "center";
    }
}

void parseArea(const Detail::JsonValue& root, const char* key, float& size, bool& enabled,
               std::unordered_map<std::string, DockArea>& assignments, DockArea area) {
    const Detail::JsonValue* node = root.find(key);
    if (!node) return;
    if (node->isNumber()) {
        size = static_cast<float>(node->number);
        return;
    }
    if (!node->isObject()) return;

    if (const Detail::JsonValue* value = node->find("size")) {
        if (value->isNumber()) size = static_cast<float>(value->number);
    }
    if (const Detail::JsonValue* value = node->find("enabled")) {
        if (value->isBool()) enabled = value->boolean;
    }
    if (const Detail::JsonValue* panels = node->find("panels")) {
        if (panels->isArray()) {
            for (const auto& entry : panels->array) {
                if (entry.isString()) assignments[entry.string] = area;
            }
        }
    }
}

long long fileTimestamp(const std::string& path) {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<long long>(time.time_since_epoch().count());
}

}  // namespace

bool Workspace::load(const std::string& path, std::string* outError) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (outError) *outError = "cannot open workspace: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    Detail::JsonValue root;
    std::string parseError;
    if (!Detail::parseJson(buffer.str(), root, &parseError) || !root.isObject()) {
        if (outError) *outError = "invalid workspace JSON: " + parseError;
        return false;
    }

    Config config;
    parseArea(root, "left", config.leftSize, config.leftEnabled, config.assignments,
              DockArea::Left);
    parseArea(root, "right", config.rightSize, config.rightEnabled, config.assignments,
              DockArea::Right);
    parseArea(root, "bottom", config.bottomSize, config.bottomEnabled, config.assignments,
              DockArea::Bottom);
    float ignoredSize = 0.0f;
    bool ignoredEnabled = true;
    parseArea(root, "center", ignoredSize, ignoredEnabled, config.assignments,
              DockArea::Center);

    if (const Detail::JsonValue* plugins = root.find("plugins")) {
        if (plugins->isObject()) {
            if (const Detail::JsonValue* dock = plugins->find("dock")) {
                if (dock->isString()) {
                    config.pluginDock = parseDockArea(dock->string, config.pluginDock);
                }
            }
            if (const Detail::JsonValue* size = plugins->find("size")) {
                if (size->isNumber()) config.pluginSize = static_cast<float>(size->number);
            }
        }
    }

    if (const Detail::JsonValue* assignments = root.find("assignments")) {
        if (assignments->isObject()) {
            for (const auto& entry : assignments->object) {
                if (entry.second.isString()) {
                    config.assignments[entry.first] =
                        parseDockArea(entry.second.string, DockArea::Center);
                }
            }
        }
    }

    m_config = std::move(config);
    m_path = path;
    m_mtime = fileTimestamp(path);
    m_loaded = true;
    return true;
}

void Workspace::reset() {
    m_config = Config{};
    m_loaded = false;
    m_mtime = 0;
}

bool Workspace::poll() {
    if (m_path.empty()) return false;
    const long long stamp = fileTimestamp(m_path);
    if (stamp == 0 || stamp == m_mtime) return false;
    m_mtime = stamp;
    std::string error;
    return load(m_path, &error);
}

void Workspace::apply(ImGuiID dockspaceId, PanelRegistry& registry,
                      float leftSize, float rightSize, float bottomSize) {
    const float left = leftSize > 0.0f ? leftSize : m_config.leftSize;
    const float right = rightSize > 0.0f ? rightSize : m_config.rightSize;
    const float bottom = bottomSize > 0.0f ? bottomSize : m_config.bottomSize;

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    ImGuiID center = dockspaceId;
    ImGuiID leftNode = 0;
    ImGuiID rightNode = 0;
    ImGuiID bottomNode = 0;

    if (m_config.leftEnabled && left > 0.0f) {
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, left, &leftNode, &center);
    }
    if (m_config.rightEnabled && right > 0.0f) {
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, right, &rightNode, &center);
    }
    if (m_config.bottomEnabled && bottom > 0.0f) {
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, bottom, &bottomNode, &center);
    }

    for (const auto& panel : registry.panels()) {
        if (panel.title.empty()) continue;

        DockArea area = panel.dock;
        const auto assignment = m_config.assignments.find(panel.id);
        if (assignment != m_config.assignments.end()) {
            area = assignment->second;
        } else if (!panel.owner.empty()) {
            area = m_config.pluginDock;
        }

        ImGuiID node = center;
        switch (area) {
            case DockArea::Left: node = leftNode ? leftNode : center; break;
            case DockArea::Right: node = rightNode ? rightNode : center; break;
            case DockArea::Bottom: node = bottomNode ? bottomNode : center; break;
            case DockArea::Center:
            case DockArea::Floating:
            default: node = center; break;
        }

        if (area == DockArea::Floating) continue;
        ImGui::DockBuilderDockWindow(panel.title.c_str(), node);
    }

    ImGui::DockBuilderFinish(dockspaceId);
}

bool Workspace::save(const std::string& path) const {
    const std::string target = path.empty() ? m_path : path;
    if (target.empty()) return false;

    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(target).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);

    std::ostringstream out;
    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"left\": { \"size\": " << m_config.leftSize
        << ", \"enabled\": " << (m_config.leftEnabled ? "true" : "false") << " },\n";
    out << "  \"right\": { \"size\": " << m_config.rightSize
        << ", \"enabled\": " << (m_config.rightEnabled ? "true" : "false") << " },\n";
    out << "  \"bottom\": { \"size\": " << m_config.bottomSize
        << ", \"enabled\": " << (m_config.bottomEnabled ? "true" : "false") << " },\n";
    out << "  \"plugins\": { \"dock\": \"" << dockAreaName(m_config.pluginDock)
        << "\", \"size\": " << m_config.pluginSize << " },\n";
    out << "  \"assignments\": {\n";
    bool first = true;
    for (const auto& [id, area] : m_config.assignments) {
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << id << "\": \"" << dockAreaName(area) << "\"";
    }
    out << "\n  }\n";
    out << "}\n";

    std::ofstream file(target, std::ios::binary);
    if (!file.is_open()) return false;
    file << out.str();
    return true;
}

bool Workspace::saveTo(const std::string& path) {
    if (path.empty() || !save(path)) return false;
    m_path = path;
    m_mtime = fileTimestamp(path);
    m_loaded = true;
    return true;
}

std::string Workspace::userConfigPath() {
#ifdef _WIN32
    if (const char* appData = std::getenv("APPDATA")) {
        if (appData[0] != '\0') {
            return (std::filesystem::path(appData) / "Caffeine" / "ui" / "workspace.json")
                .generic_string();
        }
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (xdg[0] != '\0') {
            return (std::filesystem::path(xdg) / "caffeine" / "ui" / "workspace.json")
                .generic_string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        if (home[0] != '\0') {
            return (std::filesystem::path(home) / ".config" / "caffeine" / "ui" /
                    "workspace.json")
                .generic_string();
        }
    }
#endif
    return "caffeine/ui/workspace.json";
}

}  // namespace Caffeine::UIKit
