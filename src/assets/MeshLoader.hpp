#pragma once
#include "assets/MeshTypes.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/Components3D.hpp"
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#endif

namespace Caffeine::Assets {
using namespace Caffeine;

class MeshLoader {
public:
    MeshLoader() = default;
    
#ifdef CF_HAS_SDL3
    explicit MeshLoader(RHI::RenderDevice* device) : m_device(device) {}
#endif

    static Mesh3D* fromMemory(const Vertex3D* verts, u32 vertCount, const u32* indices, u32 indexCount) {
        Mesh3D* mesh = new Mesh3D();
        mesh->vertices.resize(vertCount);
        mesh->indices.resize(indexCount);
        
        for (u32 i = 0; i < vertCount; ++i) {
            mesh->vertices[i] = verts[i];
        }
        
        for (u32 i = 0; i < indexCount; ++i) {
            mesh->indices[i] = indices[i];
        }
        
        SubMesh submesh;
        submesh.indexOffset = 0;
        submesh.indexCount = indexCount;
        submesh.materialIndex = 0;
        mesh->subMeshes.push_back(submesh);
        
        computeBounds(*mesh);
        
        return mesh;
    }

    static Mesh3D* parseOBJ(const char* src, usize srcLen) {
        if (srcLen == 0) return nullptr;
        
        std::vector<Vec3> positions;
        std::vector<Vec3> normals;
        std::vector<Vec2> texcoords;
        std::vector<Vertex3D> vertices;
        std::vector<u32> indices;
        
        const char* line = src;
        const char* end = src + srcLen;
        
        while (line < end) {
            while (line < end && (*line == ' ' || *line == '\t')) ++line;
            
            if (line >= end || *line == '\n' || *line == '\r' || *line == '#') {
                while (line < end && *line != '\n') ++line;
                if (line < end) ++line;
                continue;
            }
            
            if (line[0] == 'v' && line[1] == 'n' && (line[2] == ' ' || line[2] == '\t')) {
                Vec3 n;
                sscanf(line + 2, "%f %f %f", &n.x, &n.y, &n.z);
                normals.push_back(n);
            }
            else if (line[0] == 'v' && line[1] == 't' && (line[2] == ' ' || line[2] == '\t')) {
                Vec2 t;
                sscanf(line + 2, "%f %f", &t.x, &t.y);
                texcoords.push_back(t);
            }
            else if (line[0] == 'v' && (line[1] == ' ' || line[1] == '\t')) {
                Vec3 p;
                sscanf(line + 1, "%f %f %f", &p.x, &p.y, &p.z);
                positions.push_back(p);
            }
            else if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t')) {
                std::vector<Vertex3D> faceVerts;
                const char* fp = line + 1;
                
                while (fp < end && *fp != '\n' && *fp != '\r') {
                    while (*fp == ' ' || *fp == '\t') ++fp;
                    if (*fp == '\n' || *fp == '\r' || fp >= end) break;
                    
                    int vi = 0, vti = 0, vni = 0;
                    
                    if (sscanf(fp, "%d/%d/%d", &vi, &vti, &vni) == 3) {
                    }
                    else if (sscanf(fp, "%d//%d", &vi, &vni) == 2) {
                        vti = 0;
                    }
                    else if (sscanf(fp, "%d/%d", &vi, &vti) == 2) {
                        vni = 0;
                    }
                    else {
                        sscanf(fp, "%d", &vi);
                        vti = 0;
                        vni = 0;
                    }
                    
                    if (vi < 0) vi = (int)positions.size() + vi + 1;
                    if (vti < 0) vti = (int)texcoords.size() + vti + 1;
                    if (vni < 0) vni = (int)normals.size() + vni + 1;
                    
                    Vertex3D vert = {};
                    if (vi > 0 && vi <= (int)positions.size()) {
                        vert.position = positions[vi - 1];
                    }
                    if (vti > 0 && vti <= (int)texcoords.size()) {
                        vert.texcoord = texcoords[vti - 1];
                    }
                    if (vni > 0 && vni <= (int)normals.size()) {
                        vert.normal = normals[vni - 1];
                    }
                    
                    faceVerts.push_back(vert);
                    
                    while (fp < end && *fp != ' ' && *fp != '\t' && *fp != '\n' && *fp != '\r') ++fp;
                }
                
                for (usize i = 1; i + 1 < faceVerts.size(); ++i) {
                    vertices.push_back(faceVerts[0]);
                    vertices.push_back(faceVerts[i]);
                    vertices.push_back(faceVerts[i + 1]);
                }
            }
            
            while (line < end && *line != '\n') ++line;
            if (line < end) ++line;
        }
        
        if (vertices.empty()) return nullptr;
        
        Mesh3D* mesh = new Mesh3D();
        mesh->vertices = vertices;
        mesh->indices.resize(vertices.size());
        for (usize i = 0; i < vertices.size(); ++i) {
            mesh->indices[i] = (u32)i;
        }
        
        SubMesh submesh;
        submesh.indexOffset = 0;
        submesh.indexCount = (u32)mesh->indices.size();
        submesh.materialIndex = 0;
        mesh->subMeshes.push_back(submesh);
        
        computeMeshTangents(*mesh);
        computeBounds(*mesh);
        
        return mesh;
    }

