#include "GameLoop.hpp"
#include <algorithm>

namespace Caffeine {

GameLoop::GameLoop(const GameLoopConfig& config)
    : m_config(config) {
}

GameLoop::~GameLoop() {
}

void GameLoop::init() {
    if (m_state != GameState::Init) return;
    m_state = GameState::Running;
    m_accumulator = 0.0;
    m_elapsedTime = 0.0;
    m_alpha = 0.0;
    m_frameCount = 0;
}

void GameLoop::tick(f64 deltaTime) {
    if (m_state != GameState::Running && m_state != GameState::Paused) return;

    if (m_callbacks) m_callbacks->onBeginFrame();
    if (onBeginFrame) onBeginFrame();

    deltaTime = std::min(deltaTime, m_config.maxFrameTime);
    m_elapsedTime += deltaTime;

    if (m_state == GameState::Running) {
        m_accumulator += deltaTime;

        while (m_accumulator >= m_config.fixedDeltaTime) {
            processFixedUpdate(m_config.fixedDeltaTime);
            m_accumulator -= m_config.fixedDeltaTime;
        }

        if (m_config.interpolation) {
            m_alpha = m_accumulator / m_config.fixedDeltaTime;
        } else {
            m_alpha = 0.0;
        }
    }

    if (m_callbacks) m_callbacks->onRender(m_alpha);
    if (onRender) onRender(m_alpha);

    if (m_callbacks) m_callbacks->onEndFrame();
    if (onEndFrame) onEndFrame();

    m_frameCount++;
}

void GameLoop::pause() {
    if (m_state == GameState::Running) {
        m_state = GameState::Paused;
    }
}

void GameLoop::resume() {
    if (m_state == GameState::Paused) {
        m_state = GameState::Running;
    }
}

void GameLoop::shutdown() {
    if (m_state == GameState::Running || m_state == GameState::Paused) {
        m_state = GameState::Shutdown;
    }
}

void GameLoop::setCallbacks(IGameCallbacks* callbacks) {
    m_callbacks = callbacks;
}

void GameLoop::processFixedUpdate(f64 dt) {
    if (m_callbacks) m_callbacks->onFixedUpdate(dt);
    if (onFixedUpdate) onFixedUpdate(dt);
}

}
