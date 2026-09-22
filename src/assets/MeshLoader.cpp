#include "assets/MeshLoader.hpp"
#include "assets/MeshLOD.hpp"
#include "assets/MeshNormals.hpp"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <stb/stb_image.h>

#include <cstring>
#include <cstdio>
#include <algorithm>

namespace Caffeine::Assets {
using namespace Caffeine;

namespace {

void ensureGltfImagesLoaded(tinygltf::Model& model, const std::string& basePath) {
    for (auto& image : model.images) {
        if (!image.image.empty()) continue;
        if (image.uri.empty() || image.uri.rfind("data:", 0) == 0) continue;

        const std::string path = basePath + image.uri;
        int width = 0;
        int height = 0;
        int channels = 0;
        u8* imgData = stbi_load(path.c_str(), &width, &height, &channels, 0);
        if (!imgData) continue;

        image.width = width;
        image.height = height;
        image.component = channels;
        image.image.assign(imgData, imgData + static_cast<size_t>(width * height * channels));
        stbi_image_free(imgData);
    }
}

bool assignImageToMesh(const tinygltf::Image& image, Mesh3D* mesh) {
    if (!mesh || image.image.empty() || image.width <= 0 || image.height <= 0) {
        return false;
    }

    mesh->baseColorTexture = image.image;
    mesh->textureWidth = static_cast<u32>(image.width);
    mesh->textureHeight = static_cast<u32>(image.height);
    mesh->textureChannels = image.component > 0 ? image.component : 4;
    return true;
}

bool extractBaseColorTexture(const tinygltf::Model& model, int materialIndex, Mesh3D* mesh) {
    if (materialIndex < 0 || materialIndex >= static_cast<int>(model.materials.size())) {
        return false;
    }

    const auto& mat = model.materials[materialIndex];
    const int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
    if (texIndex < 0 || texIndex >= static_cast<int>(model.textures.size())) {
        return false;
    }

    const int imageIndex = model.textures[texIndex].source;
    if (imageIndex < 0 || imageIndex >= static_cast<int>(model.images.size())) {
        return false;
    }

    return assignImageToMesh(model.images[imageIndex], mesh);
}

}  // namespace

Mesh3D* MeshLoader::parseGLTF(const u8* data, usize dataLen, const char* filename,
                              std::string* outError) {
    if (!filename) {
        if (outError) *outError = "Dados glTF invalidos";
        return nullptr;
    }

    const std::string filenameStr(filename);
    const bool isGlb = filenameStr.ends_with(".glb");
    if (isGlb && (!data || dataLen == 0)) {
        if (outError) *outError = "Dados glTF invalidos";
        return nullptr;
    }
    
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    std::string basePath;
    
    size_t lastSlash = filenameStr.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        basePath = filenameStr.substr(0, lastSlash + 1);
    }
    
    bool success = false;
    
    if (isGlb) {
        success = loader.LoadBinaryFromMemory(&model, &err, &warn, 
                                             reinterpret_cast<const unsigned char*>(data), 
                                             static_cast<unsigned int>(dataLen),
                                             basePath);
    } else {
        success = loader.LoadASCIIFromFile(&model, &err, &warn, filenameStr);
    }
    
    if (!warn.empty() && outError && outError->empty()) {
        *outError = warn;
    }

    if (!success) {
        if (outError) {
            *outError = err.empty() ? "Falha ao carregar glTF" : err;
        }
        return nullptr;
    }

    if (model.meshes.empty()) {
        if (outError) *outError = "glTF nao contem meshes";
        return nullptr;
    }

    auto mesh = new Mesh3D();
    mesh->flipTextureV = false;
    mesh->materials.resize(model.materials.size());
    for (size_t mi = 0; mi < model.materials.size(); ++mi) {
        const auto& gmat = model.materials[mi];
        MeshSurfaceMaterial& mat = mesh->materials[mi];
        const auto& pbr = gmat.pbrMetallicRoughness;
        mat.albedoColor = Color{
            static_cast<f32>(pbr.baseColorFactor[0]),
            static_cast<f32>(pbr.baseColorFactor[1]),
            static_cast<f32>(pbr.baseColorFactor[2]),
            static_cast<f32>(pbr.baseColorFactor[3]),
        };
        mat.metallic = static_cast<f32>(pbr.metallicFactor);
        mat.roughness = static_cast<f32>(pbr.roughnessFactor);
        mat.doubleSided = gmat.doubleSided;

        const int texIndex = pbr.baseColorTexture.index;
        if (texIndex < 0 || texIndex >= static_cast<int>(model.textures.size())) continue;

        const int imageIndex = model.textures[texIndex].source;
        if (imageIndex < 0 || imageIndex >= static_cast<int>(model.images.size())) continue;

        const auto& image = model.images[imageIndex];
        if (!image.image.empty()) {
            mat.albedoPixels = image.image;
            mat.albedoWidth = static_cast<u32>(image.width);
            mat.albedoHeight = static_cast<u32>(image.height);
            mat.albedoChannels = image.component > 0 ? image.component : 4;
        } else if (!image.uri.empty()) {
            mat.albedoPath = basePath + image.uri;
        }
    }
    std::vector<Vertex3D> vertices;
    std::vector<u32> indices;
    int primaryMaterialIndex = -1;
    bool generateSmoothNormals = false;

    if (!basePath.empty()) {
        ensureGltfImagesLoaded(model, basePath);
    }

    u32 indexOffset = 0;
    
    for (const auto& gltfMesh : model.meshes) {
        for (const auto& primitive : gltfMesh.primitives) {
        if (primitive.material >= 0 && primaryMaterialIndex < 0) {
            primaryMaterialIndex = primitive.material;
        }

        auto posIt = primitive.attributes.find("POSITION");
        auto normIt = primitive.attributes.find("NORMAL");
        auto texIt = primitive.attributes.find("TEXCOORD_0");
        
        if (posIt == primitive.attributes.end()) {
            continue;
        }
        if (normIt == primitive.attributes.end()) {
            generateSmoothNormals = true;
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
        if (outError) *outError = "glTF sem vertices validos";
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
    
    if (!extractBaseColorTexture(model, primaryMaterialIndex, mesh)) {
        for (const auto& texture : model.textures) {
            if (texture.source >= 0 && texture.source < static_cast<int>(model.images.size())) {
                if (assignImageToMesh(model.images[texture.source], mesh)) {
                    break;
                }
            }
        }
    }
    
     MeshLOD::generateLODs(mesh, 3);

    if (generateSmoothNormals) {
        computeSmoothNormals(*mesh);
    }

    computeMeshTangents(*mesh);
    computeBounds(*mesh);

    return mesh;
}

bool MeshLoader::loadTextureFromFile(Mesh3D* mesh, const char* imagePath) {
    if (!mesh || !imagePath) return false;

    int width = 0;
    int height = 0;
    int channels = 0;
    u8* data = stbi_load(imagePath, &width, &height, &channels, 0);
    if (!data) return false;

    mesh->baseColorTexture.assign(data, data + static_cast<size_t>(width * height * channels));
    mesh->textureWidth = static_cast<u32>(width);
    mesh->textureHeight = static_cast<u32>(height);
    mesh->textureChannels = channels;
    stbi_image_free(data);
    return true;
}

void MeshLoader::loadPNGTexture(Mesh3D* mesh, const char* pngPath) {
    loadTextureFromFile(mesh, pngPath);
}

}