    Mesh3D* loadOBJ(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return nullptr;
        
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        if (size <= 0) {
            fclose(f);
            return nullptr;
        }
        
        std::vector<char> buffer(size + 1);
        fread(buffer.data(), 1, size, f);
        fclose(f);
        buffer[size] = '\0';
        
        return parseOBJ(buffer.data(), size);
    }
    
    static Mesh3D* parseGLTF(const u8* data, usize dataLen, const char* filename,
                             std::string* outError = nullptr);
    
    static bool loadTextureFromFile(Mesh3D* mesh, const char* imagePath);
    static void loadPNGTexture(Mesh3D* mesh, const char* pngPath);


#ifdef CF_HAS_SDL3
    void uploadToGPU(Mesh3D* mesh) {
        if (!m_device || !mesh) return;

        if (!mesh->vertices.empty()) {
            RHI::BufferDesc desc;
            desc.size = mesh->vertices.size() * sizeof(Vertex3D);
            desc.name = "MeshVertexBuffer";
            if (!mesh->vertexBuffer) {
                mesh->vertexBuffer = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);
            }
            if (mesh->vertexBuffer) {
                m_device->uploadBuffer(mesh->vertexBuffer, mesh->vertices.data(), desc.size);
            }
        }

        if (!mesh->indices.empty()) {
            RHI::BufferDesc desc;
            desc.size = mesh->indices.size() * sizeof(u32);
            desc.name = "MeshIndexBuffer";
            if (!mesh->indexBuffer) {
                mesh->indexBuffer = m_device->createBuffer(desc, RHI::BufferUsage::Index);
            }
            if (mesh->indexBuffer) {
                m_device->uploadBuffer(mesh->indexBuffer, mesh->indices.data(), desc.size);
            }
        }
    }
#endif

private:
    static void computeMeshTangents(Mesh3D& mesh) {
        if (mesh.vertices.empty() || mesh.indices.size() < 3) return;

        std::vector<Vec3> tan1(mesh.vertices.size(), Vec3(0.0f, 0.0f, 0.0f));
        std::vector<Vec3> tan2(mesh.vertices.size(), Vec3(0.0f, 0.0f, 0.0f));

        for (usize i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const Vertex3D& v0 = mesh.vertices[mesh.indices[i]];
            const Vertex3D& v1 = mesh.vertices[mesh.indices[i + 1]];
            const Vertex3D& v2 = mesh.vertices[mesh.indices[i + 2]];

            const Vec3 e1 = v1.position - v0.position;
            const Vec3 e2 = v2.position - v0.position;
            const Vec2 duv1 = v1.texcoord - v0.texcoord;
            const Vec2 duv2 = v2.texcoord - v0.texcoord;

            const f32 denom = duv1.x * duv2.y - duv2.x * duv1.y;
            if (std::abs(denom) < 1e-8f) continue;
            const f32 r = 1.0f / denom;
            const Vec3 tangent = (e1 * duv2.y - e2 * duv1.y) * r;
            const Vec3 bitangent = (e2 * duv1.x - e1 * duv2.x) * r;

            tan1[mesh.indices[i]] += tangent;
            tan1[mesh.indices[i + 1]] += tangent;
            tan1[mesh.indices[i + 2]] += tangent;
            tan2[mesh.indices[i]] += bitangent;
            tan2[mesh.indices[i + 1]] += bitangent;
            tan2[mesh.indices[i + 2]] += bitangent;
        }

        for (usize i = 0; i < mesh.vertices.size(); ++i) {
            const Vec3& n = mesh.vertices[i].normal;
            Vec3 t = tan1[i];
            if (t.lengthSquared() < 1e-8f) {
                mesh.vertices[i].tangent = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
                continue;
            }
            t = (t - n * n.dot(t)).normalized();
            const f32 w = (n.cross(t).dot(tan2[i]) < 0.0f) ? -1.0f : 1.0f;
            mesh.vertices[i].tangent = Vec4(t.x, t.y, t.z, w);
        }
    }

    static void computeBounds(Mesh3D& mesh) {
        if (mesh.vertices.empty()) {
            mesh.bounds.min = Vec3(0.0f, 0.0f, 0.0f);
            mesh.bounds.max = Vec3(0.0f, 0.0f, 0.0f);
            return;
        }
        
        Vec3 minBounds = mesh.vertices[0].position;
        Vec3 maxBounds = mesh.vertices[0].position;
        
        for (const auto& v : mesh.vertices) {
            minBounds.x = std::min(minBounds.x, v.position.x);
            minBounds.y = std::min(minBounds.y, v.position.y);
            minBounds.z = std::min(minBounds.z, v.position.z);
            
            maxBounds.x = std::max(maxBounds.x, v.position.x);
            maxBounds.y = std::max(maxBounds.y, v.position.y);
            maxBounds.z = std::max(maxBounds.z, v.position.z);
        }
        
        mesh.bounds.min = minBounds;
        mesh.bounds.max = maxBounds;
    }

#ifdef CF_HAS_SDL3
    RHI::RenderDevice* m_device = nullptr;
#endif
};

class MeshSystem : public ECS::ISystem {
public:
    void onUpdate(ECS::World& world, f32 dt) override {
        (void)dt;
        ECS::ComponentQuery q;
        q.with<ECS::Position3D>();
        q.with<MeshRenderer>();
        world.forEach<ECS::Position3D, MeshRenderer>(q,
            [](ECS::Entity, ECS::Position3D&, MeshRenderer&) {
            });
    }
};

}  // namespace Caffeine::Assets
