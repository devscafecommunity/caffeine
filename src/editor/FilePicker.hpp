#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace Caffeine::Editor {

class FilePicker {
public:
    enum class Mode {
        PickFolder,
        PickFile,
        SaveFile
    };

    static std::optional<std::filesystem::path> pickPath(
        Mode mode,
        const std::string& title,
        const std::filesystem::path& defaultPath = "."
    );

private:
    static std::optional<std::filesystem::path> pickPathNative(
        Mode mode,
        const std::string& title,
        const std::filesystem::path& defaultPath
    );

    static std::optional<std::filesystem::path> pickPathImGui(
        Mode mode,
        const std::string& title,
        const std::filesystem::path& defaultPath
    );
};

} // namespace Caffeine::Editor
