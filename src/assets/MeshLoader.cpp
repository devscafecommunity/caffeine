#include "assets/MeshLoader.hpp"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <cstring>
#include <cstdio>
#include <algorithm>

namespace Caffeine::Assets {
using namespace Caffeine;

Mesh3D* MeshLoader::parseGLTF(const u8* data, usize dataLen, const char* filename) {
    if (!data || dataLen == 0 || !filename) {
        return nullptr;
    }
    
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    
    std::string filenameStr(filename);
    std::string basePath;
    
    size_t lastSlash = filenameStr.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        basePath = filenameStr.substr(0, lastSlash + 1);
    }
    
    bool success = false;
    
    if (filenameStr.ends_with(".glb")) {
        success = loader.LoadBinaryFromMemory(&model, &err, &warn, 
                                             reinterpret_cast<const unsigned char*>(data), 
                                             static_cast<unsigned int>(dataLen),
                                             basePath);
    } else {
        success = loader.LoadASCIIFromFile(&model, &err, &warn, filenameStr);
    }
    
    if (!success || model.meshes.empty()) {
        return nullptr;
    }
    
    auto mesh = new Mesh3D();
    std::vector<Vertex3D> vertices;
    std::vector<u32> indices;
    
    u32 indexOffset = 0;
    
    for (const auto& gltfMesh : model.meshes) {
        for (const auto& primitive : gltfMesh.primitives) {
        auto posIt = primitive.attributes.find("POSITION");
        auto normIt = primitive.attributes.find("NORMAL");
        auto texIt = primitive.attributes.find("TEXCOORD_0");
        
        if (posIt == primitive.attributes.end()) {
            continue;
        }
        
        const auto& posAccessor = model.accessors[posIt->second];
        const auto& posBufferView = model.bufferViews[posAccessor.bufferView];
        const auto& posBuffer = model.buffers[posBufferView.buffer];
        
        u32 vertexCount = static_cast<u32>(posAccessor.count);
        u32 vertStartIndex = vertices.size();
        
        const u8* posData = posBuffer.data.data() + posBufferView.byteOffset + posAccessor.byteOffset;
        const u8* normData = nullptr;
        const u8* texData = nullptr;
        
        if (normIt != primitive.attributes.end()) {
            const auto& normAccessor = model.accessors[normIt->second];
            const auto& normBufferView = model.bufferViews[normAccessor.bufferView];
            const auto& normBuffer = model.buffers[normBufferView.buffer];
            normData = normBuffer.data.data() + normBufferView.byteOffset + normAccessor.byteOffset;
        }
        
        if (texIt != primitive.attributes.end()) {
            const auto& texAccessor = model.accessors[texIt->second];
            const auto& texBufferView = model.bufferViews[texAccessor.bufferView];
            const auto& texBuffer = model.buffers[texBufferView.buffer];
            texData = texBuffer.data.data() + texBufferView.byteOffset + texAccessor.byteOffset;
        }
        
        for (u32 i = 0; i < vertexCount; ++i) {
            Vertex3D vert = {};
            
            const f32* pos = reinterpret_cast<const f32*>(posData + i * sizeof(f32) * 3);
            vert.position = Vec3(pos[0], pos[1], pos[2]);
            
            if (normData) {
                const f32* norm = reinterpret_cast<const f32*>(normData + i * sizeof(f32) * 3);
                vert.normal = Vec3(norm[0], norm[1], norm[2]);
            } else {
                vert.normal = Vec3(0.0f, 1.0f, 0.0f);
            }
            
            if (texData) {
                const f32* tex = reinterpret_cast<const f32*>(texData + i * sizeof(f32) * 2);
                vert.texcoord = Vec2(tex[0], tex[1]);
            } else {
                vert.texcoord = Vec2(0.0f, 0.0f);
            }
            
            vert.tangent = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
            
            vertices.push_back(vert);
        }
        
        if (primitive.indices >= 0) {
            const auto& idxAccessor = model.accessors[primitive.indices];
            const auto& idxBufferView = model.bufferViews[idxAccessor.bufferView];
            const auto& idxBuffer = model.buffers[idxBufferView.buffer];
            
            const u8* idxData = idxBuffer.data.data() + idxBufferView.byteOffset + idxAccessor.byteOffset;
            u32 indexCount = static_cast<u32>(idxAccessor.count);
            
            SubMesh submesh;
            submesh.indexOffset = indexOffset;
            submesh.indexCount = indexCount;
            submesh.materialIndex = std::max(0, primitive.material);
            mesh->subMeshes.push_back(submesh);
            
            for (u32 i = 0; i < indexCount; ++i) {
                u32 idx = 0;
                
                if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const u16* idxPtr = reinterpret_cast<const u16*>(idxData + i * sizeof(u16));
                    idx = *idxPtr + vertStartIndex;
                } else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    const u32* idxPtr = reinterpret_cast<const u32*>(idxData + i * sizeof(u32));
                    idx = *idxPtr + vertStartIndex;
                } else {
                    const u8* idxPtr = reinterpret_cast<const u8*>(idxData + i * sizeof(u8));
                    idx = *idxPtr + vertStartIndex;
                }
                
                indices.push_back(idx);
            }
            
            indexOffset += indexCount;
        }
    }
    }
    
    if (vertices.empty()) {
        delete mesh;
        return nullptr;
    }
    
    mesh->vertices = vertices;
    mesh->indices = indices;
    
    if (mesh->subMeshes.empty()) {
        SubMesh submesh;
        submesh.indexOffset = 0;
        submesh.indexCount = static_cast<u32>(indices.size());
        submesh.materialIndex = 0;
        mesh->subMeshes.push_back(submesh);
    }
    
    for (const auto& texture : model.textures) {
        if (texture.source >= 0 && texture.source < (int)model.images.size()) {
            const auto& image = model.images[texture.source];
            if (!image.image.empty()) {
                mesh->baseColorTexture = image.image;
                mesh->textureWidth = image.width;
                mesh->textureHeight = image.height;
                break;
            }
        }
    }
    
    computeBounds(*mesh);
    
    return mesh;
}

}
