#include "AssetCooker.hpp"
#include "BuildLog.hpp"
#include "assets/MeshLoader.hpp"
#include "assets/MeshTypes.hpp"
#include "assets/TextureCompiler.hpp"
#include "core/io/CafWriter.hpp"
#include "core/io/CafTypes.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Caffeine::Editor {

namespace {
std::unordered_map<std::string, std::uintmax_t> s_cacheEntries;

std::uintmax_t fileMtime(const fs::path& path) {
    std::error_code ec;
    return fs::last_write_time(path, ec).time_since_epoch().count();
}

bool isTextureExt(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga";
}

} // namespace

bool AssetCooker::CookTextures(const std::string& assetsDir, const std::string& outputDir, BuildProgress& progress) {
    if (!fs::exists(assetsDir)) {
        BuildLog::info("No raw assets directory for textures");
        return true;
    }

    fs::create_directories(outputDir);
    Assets::TextureCompiler compiler;

    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(assetsDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (isTextureExt(ext)) files.push_back(entry.path());
    }

    const usize total = files.size();
    usize cooked = 0;
    usize skipped = 0;

    for (usize i = 0; i < files.size(); ++i) {
        const fs::path& src = files[i];
        const fs::path rel = fs::relative(src, assetsDir);
        fs::path dst = fs::path(outputDir) / rel;
        dst.replace_extension(".caf");

        progress.currentTask = "Cooking texture: " + src.filename().string();
        BuildLog::setTask(progress.currentTask);
        progress.progress.store(0.20f + 0.20f * static_cast<f32>(i) / static_cast<f32>(std::max<usize>(total, 1)),
                              std::memory_order_relaxed);

        if (!ShouldCookAsset(src.string())) {
            ++skipped;
            continue;
        }

        fs::create_directories(dst.parent_path());
        Assets::AssetImportContext ctx;
        ctx.SourcePath = src;
        ctx.DestinationPath = dst;
        if (!compiler.Compile(ctx)) {
            for (const auto& line : ctx.Logs) BuildLog::error(line);
            return false;
        }
        for (const auto& line : ctx.Logs) BuildLog::info(line);
        s_cacheEntries[src.string()] = fileMtime(src);
        ++cooked;
    }

    BuildLog::info("Textures cooked: " + std::to_string(cooked) + ", skipped: " + std::to_string(skipped));
    return true;
}

bool AssetCooker::CookShaders(const std::string& assetsDir, const std::string& outputDir, BuildProgress& progress) {
    (void)assetsDir;
    (void)outputDir;
    (void)progress;
    BuildLog::info("Shader cooking not yet implemented — skipped");
    return true;
}

