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
    
    static Mesh3D* parseGLTF(const u8* data, usize dataLen, const char* filename);
    
    static void loadPNGTexture(Mesh3D* mesh, const char* pngPath);


#ifdef CF_HAS_SDL3
    void uploadToGPU(Mesh3D* mesh) {
        if (!m_device || !mesh) return;
        
        if (!mesh->vertices.empty()) {
            RHI::BufferDesc desc;
            desc.size = mesh->vertices.size() * sizeof(Vertex3D);
            desc.name = "MeshVertexBuffer";
            mesh->vertexBuffer = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);
        }
        
        if (!mesh->indices.empty()) {
            RHI::BufferDesc desc;
            desc.size = mesh->indices.size() * sizeof(u32);
            desc.name = "MeshIndexBuffer";
            mesh->indexBuffer = m_device->createBuffer(desc, RHI::BufferUsage::Index);
        }
    }
#endif

private:
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
