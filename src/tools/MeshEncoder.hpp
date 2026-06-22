#pragma once

#include "tools/PipelineTypes.hpp"
#include "assets/MeshLoader.hpp"
#include "assets/MeshTypes.hpp"
#include "core/io/CafWriter.hpp"
#include "core/io/Crc32.hpp"
#include <vector>
#include <cstdio>
#include <string_view>
#include <cstring>

namespace Caffeine::Tools {
using namespace Caffeine;

class MeshEncoder {
public:
    static ConversionResult encodeRaw(
        std::string_view outputPath,
        const Assets::Mesh3D& mesh,
        const MeshEncodeOptions& opts = {})
    {
        (void)opts;
        ConversionResult result;
        
        if (mesh.vertices.empty()) {
            result.errorMessage = "Invalid mesh: empty vertex data";
            return result;
        }
        
        MeshMetadata meta;
        meta.vertexCount = static_cast<u32>(mesh.vertices.size());
        meta.indexCount = static_cast<u32>(mesh.indices.size());
        meta.subMeshCount = static_cast<u32>(mesh.subMeshes.size());
        meta.reserved = 0;
        
        std::vector<u8> payload;
        u64 vertexDataSize = mesh.vertices.size() * sizeof(Assets::Vertex3D);
        u64 indexDataSize = mesh.indices.size() * sizeof(u32);
        payload.resize(vertexDataSize + indexDataSize);
        
        std::memcpy(payload.data(), mesh.vertices.data(), vertexDataSize);
        std::memcpy(payload.data() + vertexDataSize, mesh.indices.data(), indexDataSize);
        
        auto writeResult = IO::CafWriter::write(
            outputPath.data(),
            AssetType::Mesh,
            CAF_FLAG_NONE,
            &meta, sizeof(meta),
            payload.data(), payload.size()
        );
        
        result.success = writeResult.success;
        if (!writeResult.success) {
            result.errorMessage = "Failed to write CAF file";
        }
        result.inputBytes = payload.size();
        result.outputBytes = writeResult.bytesWritten;
        result.compressionRatio = result.inputBytes > 0 
            ? static_cast<f32>(result.outputBytes) / static_cast<f32>(result.inputBytes) 
            : 0.0f;
        
        return result;
    }
    
    ConversionResult encode(std::string_view inputPath,
                            std::string_view outputPath,
                            const MeshEncodeOptions& opts = {})
    {
        std::string pathStr(inputPath);
        
        if (pathStr.size() >= 4 && pathStr.substr(pathStr.size() - 4) == ".obj") {
            return encodeObj(inputPath, outputPath, opts);
        }
        
        if (pathStr.size() >= 5 && pathStr.substr(pathStr.size() - 5) == ".gltf") {
            return encodeGltf(inputPath, outputPath, opts);
        }
        
        if (pathStr.size() >= 4 && pathStr.substr(pathStr.size() - 4) == ".glb") {
            return encodeGltf(inputPath, outputPath, opts);
        }
        
        ConversionResult result;
        result.errorMessage = "Unsupported mesh format (only .obj, .gltf, .glb supported)";
        return result;
    }

private:
    ConversionResult encodeObj(std::string_view inputPath,
                               std::string_view outputPath,
                               const MeshEncodeOptions& opts)
    {
        ConversionResult result;
        
        FILE* f = std::fopen(inputPath.data(), "rb");
        if (!f) {
            result.errorMessage = "Failed to open OBJ file";
            return result;
        }
        
        std::fseek(f, 0, SEEK_END);
        long fileSize = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        
        if (fileSize <= 0) {
            std::fclose(f);
            result.errorMessage = "Empty OBJ file";
            return result;
        }
        
        std::vector<char> buffer(fileSize + 1);
        std::fread(buffer.data(), 1, fileSize, f);
        std::fclose(f);
        buffer[fileSize] = '\0';
        
        Assets::Mesh3D* mesh = Assets::MeshLoader::parseOBJ(buffer.data(), fileSize);
        if (!mesh) {
            result.errorMessage = "Failed to parse OBJ file";
            return result;
        }
        
        result = encodeRaw(outputPath, *mesh, opts);
        delete mesh;
        
        return result;
    }
    
    ConversionResult encodeGltf(std::string_view inputPath,
                                std::string_view outputPath,
                                const MeshEncodeOptions& opts)
    {
        FILE* f = std::fopen(inputPath.data(), "rb");
        if (!f) {
            ConversionResult result;
            result.errorMessage = "Failed to open glTF file";
            return result;
        }
        
        std::fseek(f, 0, SEEK_END);
        long fileSize = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        
        if (fileSize <= 0) {
            std::fclose(f);
            ConversionResult result;
            result.errorMessage = "Empty glTF file";
            return result;
        }
        
        std::vector<u8> buffer(fileSize);
        std::fread(buffer.data(), 1, fileSize, f);
        std::fclose(f);
        
        Assets::Mesh3D* mesh = Assets::MeshLoader::parseGLTF(
            buffer.data(), 
            buffer.size(),
            inputPath.data()
        );
        
        if (!mesh) {
            ConversionResult result;
            result.errorMessage = "Failed to parse glTF file";
            return result;
        }
        
        auto result = encodeRaw(outputPath, *mesh, opts);
        delete mesh;
        
        return result;
    }
};

} // namespace Caffeine::Tools
