#include "catch.hpp"
#include "caffeine/effects/EffectApi.hpp"
#include "effects/EffectSystem.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/MeshGeometry.hpp"
#include "ecs/World.hpp"

#include <algorithm>
#include <cmath>

using namespace Caffeine;
using namespace Caffeine::Effects;
using namespace Caffeine::ECS;

TEST_CASE("Henyey-Greenstein is isotropic at zero anisotropy", "[effects]") {
    const f32 phase = henyeyGreenstein(0.0f, 0.0f);
    REQUIRE(phase == Approx(1.0f / (4.0f * 3.14159265f)).margin(0.001f));
}

TEST_CASE("Volumetric scatter is inside the light radius", "[effects]") {
    const Vec3 light(0.0f, 2.0f, 0.0f);
    const f32 inside = volumetricScatter(Vec3(0.0f, 2.4f, 0.0f), light, Vec3(0.0f, 1.0f, 0.0f),
                                         3.0f, 0.4f, 0.2f, 2.0f);
    const f32 outside = volumetricScatter(Vec3(10.0f, 2.0f, 0.0f), light, Vec3(0.0f, 0.0f, 1.0f),
                                          3.0f, 0.4f, 0.2f, 2.0f);
    REQUIRE(inside > 0.0f);
    REQUIRE(outside == Approx(0.0f).margin(0.0001f));
}

TEST_CASE("Reflection budgets pick step counts", "[effects]") {
    REQUIRE(reflectionSteps(EffectQuality::Performance) == 8u);
    REQUIRE(reflectionSteps(EffectQuality::Balanced) == 24u);
    REQUIRE(reflectionSteps(EffectQuality::Quality) == 48u);
    REQUIRE(reflectionRoughnessGate(EffectQuality::Performance) <
            reflectionRoughnessGate(EffectQuality::Quality));
}

TEST_CASE("Particles simulate in 3D and stay on the plane in 2D", "[effects]") {
    World world;
    Entity spark = world.create();
    world.add<Transform>(spark);
    EffectComponent effect;
    effect.rate = 40.0f;
    effect.maxParticles = 8;
    effect.lifetime = 2.0f;
    effect.gravity = Vec3(0.0f, -4.0f, 0.0f);
    effect.domain = static_cast<u8>(EffectDomain::ThreeD);
    effect.seed = 7;
    world.add<EffectComponent>(spark, effect);

    tickEffects(world, 0.5f, Vec3(0.0f, 1.0f, 4.0f));
    const std::vector<SimParticle>* spawned = particlesFor(spark.id());
    REQUIRE(spawned != nullptr);
    REQUIRE(spawned->size() > 0u);
    const f32 y0 = spawned->front().position.y;

    tickEffects(world, 0.5f, Vec3(0.0f, 1.0f, 4.0f));
    REQUIRE(particlesFor(spark.id())->front().position.y < y0);

    Entity sheet = world.create();
    world.add<Transform>(sheet).position = Vec3(1.0f, 2.0f, 3.0f);
    EffectComponent flat;
    flat.domain = static_cast<u8>(EffectDomain::TwoD);
    flat.rate = 10.0f;
    flat.maxParticles = 4;
    flat.velocityMin = Vec3(0.0f, 1.0f, 5.0f);
    flat.velocityMax = Vec3(0.0f, 1.0f, 5.0f);
    flat.gravity = Vec3(0.0f, 0.0f, 9.0f);
    world.add<EffectComponent>(sheet, flat);
    tickEffects(world, 0.2f, {});
    const std::vector<SimParticle>* planar = particlesFor(sheet.id());
    REQUIRE(planar != nullptr);
    REQUIRE(!planar->empty());
    REQUIRE(planar->front().position.z == Approx(3.0f).margin(0.001f));
    REQUIRE(planar->front().velocity.z == Approx(0.0f).margin(0.001f));
}

TEST_CASE("Reflective shade lowers roughness and sets a step budget", "[effects]") {
    SurfaceShade shade;
    shade.roughness = 0.8f;
    shade.metallic = 0.0f;
    EffectComponent mirror;
    mirror.kind = static_cast<u8>(EffectKind::ReflectiveSurface);
    mirror.quality = static_cast<u8>(EffectQuality::Quality);
    mirror.reflection = 1.0f;
    mirror.roughness = 0.08f;
    mirror.metallic = 1.0f;
    applyEffectShade(mirror, shade);
    REQUIRE(shade.roughness == Approx(0.08f).margin(0.001f));
    REQUIRE(shade.metallic == Approx(1.0f).margin(0.001f));
    REQUIRE(shade.ssrSteps == 48);
    REQUIRE(shade.reflection == Approx(1.0f).margin(0.001f));
}

