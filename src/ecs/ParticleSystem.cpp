#include "ecs/ParticleSystem.hpp"
#include "ecs/World.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"

namespace Caffeine::ECS {

static float randomFloat(float min, float max) {
    float r = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    return min + r * (max - min);
}

void ParticleSystem::onUpdate(World& world, f32 dt) {
    ComponentQuery q;
    q.with<ParticleEmitterComponent>();
    q.with<Position2D>();

    world.forEach<ParticleEmitterComponent, Position2D>(q,
        [&dt](Entity e, ParticleEmitterComponent& emitter, Position2D& pos) {
            size_t toEmit = static_cast<size_t>(emitter.emissionRate * dt);
            for (size_t i = 0; i < toEmit && emitter.activeParticles.size() < static_cast<size_t>(emitter.maxParticles); ++i) {
                ParticleEmitterComponent::Particle p;
                p.position = { pos.x, pos.y };
                p.velocity.x = randomFloat(emitter.velocityMin.x, emitter.velocityMax.x);
                p.velocity.y = randomFloat(emitter.velocityMin.y, emitter.velocityMax.y);
                p.life = emitter.lifetime;
                p.maxLife = emitter.lifetime;
                p.color = emitter.startColor;
                p.size = emitter.startSize;
                emitter.activeParticles.push_back(p);
            }

            for (auto& p : emitter.activeParticles) {
                p.life -= dt;
                p.position.x += p.velocity.x * dt;
                p.position.y += p.velocity.y * dt;

                float t = 1.0f - (p.life / p.maxLife);
                if (t > 1.0f) t = 1.0f;
                if (t < 0.0f) t = 0.0f;

                p.size = emitter.startSize + (emitter.endSize - emitter.startSize) * t;

                u8 sr = (emitter.startColor >> 24) & 0xFF;
                u8 sg = (emitter.startColor >> 16) & 0xFF;
                u8 sb = (emitter.startColor >> 8) & 0xFF;
                u8 sa = emitter.startColor & 0xFF;

                u8 er = (emitter.endColor >> 24) & 0xFF;
                u8 eg = (emitter.endColor >> 16) & 0xFF;
                u8 eb = (emitter.endColor >> 8) & 0xFF;
                u8 ea = emitter.endColor & 0xFF;

                u8 r = static_cast<u8>(sr + (er - sr) * t);
                u8 g = static_cast<u8>(sg + (eg - sg) * t);
                u8 b = static_cast<u8>(sb + (eb - sb) * t);
                u8 a = static_cast<u8>(sa + (ea - sa) * t);

                p.color = (r << 24) | (g << 16) | (b << 8) | a;
            }

            std::erase_if(emitter.activeParticles, [](const ParticleEmitterComponent::Particle& p) {
                return p.life <= 0.0f;
            });
        });
}

}