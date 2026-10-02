#include "assets/MeshLoader.hpp"
#include "assets/MeshLOD.hpp"
#include "assets/MeshNormals.hpp"
#include "animation/SkinLibrary.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <stb/stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

const u8* accessorBytes(const tinygltf::Model& model, int accessorIndex, size_t& stride, size_t& count) {
    if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) return nullptr;
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return nullptr;
    }
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) return nullptr;
    const int components = tinygltf::GetNumComponentsInType(accessor.type);
    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    stride = view.byteStride != 0 ? static_cast<size_t>(view.byteStride)
                                  : static_cast<size_t>(components * componentSize);
    count = static_cast<size_t>(accessor.count);
    return model.buffers[view.buffer].data.data() + view.byteOffset + accessor.byteOffset;
}

bool invertMat4(const Mat4& input, Mat4& output) {
    f32 row[4][8] = {};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) row[r][c] = input(static_cast<usize>(r), static_cast<usize>(c));
        row[r][r + 4] = 1.0f;
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::fabs(row[r][col]) > std::fabs(row[pivot][col])) pivot = r;
        }
        if (std::fabs(row[pivot][col]) < 1.0e-8f) return false;
        if (pivot != col) {
            for (int c = 0; c < 8; ++c) std::swap(row[col][c], row[pivot][c]);
        }
        const f32 scale = row[col][col];
        for (int c = 0; c < 8; ++c) row[col][c] /= scale;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            const f32 factor = row[r][col];
            for (int c = 0; c < 8; ++c) row[r][c] -= factor * row[col][c];
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) output(static_cast<usize>(r), static_cast<usize>(c)) = row[r][c + 4];
    }
    return true;
}

Mat4 gltfNodeLocal(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        Mat4 matrix;
        for (int i = 0; i < 16; ++i) matrix.data()[i] = static_cast<f32>(node.matrix[static_cast<size_t>(i)]);
        return matrix;
    }
    Vec3 translation;
    Quat rotation = Quat::identity();
    Vec3 scale(1.0f, 1.0f, 1.0f);
    if (node.translation.size() == 3) {
        translation = Vec3(static_cast<f32>(node.translation[0]), static_cast<f32>(node.translation[1]),
                           static_cast<f32>(node.translation[2]));
    }
    if (node.rotation.size() == 4) {
        rotation = Quat(static_cast<f32>(node.rotation[0]), static_cast<f32>(node.rotation[1]),
                        static_cast<f32>(node.rotation[2]), static_cast<f32>(node.rotation[3]));
    }
    if (node.scale.size() == 3) {
        scale = Vec3(static_cast<f32>(node.scale[0]), static_cast<f32>(node.scale[1]),
                     static_cast<f32>(node.scale[2]));
    }
    return Mat4::translation(translation.x, translation.y, translation.z) * rotation.toMatrix() *
           Mat4::scale(scale.x, scale.y, scale.z);
}

