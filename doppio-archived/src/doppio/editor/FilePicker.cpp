#include "editor/FilePicker.hpp"
#include "editor/EditorIcons.hpp"
#include "caffeine/ui/UI.hpp"

#ifdef CF_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Caffeine::Editor {

#ifdef CF_HAS_IMGUI

namespace {

std::unordered_set<std::string> g_filePickerCloseEvents;

struct PickerState {
    std::filesystem::path currentPath;
    std::filesystem::path treeRoot;
    std::filesystem::path anchorPath;
    std::vector<std::filesystem::path> entries;
    std::string searchFilter;
    std::string pathInput;
    std::string fileNameInput;
    bool isOpen = true;
    bool entriesDirty = true;
    std::unique_ptr<Caffeine::UIKit::Elements::Dialog> dialog;
};

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::filesystem::path homeDirectory() {
#ifdef _WIN32
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) return std::filesystem::path(userProfile);
#else
    const char* home = std::getenv("HOME");
    if (home) return std::filesystem::path(home);
#endif
    return std::filesystem::current_path();
}

std::filesystem::path resolveUserPath(const std::string& input) {
    if (input.empty()) return {};

    std::filesystem::path path(input);
    std::string str = input;
    if (!str.empty() && str[0] == '~') {
        const auto home = homeDirectory();
        if (str.size() == 1) {
            path = home;
        } else if (str[1] == '/' || str[1] == '\\') {
            path = home / str.substr(2);
        }
    }

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return std::filesystem::weakly_canonical(path, ec);
    }

    const auto absolute = std::filesystem::absolute(path, ec);
    if (!ec && std::filesystem::exists(absolute, ec)) {
        return std::filesystem::weakly_canonical(absolute, ec);
    }
    return absolute;
}

std::filesystem::path computeTreeRoot(const std::filesystem::path& start) {
    std::error_code ec;
    auto dir = start;
    if (!std::filesystem::is_directory(dir, ec)) {
        if (dir.has_parent_path()) {
            dir = dir.parent_path();
        }
    }

    const auto home = homeDirectory();
    std::filesystem::path root = dir;
    for (int i = 0; i < 24 && root.has_parent_path(); ++i) {
        if (root == home) break;
        const auto parent = root.parent_path();
        if (parent == root) break;
        root = parent;
    }
    return root;
}

void syncPathInput(PickerState& state) {
    std::error_code ec;
    state.pathInput = std::filesystem::weakly_canonical(state.currentPath, ec).string();
    if (ec) state.pathInput = state.currentPath.string();
}

bool navigateTo(PickerState& state, const std::filesystem::path& target) {
    std::error_code ec;
    if (target.empty() || !std::filesystem::exists(target, ec)) return false;
    if (!std::filesystem::is_directory(target, ec)) return false;

    state.currentPath = std::filesystem::weakly_canonical(target, ec);
    if (ec) state.currentPath = target;
    syncPathInput(state);
    state.entriesDirty = true;
    return true;
}

void refreshEntries(PickerState& state) {
    state.entries.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(state.currentPath, ec)) {
        state.entriesDirty = false;
        return;
    }

    const std::string lowerFilter = toLower(state.searchFilter);
    try {
        for (const auto& entry :
             std::filesystem::directory_iterator(state.currentPath,
                                               std::filesystem::directory_options::skip_permission_denied,
                                               ec)) {
            const std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue;

            if (!lowerFilter.empty() && toLower(name).find(lowerFilter) == std::string::npos) {
                continue;
            }
            state.entries.push_back(entry.path());
        }
    } catch (const std::exception&) {
        state.entriesDirty = false;
        return;
    }

    std::sort(state.entries.begin(), state.entries.end(), [](const auto& a, const auto& b) {
        std::error_code ecA;
        std::error_code ecB;
        const bool aIsDir = std::filesystem::is_directory(a, ecA);
        const bool bIsDir = std::filesystem::is_directory(b, ecB);
        if (aIsDir != bIsDir) return aIsDir;
        return a.filename().string() < b.filename().string();
    });

    state.entriesDirty = false;
}

const char* iconForPath(const std::filesystem::path& path, bool isDirectory) {
    if (isDirectory) return EditorIcon::Folder;

    const std::string ext = toLower(path.extension().string());
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
        ext == ".webp" || ext == ".gif") {
        return "beer";
    }
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") {
        return "bell";
    }
    if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".dae") {
        return "arrows-diagonal";
    }
    if (ext == ".caf" || ext == ".scene") {
        return "account";
    }
    if (ext == ".lua" || ext == ".py" || ext == ".js" || ext == ".ts") {
        return "at";
    }
    if (ext == ".cap") {
        return "arrow-down-square";
    }
    if (ext == ".json" || ext == ".txt" || ext == ".md") {
        return "align-left";
    }
    return "alert-circle";
}