bool AssetCooker::CookMeshes(const std::string& assetsDir, const std::string& outputDir, BuildProgress& progress) {
    if (!fs::exists(assetsDir)) {
        BuildLog::info("No raw assets directory for meshes");
        return true;
    }

    fs::create_directories(outputDir);
    int cooked = 0;
    int skipped = 0;
    Assets::MeshLoader loader;

    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(assetsDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {
            files.push_back(entry.path());
        }
    }

    for (usize i = 0; i < files.size(); ++i) {
        const fs::path& entry = files[i];
        progress.currentTask = "Cooking mesh: " + entry.filename().string();
        BuildLog::setTask(progress.currentTask);
        progress.progress.store(0.40f + 0.20f * static_cast<f32>(i) / static_cast<f32>(std::max<usize>(files.size(), 1)),
                              std::memory_order_relaxed);

        if (!ShouldCookAsset(entry.string())) {
            ++skipped;
            continue;
        }

        Assets::Mesh3D* mesh = nullptr;
        const std::string ext = entry.extension().string();
        if (ext == ".obj") {
            mesh = loader.loadOBJ(entry.c_str());
        } else if (ext == ".glb") {
            std::vector<u8> buffer;
            FILE* f = fopen(entry.c_str(), "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                const long size = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (size > 0) {
                    buffer.resize(static_cast<size_t>(size));
                    fread(buffer.data(), 1, static_cast<size_t>(size), f);
                    mesh = Assets::MeshLoader::parseGLTF(buffer.data(), buffer.size(),
                                                         entry.string().c_str());
                }
                fclose(f);
            }
        } else {
            mesh = Assets::MeshLoader::parseGLTF(nullptr, 0, entry.string().c_str());
        }

        if (!mesh) {
            BuildLog::warn("Failed to parse mesh: " + entry.string());
            continue;
        }

        std::vector<u8> payload;
        const u32 vertexCount = static_cast<u32>(mesh->vertices.size());
        const u32 indexCount = static_cast<u32>(mesh->indices.size());
        const u32 submeshCount = static_cast<u32>(mesh->subMeshes.size());
        payload.insert(payload.end(), reinterpret_cast<const u8*>(&vertexCount),
                       reinterpret_cast<const u8*>(&vertexCount) + sizeof(u32));
        payload.insert(payload.end(), reinterpret_cast<const u8*>(&indexCount),
                       reinterpret_cast<const u8*>(&indexCount) + sizeof(u32));
        payload.insert(payload.end(), reinterpret_cast<const u8*>(&submeshCount),
                       reinterpret_cast<const u8*>(&submeshCount) + sizeof(u32));
        for (const auto& v : mesh->vertices) {
            payload.insert(payload.end(), reinterpret_cast<const u8*>(&v),
                           reinterpret_cast<const u8*>(&v) + sizeof(Assets::Vertex3D));
        }
        for (const auto& idx : mesh->indices) {
            payload.insert(payload.end(), reinterpret_cast<const u8*>(&idx),
                           reinterpret_cast<const u8*>(&idx) + sizeof(u32));
        }
        for (const auto& sm : mesh->subMeshes) {
            payload.insert(payload.end(), reinterpret_cast<const u8*>(&sm),
                           reinterpret_cast<const u8*>(&sm) + sizeof(Assets::SubMesh));
        }

        const fs::path rel = fs::relative(entry, assetsDir);
        fs::path outPath = fs::path(outputDir) / rel;
        outPath.replace_extension(".caf");
        fs::create_directories(outPath.parent_path());

        const auto result = IO::CafWriter::write(
            outPath.string().c_str(),
            ::Caffeine::AssetType::Mesh,
            CAF_FLAG_NONE,
            payload.data(), payload.size(),
            nullptr, 0);

        delete mesh;

        if (!result.success) {
            BuildLog::error("Failed to cook mesh: " + entry.string());
            return false;
        }

        s_cacheEntries[entry.string()] = fileMtime(entry);
        ++cooked;
        BuildLog::info("Cooked mesh: " + entry.filename().string());
    }

    BuildLog::info("Meshes cooked: " + std::to_string(cooked) + ", skipped: " + std::to_string(skipped));
    return true;
}

bool AssetCooker::LoadBuildCache(const std::string& cacheFile) {
    s_cacheEntries.clear();
    if (cacheFile.empty() || !fs::exists(cacheFile)) return true;

    std::ifstream in(cacheFile);
    std::string line;
    while (std::getline(in, line)) {
        const auto sep = line.find('=');
        if (sep == std::string::npos) continue;
        const std::string path = line.substr(0, sep);
        const std::uintmax_t mtime = std::stoull(line.substr(sep + 1));
        s_cacheEntries[path] = mtime;
    }
    BuildLog::info("Loaded build cache (" + std::to_string(s_cacheEntries.size()) + " entries)");
    return true;
}

bool AssetCooker::SaveBuildCache(const std::string& cacheFile) {
    if (cacheFile.empty()) return true;
    fs::create_directories(fs::path(cacheFile).parent_path());
    std::ofstream out(cacheFile);
    for (const auto& [path, mtime] : s_cacheEntries) {
        out << path << '=' << mtime << '\n';
    }
    return out.good();
}

bool AssetCooker::ShouldCookAsset(const std::string& assetPath) {
    const auto it = s_cacheEntries.find(assetPath);
    if (it == s_cacheEntries.end()) return true;
    return it->second != fileMtime(assetPath);
}

} // namespace Caffeine::Editor