TEST_CASE("Fog fills 3D shells and dust stays on the 2D overlay", "[effects]") {
    World world;
    Entity volume = world.create();
    world.add<Transform>(volume);
    REQUIRE(setFog(world, volume, EffectDomain::ThreeD, EffectFogStyle::Fog));
    tickEffects(world, 0.2f, {});
    REQUIRE(world.get<EffectComponent>(volume)->emitCarry > 0.0f);

    Assets::Mesh3D mesh;
    buildEffectMesh(world, Vec3(0.0f, 2.0f, 6.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), mesh);
    const size_t volumeVerts = mesh.vertices.size();
    REQUIRE(volumeVerts >= 4u);

    Entity dust = world.create();
    world.add<Transform>(dust);
    REQUIRE(setFog(world, dust, EffectDomain::TwoD, EffectFogStyle::Dust));
    std::vector<EffectSprite> overlay;
    collectOverlayEffects(world, true, overlay);
    REQUIRE(overlay.size() >= 3u);
    mesh.vertices.clear();
    buildEffectMesh(world, Vec3(0.0f, 2.0f, 6.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), mesh);
    REQUIRE(mesh.vertices.size() == volumeVerts);
}

TEST_CASE("Edited mesh geometry replaces the shared primitive", "[effects]") {
    World world;
    Entity box = world.create();
    MeshFilterComponent filter;
    filter.primitive = MeshPrimitive::Cube;
    world.add<MeshFilterComponent>(box, filter);
    REQUIRE(bakeMeshGeometry(world, box));
    MeshGeometryComponent* geometry = world.get<MeshGeometryComponent>(box);
    REQUIRE(geometry != nullptr);
    REQUIRE(geometry->positions.size() >= 8u);
    const f32 y0 = geometry->positions[0].y;
    geometry->positions[0].y = y0 + 1.5f;
    geometry->revision++;
    Assets::Mesh3D* mesh = editedMesh(world, box);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices[0].position.y == Approx(y0 + 1.5f).margin(0.001f));
    const std::vector<u32> face = faceVertexIndices(*geometry, 0);
    REQUIRE(face.size() >= 3u);
}

TEST_CASE("Volumetric lights are the entity mesh", "[effects]") {
    World world;
    Entity cone = world.create();
    world.add<Position3D>(cone);
    world.add<Rotation3D>(cone);
    world.add<Scale3D>(cone);
    EffectComponent shaft;
    configureVolumetricLight(shaft, VolumetricShape::Cone);
    world.add<EffectComponent>(cone, shaft);

    Assets::Mesh3D mesh;
    buildEffectMesh(world, Vec3(0.0f, 2.0f, 6.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), mesh);
    const MeshFilterComponent* filter = world.get<MeshFilterComponent>(cone);
    REQUIRE(filter != nullptr);
    REQUIRE(filter->primitive == MeshPrimitive::Cone);
    const Scale3D* scale = world.get<Scale3D>(cone);
    REQUIRE(scale != nullptr);
    REQUIRE(scale->scale.y == Approx(8.0f).margin(0.01f));

    f32 minZ = 1.0e9f;
    f32 maxZ = -1.0e9f;
    f32 baseRadius = 0.0f;
    for (const Assets::Vertex3D& vertex : mesh.vertices) {
        minZ = std::min(minZ, vertex.position.z);
        maxZ = std::max(maxZ, vertex.position.z);
        baseRadius = std::max(baseRadius, std::abs(vertex.position.x));
    }
    REQUIRE(maxZ - minZ == Approx(8.0f).margin(0.05f));
    REQUIRE(baseRadius == Approx(0.8f).margin(0.05f));

    world.get<Scale3D>(cone)->scale.y = 3.0f;
    mesh.vertices.clear();
    mesh.indices.clear();
    buildEffectMesh(world, Vec3(0.0f, 2.0f, 6.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), mesh);
    minZ = 1.0e9f;
    maxZ = -1.0e9f;
    for (const Assets::Vertex3D& vertex : mesh.vertices) {
        minZ = std::min(minZ, vertex.position.z);
        maxZ = std::max(maxZ, vertex.position.z);
    }
    REQUIRE(maxZ - minZ == Approx(3.0f).margin(0.05f));
    REQUIRE(filter->primitive == MeshPrimitive::Cone);
}

TEST_CASE("Particle params can be set without wiping a 2D emitter", "[effects]") {
    World world;
    Entity spark = world.create();
    REQUIRE(setParticleEmitter(world, spark, EffectDomain::TwoD));
    REQUIRE(setParticleParams(world, spark, 12.0f, 1.2f, 6, 0.3f, 0.04f, -3.0f));
    const EffectComponent* effect = world.get<EffectComponent>(spark);
    REQUIRE(effect != nullptr);
    REQUIRE(effect->domain == static_cast<u8>(EffectDomain::TwoD));
    REQUIRE(effect->rate == Approx(12.0f));
    REQUIRE(effect->maxParticles == 6);
    REQUIRE(effect->gravity.y == Approx(-3.0f));
}