void drawFileIcon(const std::filesystem::path& path, bool isDirectory, f32 size) {
    const char* icon = iconForPath(path, isDirectory);
    if (EditorIcons::hasIcon(icon)) {
        EditorIcons::image(icon, size);
    } else if (isDirectory) {
        ImGui::TextUnformatted("[dir]");
    } else {
        ImGui::TextUnformatted("[file]");
    }
}

bool isPathPrefix(const std::filesystem::path& prefix, const std::filesystem::path& path) {
    std::error_code ec;
    auto rel = std::filesystem::relative(path, prefix, ec);
    if (ec || rel.empty()) return false;
    const auto relStr = rel.generic_string();
    return relStr.rfind("..", 0) != 0;
}

void drawDirectoryTree(const std::filesystem::path& dir, PickerState& state, int depth) {
    if (depth > 16) return;

    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;

    const std::string name = dir.filename().empty() ? dir.string() : dir.filename().string();
    const bool onPath = isPathPrefix(dir, state.currentPath) || dir == state.currentPath;
    const bool selected = (dir == state.currentPath);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (selected) flags |= ImGuiTreeNodeFlags_Selected;
    if (onPath) ImGui::SetNextItemOpen(true, ImGuiCond_Once);

    ImGui::PushID(dir.string().c_str());
    drawFileIcon(dir, true, ImGui::GetFontSize());
    ImGui::SameLine(0.0f, 4.0f);
    const bool open = ImGui::TreeNodeEx(name.c_str(), flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        navigateTo(state, dir);
    }

    if (open) {
        std::vector<std::filesystem::path> subdirs;
        for (const auto& entry :
             std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_directory(ec)) continue;
            const std::string subName = entry.path().filename().string();
            if (subName.empty() || subName[0] == '.') continue;
            subdirs.push_back(entry.path());
        }
        std::sort(subdirs.begin(), subdirs.end());
        for (const auto& sub : subdirs) {
            drawDirectoryTree(sub, state, depth + 1);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void renderBreadcrumbs(PickerState& state) {
    std::vector<std::filesystem::path> crumbs;
    std::error_code ec;
    auto rel = std::filesystem::relative(state.currentPath, state.treeRoot, ec);
    crumbs.push_back(state.treeRoot);
    if (!ec && !rel.empty() && rel.generic_string().rfind("..", 0) != 0) {
        for (const auto& part : rel) {
            crumbs.push_back(crumbs.back() / part);
        }
    } else if (state.currentPath != state.treeRoot) {
        crumbs.push_back(state.currentPath);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    for (size_t i = 0; i < crumbs.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextUnformatted("/");
            ImGui::SameLine(0.0f, 2.0f);
        }

        const std::string label =
            (i == 0) ? "Root" : crumbs[i].filename().string();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(label.c_str())) {
            navigateTo(state, crumbs[i]);
            ImGui::PopID();
            ImGui::PopStyleColor();
            return;
        }
        ImGui::PopID();
    }
    ImGui::PopStyleColor();
}

bool renderPathBar(PickerState& state) {
    bool navigated = false;

    if (ImGui::Button("Home", ImVec2(56.0f, 0.0f))) {
        navigateTo(state, homeDirectory());
        navigated = true;
    }
    ImGui::SameLine();

    std::error_code ec;
    const bool canGoUp = state.currentPath.has_parent_path() &&
                         state.currentPath != state.currentPath.root_path();
    if (!canGoUp) ImGui::BeginDisabled();
    if (EditorIcons::iconButton("arrow-small-up", "fp_up")) {
        if (navigateTo(state, state.currentPath.parent_path())) {
            navigated = true;
        }
    }
    if (!canGoUp) ImGui::EndDisabled();
    ImGui::SameLine();

    if (!state.anchorPath.empty()) {
        if (ImGui::Button("Anchor")) {
            navigateTo(state, state.anchorPath);
            navigated = true;
        }
        ImGui::SameLine();
    }

    ImGui::PushItemWidth(-70.0f);
    char pathBuf[1024] = {};
    std::strncpy(pathBuf, state.pathInput.c_str(), sizeof(pathBuf) - 1);
    const bool enterPressed =
        ImGui::InputTextWithHint("##fp_path", "Paste or type a path, press Enter...", pathBuf,
                                 sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();

    if (ImGui::Button("Go", ImVec2(60.0f, 0.0f)) || enterPressed) {
        const auto resolved = resolveUserPath(pathBuf);
        if (navigateTo(state, resolved)) {
            navigated = true;
        } else {
            state.pathInput = pathBuf;
        }
    } else if (pathBuf != state.pathInput) {
        state.pathInput = pathBuf;
    }

    return navigated;
}

void renderFileList(FilePicker::Mode mode, PickerState& state, std::optional<std::filesystem::path>& result) {
    if (state.entriesDirty) {
        refreshEntries(state);
    }

    char filterBuf[256] = {};
    std::strncpy(filterBuf, state.searchFilter.c_str(), sizeof(filterBuf) - 1);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##fp_filter", "Filter files and folders...", filterBuf, sizeof(filterBuf))) {
        state.searchFilter = filterBuf;
        state.entriesDirty = true;
    }

    ImGui::Spacing();

    if (ImGui::BeginTable("fp_list_table", 2,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        const f32 iconSize = ImGui::GetFontSize();
        for (size_t i = 0; i < state.entries.size(); ++i) {
            const auto& entry = state.entries[i];
            std::error_code ec;
            const bool isDir = std::filesystem::is_directory(entry, ec);
            const std::string name = entry.filename().string();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(i));

            drawFileIcon(entry, isDir, iconSize);
            ImGui::SameLine();
            if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                if (isDir) {
                    navigateTo(state, entry);
                } else if (mode != FilePicker::Mode::PickFolder) {
                    result = entry;
                    state.isOpen = false;
                }
            }

            if (isDir && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                navigateTo(state, entry);
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", isDir ? "Folder" : entry.extension().string().c_str());

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

}  // namespace

std::optional<std::filesystem::path> FilePicker::pickPath(
    Mode mode,
    const std::string& title,
    const std::filesystem::path& defaultPath
) {
    return pickPathImGui(mode, title, defaultPath);
}

bool FilePicker::consumeCloseEvent(const std::string& title) {
    auto it = g_filePickerCloseEvents.find(title);
    if (it == g_filePickerCloseEvents.end()) {
        return false;
    }
    g_filePickerCloseEvents.erase(it);
    return true;
}

std::optional<std::filesystem::path> FilePicker::pickPathNative(
    Mode mode,
    const std::string& title,
    const std::filesystem::path& defaultPath
) {
    return std::nullopt;
}

std::optional<std::filesystem::path> FilePicker::pickPathImGui(
    Mode mode,
    const std::string& title,
    const std::filesystem::path& defaultPath
) {
    static std::unordered_map<std::string, PickerState> states;

    auto it = states.find(title);
    if (it == states.end()) {
        std::filesystem::path start =
            defaultPath.empty() ? std::filesystem::current_path() : defaultPath;
        std::error_code ec;
        if (!std::filesystem::is_directory(start, ec) && start.has_parent_path()) {
            start = start.parent_path();
        }
        if (!std::filesystem::exists(start, ec)) start = homeDirectory();

        PickerState created;
        created.currentPath = start;
        created.anchorPath = start;
        created.treeRoot = computeTreeRoot(start);
        syncPathInput(created);
        created.entriesDirty = true;
        states.emplace(title, std::move(created));
        it = states.find(title);
    }

    PickerState& state = it->second;
    std::optional<std::filesystem::path> result;

    if (!state.isOpen) {
        states.erase(title);
        g_filePickerCloseEvents.insert(title);
        return std::nullopt;
    }

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    if (!state.dialog) {
        state.dialog = std::make_unique<Dialog>(title);
        state.dialog->setInitialSize(ImVec2(980.0f, 620.0f));
        state.dialog->setOnClose([&state] { state.isOpen = false; });
        state.dialog->open();
    }
    if (state.entriesDirty) refreshEntries(state);

    Dialog& dialog = *state.dialog;
    dialog.clear();

    auto& pathRow = dialog.emplace<Row>("fp_path_row");
    pathRow.emplace<Button>("Home").setOnClick([&state] {
        navigateTo(state, homeDirectory());
    });
    std::error_code upEc;
    const bool canGoUp = state.currentPath.has_parent_path() &&
                         state.currentPath != state.currentPath.root_path();
    auto& up = pathRow.emplace<IconButton>("arrow-small-up", "Parent folder");
    up.setEnabled(canGoUp);
    up.setOnClick([&state] {
        navigateTo(state, state.currentPath.parent_path());
    });
    if (!state.anchorPath.empty()) {
        pathRow.emplace<Button>("Anchor").setOnClick([&state] {
            navigateTo(state, state.anchorPath);
        });
    }
    auto& path = pathRow.emplace<TextInput>(state.pathInput, 1024);
    path.setGrow(true);
    pathRow.emplace<Button>("Go").setOnClick([&state] {
        const auto resolved = resolveUserPath(state.pathInput);
        if (!navigateTo(state, resolved)) {
            syncPathInput(state);
        }
    });

    dialog.emplace<Label>(state.currentPath.string(), TextTone::Muted).setWrap(true);
    dialog.emplace<Separator>();

    auto& body = dialog.emplace<Row>("fp_body");
    auto& folderCard = body.emplace<Card>("fp_folders");
    folderCard.setWidth(240.0f);
    folderCard.emplace<Label>("Folders", TextTone::Accent).setHeading(true);
    auto& folderList = folderCard.emplace<ListBox>("fp_folder_list");
    folderList.setHeight(420.0f);

    if (state.currentPath.has_parent_path() &&
        state.currentPath != state.currentPath.root_path()) {
        auto& parentItem = folderList.emplace<ListItem>("..");
        parentItem.setId("fp_parent");
        parentItem.setOnClick([&state] {
            navigateTo(state, state.currentPath.parent_path());
        });
    }
    {
        std::error_code ec;
        std::vector<std::filesystem::path> subdirs;
        for (const auto& entry : std::filesystem::directory_iterator(
                 state.currentPath,
                 std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_directory(ec)) continue;
            const std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            subdirs.push_back(entry.path());
        }
        std::sort(subdirs.begin(), subdirs.end());
        for (const auto& subdir : subdirs) {
            auto& item = folderList.emplace<ListItem>(subdir.filename().string());
            item.setId("fp_dir:" + subdir.string());
            item.setOnClick([&state, subdir] { navigateTo(state, subdir); });
        }
    }

    auto& contents = body.emplace<Card>("fp_contents");
    contents.setGrow(true);
    contents.emplace<Label>("Contents", TextTone::Accent).setHeading(true);
    auto& search = contents.emplace<Elements::SearchField>(
        state.searchFilter, "Filter files and folders...");
    search.setGrow(true);
    search.setOnChange([&state](const std::string&) { state.entriesDirty = true; });
    auto& fileList = contents.emplace<ListBox>("fp_file_list");
    fileList.setHeight(390.0f);

    for (const auto& entry : state.entries) {
        std::error_code ec;
        const bool isDir = std::filesystem::is_directory(entry, ec);
        std::string label = entry.filename().string();
        if (isDir) {
            label += "  [folder]";
        } else {
            label += "  [" + entry.extension().string() + "]";
        }
        auto& item = fileList.emplace<ListItem>(label, false);
        item.setId("fp_file:" + entry.string());
        item.setOnClick([&state, &result, mode, entry, isDir] {
            if (isDir) {
                navigateTo(state, entry);
                return;
            }
            if (mode == FilePicker::Mode::PickFolder) return;
            result = entry;
            state.isOpen = false;
            if (state.dialog) state.dialog->close();
        });
    }

    dialog.emplace<Separator>();
    if (mode == FilePicker::Mode::SaveFile) {
        dialog.emplace<TextFieldProperty>("File name", state.fileNameInput, 512);
    }
    auto& footer = dialog.emplace<Row>("fp_footer");
    if (mode == FilePicker::Mode::PickFolder) {
        footer.emplace<Button>("Select This Folder", ButtonVariant::Primary)
            .setOnClick([&state, &result] {
                result = state.currentPath;
                state.isOpen = false;
                if (state.dialog) state.dialog->close();
            });
    } else if (mode == FilePicker::Mode::SaveFile) {
        footer.emplace<Button>("Save Here", ButtonVariant::Primary)
            .setOnClick([&state, &result] {
                if (state.fileNameInput.empty()) return;
                result = state.currentPath / state.fileNameInput;
                state.isOpen = false;
                if (state.dialog) state.dialog->close();
            });
    }
    footer.emplace<Button>("Cancel").setOnClick([&state] {
        state.isOpen = false;
        if (state.dialog) state.dialog->close();
    });

    UIContext ui;
    ui.draw(dialog);

    if (!state.isOpen) {
        state.dialog->close();
        states.erase(title);
        g_filePickerCloseEvents.insert(title);
    }
    return result;
}

#else  // !CF_HAS_IMGUI

std::optional<std::filesystem::path> FilePicker::pickPath(
    Mode mode,
    const std::string& title,
    const std::filesystem::path& defaultPath
) {
    return std::nullopt;
}

bool FilePicker::consumeCloseEvent(const std::string& title) {
    return false;
}

std::optional<std::filesystem::path> FilePicker::pickPathNative(
    Mode mode,
    const std::string& title,
    const std::filesystem::path& defaultPath
) {
    return std::nullopt;
}

std::optional<std::filesystem::path> FilePicker::pickPathImGui(
    Mode mode,
    const std::string& title,
    const std::filesystem::path& defaultPath
) {
    return std::nullopt;
}

#endif  // CF_HAS_IMGUI

}  // namespace Caffeine::Editor
