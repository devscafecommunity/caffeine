#include "editor/DragDropSystem.hpp"
#include "debug/LogSystem.hpp"
#include <cstdlib>
#include <ctime>

namespace Caffeine::Editor {

FileImportCallback DragDropManager::s_importCallback = nullptr;

void DragDropManager::importFilesToCapPack(
    const std::vector<std::filesystem::path>& files,
    const std::filesystem::path& projectRoot
) {
    if (files.empty()) {
        if (s_importCallback) {
            s_importCallback(false, "No files selected");
        }
        return;
    }

    auto tempDir = std::filesystem::temp_directory_path() / 
                   ("caf_import_" + std::to_string(std::time(nullptr)));
    
    try {
        std::filesystem::create_directory(tempDir);
        
        for (const auto& file : files) {
            auto dest = tempDir / file.filename();
            std::filesystem::copy(file, dest);
        }
        
        std::filesystem::path capPath = projectRoot / "game.cap";
        std::string command = "./caf-pack --input " + tempDir.string() + 
                             " --output " + capPath.string();
        
        int result = std::system(command.c_str());
        
        std::filesystem::remove_all(tempDir);
        
        bool success = (result == 0);
        std::string message = success ? 
            "Imported " + std::to_string(files.size()) + " assets" :
            "Import failed: caf-pack error";
        
        if (s_importCallback) {
            s_importCallback(success, message);
        }
    } catch (const std::exception& e) {
        std::filesystem::remove_all(tempDir);
        if (s_importCallback) {
            s_importCallback(false, std::string("Import error: ") + e.what());
        }
    }
}

} // namespace Caffeine::Editor