void importGltfSkin(const tinygltf::Model& model, const std::string& path, Mesh3D* mesh) {
    if (!mesh || model.skins.empty() || model.nodes.empty()) return;
    const tinygltf::Skin& skin = model.skins[0];
    if (skin.joints.empty()) return;

    std::vector<int> parent(model.nodes.size(), -1);
    for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
        for (int child : model.nodes[nodeIndex].children) {
            if (child >= 0 && child < static_cast<int>(parent.size())) parent[static_cast<size_t>(child)] = static_cast<int>(nodeIndex);
        }
    }
    std::vector<int> boneOfNode(model.nodes.size(), -1);
    Animation::ImportedSkin imported;
    imported.skeleton.bones.resize(skin.joints.size());
    imported.boneNames.resize(skin.joints.size());
    for (size_t joint = 0; joint < skin.joints.size(); ++joint) {
        const int nodeIndex = skin.joints[joint];
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) continue;
        boneOfNode[static_cast<size_t>(nodeIndex)] = static_cast<int>(joint);
        const tinygltf::Node& node = model.nodes[nodeIndex];
        imported.boneNames[joint] = node.name.empty() ? ("bone_" + std::to_string(joint)) : node.name;
        imported.skeleton.bones[joint].name = imported.boneNames[joint].c_str();
        imported.skeleton.bones[joint].localTransform = gltfNodeLocal(node);
        imported.skeleton.bones[joint].bindPoseInverse = Mat4::identity();
    }
    for (size_t joint = 0; joint < skin.joints.size(); ++joint) {
        int cursor = parent[static_cast<size_t>(skin.joints[joint])];
        int parentBone = -1;
        while (cursor >= 0) {
            if (boneOfNode[static_cast<size_t>(cursor)] >= 0) {
                parentBone = boneOfNode[static_cast<size_t>(cursor)];
                break;
            }
            cursor = parent[static_cast<size_t>(cursor)];
        }
        imported.skeleton.bones[joint].parentIndex = parentBone;
    }

    std::vector<Mat4> nodeWorld(model.nodes.size(), Mat4::identity());
    std::vector<u8> visited(model.nodes.size(), 0);
    const auto walkNode = [&](auto&& self, int index, const Mat4& parentWorld) -> void {
        if (index < 0 || index >= static_cast<int>(model.nodes.size()) || visited[static_cast<size_t>(index)]) return;
        visited[static_cast<size_t>(index)] = 1;
        nodeWorld[static_cast<size_t>(index)] = parentWorld * gltfNodeLocal(model.nodes[static_cast<size_t>(index)]);
        for (int child : model.nodes[static_cast<size_t>(index)].children) self(self, child, nodeWorld[static_cast<size_t>(index)]);
    };
    for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
        if (parent[nodeIndex] < 0) walkNode(walkNode, static_cast<int>(nodeIndex), Mat4::identity());
    }
    for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
        if (model.nodes[nodeIndex].skin != 0) continue;
        Mat4 inverse;
        if (invertMat4(nodeWorld[nodeIndex], inverse)) imported.skeleton.skinSpace = inverse;
        break;
    }

    size_t stride = 0;
    size_t count = 0;
    if (const u8* bytes = accessorBytes(model, skin.inverseBindMatrices, stride, count)) {
        const size_t joints = std::min(count, imported.skeleton.bones.size());
        for (size_t joint = 0; joint < joints; ++joint) {
            Mat4 inverse;
            std::memcpy(inverse.data(), bytes + joint * stride, sizeof(f32) * 16);
            imported.skeleton.bones[joint].bindPoseInverse = inverse;
        }
    }

    mesh->skin.assign(mesh->vertices.size(), {});
    u32 vertexCursor = 0;
    for (const tinygltf::Mesh& gltfMesh : model.meshes) {
        for (const tinygltf::Primitive& primitive : gltfMesh.primitives) {
            const auto position = primitive.attributes.find("POSITION");
            if (position == primitive.attributes.end()) continue;
            const u32 vertexCount = static_cast<u32>(model.accessors[position->second].count);
            const auto joints = primitive.attributes.find("JOINTS_0");
            const auto weights = primitive.attributes.find("WEIGHTS_0");
            size_t jointStride = 0;
            size_t jointCount = 0;
            size_t weightStride = 0;
            size_t weightCount = 0;
            const u8* jointBytes = joints == primitive.attributes.end()
                                       ? nullptr
                                       : accessorBytes(model, joints->second, jointStride, jointCount);
            const u8* weightBytes = weights == primitive.attributes.end()
                                        ? nullptr
                                        : accessorBytes(model, weights->second, weightStride, weightCount);
            const int jointType = jointBytes ? model.accessors[joints->second].componentType : 0;
            for (u32 vertex = 0; vertex < vertexCount; ++vertex) {
                const size_t index = static_cast<size_t>(vertexCursor) + vertex;
                if (index >= mesh->skin.size()) break;
                VertexSkin influence;
                if (jointBytes && vertex < jointCount) {
                    for (int channel = 0; channel < 4; ++channel) {
                        u16 jointIndex = 0;
                        if (jointType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                            std::memcpy(&jointIndex, jointBytes + vertex * jointStride + channel * sizeof(u16), sizeof(u16));
                        } else {
                            jointIndex = jointBytes[vertex * jointStride + static_cast<size_t>(channel)];
                        }
                        influence.joints[channel] = jointIndex;
                    }
                }
                if (weightBytes && vertex < weightCount) {
                    std::memcpy(influence.weights, weightBytes + vertex * weightStride, sizeof(f32) * 4);
                }
                mesh->skin[index] = influence;
            }
            vertexCursor += vertexCount;
        }
    }

    for (const tinygltf::Animation& animation : model.animations) {
        Animation::SkeletalClip clip;
        clip.name = animation.name.empty() ? "Clip" : animation.name.c_str();
        clip.loop = true;
        f32 duration = 0.0f;
        for (const tinygltf::AnimationChannel& channel : animation.channels) {
            if (channel.target_node < 0 || channel.target_node >= static_cast<int>(boneOfNode.size())) continue;
            const int bone = boneOfNode[static_cast<size_t>(channel.target_node)];
            if (bone < 0 || channel.sampler < 0 || channel.sampler >= static_cast<int>(animation.samplers.size())) {
                continue;
            }
            const tinygltf::AnimationSampler& sampler = animation.samplers[channel.sampler];
            size_t timeStride = 0;
            size_t timeCount = 0;
            size_t valueStride = 0;
            size_t valueCount = 0;
            const u8* times = accessorBytes(model, sampler.input, timeStride, timeCount);
            const u8* values = accessorBytes(model, sampler.output, valueStride, valueCount);
            if (!times || !values || timeCount == 0) continue;
            const bool cubic = sampler.interpolation == "CUBICSPLINE";
            const int valuesPerKey = cubic ? 3 : 1;
            if (valueCount < timeCount * static_cast<size_t>(valuesPerKey)) continue;
            if (channel.target_path != "translation" && channel.target_path != "rotation" &&
                channel.target_path != "scale") {
                continue;
            }
            const int components = channel.target_path == "rotation" ? 4 : 3;
            const Mat4& rest = imported.skeleton.bones[static_cast<size_t>(bone)].localTransform;
            const Vec3 restPosition(rest(0, 3), rest(1, 3), rest(2, 3));
            const Vec3 restScale(Vec3(rest(0, 0), rest(1, 0), rest(2, 0)).length(),
                                 Vec3(rest(0, 1), rest(1, 1), rest(2, 1)).length(),
                                 Vec3(rest(0, 2), rest(1, 2), rest(2, 2)).length());
            const Quat restRotation = Quat::fromMatrix(rest);
            std::vector<Animation::SkeletalKeyframe> keys(timeCount);
            for (size_t key = 0; key < timeCount; ++key) {
                f32 time = 0.0f;
                std::memcpy(&time, times + key * timeStride, sizeof(f32));
                keys[key].time = time;
                keys[key].position = restPosition;
                keys[key].rotation = restRotation;
                keys[key].scale = restScale;
                duration = std::max(duration, time);
                const u8* value = values + (key * static_cast<size_t>(valuesPerKey) + (cubic ? 1 : 0)) * valueStride;
                f32 raw[4] = {0, 0, 0, 1};
                std::memcpy(raw, value, sizeof(f32) * static_cast<size_t>(components));
                if (channel.target_path == "translation") keys[key].position = Vec3(raw[0], raw[1], raw[2]);
                else if (channel.target_path == "scale") keys[key].scale = Vec3(raw[0], raw[1], raw[2]);
                else keys[key].rotation = Quat(raw[0], raw[1], raw[2], raw[3]);
            }
            const std::vector<Animation::SkeletalKeyframe>* existing = clip.channels.get(static_cast<u32>(bone));
            std::vector<Animation::SkeletalKeyframe> merged = existing ? *existing : std::vector<Animation::SkeletalKeyframe>{};
            if (merged.empty()) {
                merged = keys;
            } else {
                for (size_t key = 0; key < keys.size() && key < merged.size(); ++key) {
                    if (channel.target_path == "translation") merged[key].position = keys[key].position;
                    else if (channel.target_path == "scale") merged[key].scale = keys[key].scale;
                    else if (channel.target_path == "rotation") merged[key].rotation = keys[key].rotation;
                }
                if (keys.size() > merged.size()) {
                    for (size_t key = merged.size(); key < keys.size(); ++key) merged.push_back(keys[key]);
                }
            }
            (void)rest;
            clip.channels.set(static_cast<u32>(bone), merged);
        }
        if (clip.channels.size() == 0) continue;
        clip.duration = duration;
        imported.clipNames.push_back(animation.name.empty() ? "Clip" : animation.name);
        imported.clips.push_back(std::move(clip));
    }

    imported.mesh = mesh;
    Animation::registerImportedSkin(path, std::move(imported));
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
    importGltfSkin(model, filenameStr, mesh);
    
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
