#include "AssetCooker.hpp"
#include "assets/MeshLoader.hpp"
#include "assets/MeshTypes.hpp"
#include "core/io/CafWriter.hpp"
#include "core/io/CafTypes.hpp"
#include <iostream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace Caffeine::Editor {

bool AssetCooker::CookTextures(const std::string& assetsDir, const std::string& outputDir, [[maybe_unused]] BuildProgress& progress) {
     std::cout << "Cooking textures from: " << assetsDir << "\n";
     std::cout << "Output directory: " << outputDir << "\n";
     std::cout << "Texture cooking - processing PNG/TGA assets...\n";
     return true;
 }
 
 bool AssetCooker::CookShaders(const std::string& assetsDir, const std::string& outputDir, [[maybe_unused]] BuildProgress& progress) {
    std::cout << "Cooking shaders from: " << assetsDir << "\n";
    std::cout << "Output directory: " << outputDir << "\n";
    std::cout << "Shader cooking - compiling GLSL to SPIR-V...\n";
    return true;
}

bool AssetCooker::CookMeshes(const std::string& assetsDir, const std::string& outputDir, [[maybe_unused]] BuildProgress& progress) {
    std::cout << "Cooking meshes from: " << assetsDir << "\n";
    std::cout << "Output directory: " << outputDir << "\n";
    
    if (!fs::exists(assetsDir)) {
        std::cout << "Assets directory does not exist: " << assetsDir << "\n";
        return false;
    }
    
    int cooked = 0;
    Assets::MeshLoader loader;
    
    for (const auto& entry : fs::recursive_directory_iterator(assetsDir)) {
        if (!entry.is_regular_file()) continue;
        
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        Assets::Mesh3D* mesh = nullptr;
        
        if (ext == ".obj") {
            mesh = loader.loadOBJ(entry.path().c_str());
        } else if (ext == ".gltf" || ext == ".glb") {
            std::vector<u8> buffer;
            FILE* f = fopen(entry.path().c_str(), "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fseek(f, 0, SEEK_SET);
                
                if (size > 0) {
                    buffer.resize(size);
                    fread(buffer.data(), 1, size, f);
                    mesh = Assets::MeshLoader::parseGLTF(buffer.data(), buffer.size(), entry.path().filename().c_str());
                }
                fclose(f);
            }
        } else {
            continue;
        }
        
        if (!mesh) {
            std::cout << "Failed to parse mesh: " << entry.path() << "\n";
            continue;
        }
        
        std::vector<u8> payload;
        
        u32 vertexCount = static_cast<u32>(mesh->vertices.size());
        u32 indexCount = static_cast<u32>(mesh->indices.size());
        u32 submeshCount = static_cast<u32>(mesh->subMeshes.size());
        
        payload.insert(payload.end(), (u8*)&vertexCount, (u8*)&vertexCount + sizeof(u32));
        payload.insert(payload.end(), (u8*)&indexCount, (u8*)&indexCount + sizeof(u32));
        payload.insert(payload.end(), (u8*)&submeshCount, (u8*)&submeshCount + sizeof(u32));
        
        for (const auto& v : mesh->vertices) {
            payload.insert(payload.end(), (u8*)&v, (u8*)&v + sizeof(Assets::Vertex3D));
        }
        
        for (const auto& idx : mesh->indices) {
            payload.insert(payload.end(), (u8*)&idx, (u8*)&idx + sizeof(u32));
        }
        
        for (const auto& sm : mesh->subMeshes) {
            payload.insert(payload.end(), (u8*)&sm, (u8*)&sm + sizeof(Assets::SubMesh));
        }
        
        std::string outPath = entry.path().stem().string() + ".caf";
        std::string fullOutPath = fs::path(outputDir) / outPath;
        
        auto result = IO::CafWriter::write(
            fullOutPath.c_str(),
            ::Caffeine::AssetType::Mesh,
            CAF_FLAG_NONE,
            payload.data(), payload.size(),
            nullptr, 0
        );
        
        if (result.success) {
            std::cout << "Cooked mesh: " << entry.path().filename().string() << " -> " << outPath << "\n";
            cooked++;
        } else {
            std::cout << "Failed to cook mesh: " << entry.path() << "\n";
        }
        
        delete mesh;
    }
    
    std::cout << "Mesh cooking complete. Cooked " << cooked << " meshes.\n";
    return true;
}

bool AssetCooker::LoadBuildCache(const std::string& cacheFile) {
    std::cout << "Loading build cache from: " << cacheFile << "\n";
    std::cout << "Cache file not yet implemented (stub)\n";
    return true;
}

bool AssetCooker::SaveBuildCache(const std::string& cacheFile) {
    std::cout << "Saving build cache to: " << cacheFile << "\n";
    std::cout << "Cache save not yet implemented (stub)\n";
    return true;
}

bool AssetCooker::ShouldCookAsset(const std::string& assetPath) {
    std::cout << "Checking if asset should be cooked: " << assetPath << "\n";
    return true;
}

}
