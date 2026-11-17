#include "catch.hpp"

#include "../src/caffeine/ecs/World.hpp"
#include "../src/caffeine/scene/ComponentAccess.hpp"
#include "../src/caffeine/scene/ComponentReflection.hpp"

#include <cstring>
#include <set>
#include <string>

using namespace Caffeine;
namespace R = Caffeine::Scene::Reflection;
namespace A = Caffeine::Scene::Access;

TEST_CASE("The reflection registry is consistent", "[reflection]") {
    const auto& registry = R::registry();
    REQUIRE(registry.size() >= 40);
    std::set<std::uint32_t> ids;
    std::set<std::string> names;
    for (const R::ComponentDesc& desc : registry) {
        INFO(desc.name);
        CHECK(ids.insert(desc.id).second);   // ids are unique and stable
        std::string key;
        for (char c : std::string(desc.name)) {
            if (c != ' ') key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        CHECK(names.insert(key).second);     // names are unique once case and spaces are ignored
        std::set<std::string> fields;
        for (const R::FieldDesc& field : desc.fields) {
            CHECK(fields.insert(field.name).second);
            if (field.kind == R::FieldKind::Enum) CHECK(!field.options.empty());
        }
        CHECK(R::findById(desc.id) == &desc);
        CHECK(R::findByName(desc.name) == &desc);
    }
}

TEST_CASE("Every reflected component can be added, read, written and removed", "[reflection]") {
    ECS::World world;
    for (const R::ComponentDesc& desc : R::registry()) {
        INFO(desc.name);
        ECS::Entity entity = world.create("Subject");
        REQUIRE_FALSE(desc.has(world, entity));
        desc.add(world, entity);
        REQUIRE(desc.has(world, entity));
        REQUIRE(R::presentComponents(world, entity).size() >= 1);
        for (const R::FieldDesc& field : desc.fields) {
            INFO(field.name);
            R::FieldValue before;
            REQUIRE(field.get(world, entity, before));
            REQUIRE(before.kind == field.kind);
            // Writing back what was read must not change anything.
            REQUIRE(field.set(world, entity, before));
            R::FieldValue after;
            REQUIRE(field.get(world, entity, after));
            CHECK(std::memcmp(before.vec, after.vec, sizeof(before.vec)) == 0);
            CHECK(before.integer == after.integer);
            CHECK(before.boolean == after.boolean);
            CHECK(std::strcmp(before.text, after.text) == 0);
            // The number/text access layer agrees with the typed one.
            std::string error;
            if (field.kind == R::FieldKind::String) {
                std::string text;
                CHECK(A::getText(world, entity, desc.name, field.name, text, &error));
                CHECK(text == before.text);
            } else if (field.kind != R::FieldKind::Vec2 && field.kind != R::FieldKind::Vec3 &&
                       field.kind != R::FieldKind::Vec4 && field.kind != R::FieldKind::Color) {
                double number = 0.0;
                CHECK(A::getNumber(world, entity, desc.name, field.name, 0, number, &error));
            }
        }
        desc.remove(world, entity);
        CHECK_FALSE(desc.has(world, entity));
        world.destroy(entity);
    }
}

TEST_CASE("Newly reflected components expose their real fields", "[reflection]") {
    ECS::World world;
    ECS::Entity entity = world.create("Subject");
    std::string error;
    REQUIRE(A::add(world, entity, "Terrain", &error));
    REQUIRE(A::setNumber(world, entity, "terrain", "Max height", 0, 321.0, &error));
    REQUIRE(A::setText(world, entity, "Terrain", "Splat Layer Paths 2", "my/rock.png", &error));
    double max = 0.0;
    REQUIRE(A::getNumber(world, entity, "Terrain", "Max height", 0, max, &error));
    CHECK(max == Approx(321.0));
    std::string path;
    REQUIRE(A::getText(world, entity, "Terrain", "Splat Layer Paths 2", path, &error));
    CHECK(path == "my/rock.png");

    REQUIRE(A::add(world, entity, "Post Process", &error));
    REQUIRE(A::setNumber(world, entity, "Post Process", "Bloom: Intensity", 0, 0.8, &error));
    double bloom = 0.0;
    REQUIRE(A::getNumber(world, entity, "Post Process", "Bloom: Intensity", 0, bloom, &error));
    CHECK(bloom == Approx(0.8));

    REQUIRE(A::add(world, entity, "Audio Emitter", &error));
    REQUIRE(A::setText(world, entity, "Audio Emitter", "Clip path", "sfx/hit.wav", &error));
    std::string clip;
    REQUIRE(A::getText(world, entity, "Audio Emitter", "Clip path", clip, &error));
    CHECK(clip == "sfx/hit.wav");

    REQUIRE(A::add(world, entity, "Natural Fluid", &error));
    REQUIRE(A::setNumber(world, entity, "Natural Fluid", "Wind speed", 0, 20.0, &error));
    REQUIRE(A::add(world, entity, "Nav Agent", &error));
    REQUIRE(A::setNumber(world, entity, "Nav Agent", "Speed", 0, 7.0, &error));
    REQUIRE(A::setNumber(world, entity, "Nav Agent", "Destination", 2, 9.0, &error));
    double z = 0.0;
    REQUIRE(A::getNumber(world, entity, "Nav Agent", "Destination", 2, z, &error));
    CHECK(z == Approx(9.0));
}
