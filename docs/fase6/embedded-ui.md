# 🖥️ Embedded UI — Dear ImGui

> **Fase:** 6 — O Olimpo  
> **Namespace:** `Caffeine::Editor`  
> **Status:** 📅 Planejado  
> **RFs:** RF6.1, RF6.2, RF6.6

---

## Visão Geral

Integração de **Dear ImGui** para a interface do Caffeine Studio IDE. ImGui é uma biblioteca de UI em modo imediato (immediate mode) — cada frame redesenha todos os widgets. Ideal para ferramentas de desenvolvimento.

**Distinção da Fase 4:**
- [UI System](../fase4/ui.md) — UI **retida** para jogos (HUD, menus, HealthBar)
- **Embedded ImGui** — UI **imediata** para ferramentas de desenvolvimento (editor, profiler, console)

---

## API Planejada

```cpp
namespace Caffeine::Editor {

// ============================================================================
// @brief  Integração ImGui com SDL3 + RHI da Caffeine.
//
//  Inicializa ImGui com:
//  - Backend SDL3 (eventos de mouse/teclado)
//  - Backend SDL_GPU (renderização)
//
//  Lifecycle:
//  1. init(window, device)
//  2. Per frame: beginFrame() → ImGui widgets → endFrame(cmd)
//  3. shutdown()
// ============================================================================
class ImGuiIntegration {
public:
    bool init(SDL_Window* window, RHI::RenderDevice* device);
    void shutdown();

    void beginFrame();           // chama ImGui::NewFrame()
    void endFrame(RHI::CommandBuffer* cmd);  // chama ImGui::Render() + draw data

    // Passa evento SDL para ImGui (chame antes de processar input do jogo)
    bool processEvent(const SDL_Event& event);

    // Verifica se ImGui está capturando input (não repassar ao jogo)
    bool wantsKeyboard() const;
    bool wantsMouse()    const;
};

// ============================================================================
// @brief  Janela de profiler — visualiza dados do Profiler da Fase 2.
// ============================================================================
class ProfilerWindow {
public:
    void render(const Debug::Profiler& profiler);

private:
    bool m_open = true;
    bool m_paused = false;
    // histograma de 120 frames
    std::array<f32, 120> m_frameTimes;
    u32 m_frameIdx = 0;
};

// ============================================================================
// @brief  Console window — exibe logs e aceita comandos.
// ============================================================================
class ConsoleWindow {
public:
    void render();
    void addLog(Debug::LogLevel level, const char* category,
                const char* message);

private:
    struct LogEntry {
        Debug::LogLevel level;
        FixedString<32>  category;
        FixedString<256> message;
    };
    std::vector<LogEntry> m_entries;
    char m_inputBuf[256] = {};
    bool m_autoScroll    = true;
    bool m_open          = true;
    Debug::LogLevel m_filterLevel = Debug::LogLevel::Trace;
};

// ============================================================================
// @brief  Stats overlay — frame time, FPS, memory.
// ============================================================================
class StatsOverlay {
public:
    void render(const GameLoop::FrameStats& stats,
                const AssetManager::CacheStats& cache);
};

}  // namespace Caffeine::Editor
```

---

## Janelas do Editor

### ProfilerWindow
```
┌──────────────────────────────────────────┐
│ 🔬 Profiler                  [Pause] [X] │
├──────────────────────────────────────────┤
│ Frame: 16.7ms  |  FPS: 60               │
│ ████████████████████████ (frame graph)  │
│                                          │
│ Scope                    avg    max      │
│ ECS::PhysicsSystem       2.1ms  4.3ms   │
│ BatchRenderer::flush     0.8ms  1.2ms   │
│ AnimationSystem          0.3ms  0.6ms   │
│ AudioSystem::update      0.1ms  0.2ms   │
└──────────────────────────────────────────┘
```

### ConsoleWindow
```
┌──────────────────────────────────────────┐
│ 💬 Console                          [X] │
├──────────────────────────────────────────┤
│ Filter: [All ▾]  [✓ Auto-scroll]        │
├──────────────────────────────────────────┤
│ [INFO] AssetManager  Loaded hero.caf    │
│ [WARN] Physics       Overlap detected   │
│ [INFO] Audio         Playing bgm.caf    │
│ [INFO] ECS           Created 50 entit. │
├──────────────────────────────────────────┤
│ > _                                      │
└──────────────────────────────────────────┘
```

---

## Integração no Game Loop

```cpp
// ── Init ──────────────────────────────────────────────────────
#if CF_EDITOR_MODE
Caffeine::Editor::ImGuiIntegration imgui;
imgui.init(window, &device);

Caffeine::Editor::ProfilerWindow profilerWindow;
Caffeine::Editor::ConsoleWindow consoleWindow;

// Hook no LogSystem para exibir no console:
Debug::LogSystem::instance().addSink([&](LogLevel lvl, const char* cat,
                                         const char* msg) {
    consoleWindow.addLog(lvl, cat, msg);
});
#endif

// ── Por frame ─────────────────────────────────────────────────
#if CF_EDITOR_MODE
imgui.beginFrame();

profilerWindow.render(Debug::Profiler::instance());
consoleWindow.render();
sceneEditor.render(world, camera);   // Fase 6.3

imgui.endFrame(cmd);
#endif
```

---

## Hot-Reload Runtime (RF6.6)

```
FileWatcher (já no AssetManager::pollFileChanges())
    │
    ▼
Arquivo modificado em disco
    │
    ├── textura (.png) → re-encode → upload GPU → todos os handles atualizados
    ├── shader (.hlsl/.glsl) → recompile → novo Pipeline → substituído
    └── script (.lua) → reload VM → funções atualizadas
```

---

## Critério de Aceitação

- [ ] Dear ImGui integrado com SDL3 sem conflitos de input
- [ ] ProfilerWindow mostra dados do Profiler real-time
- [ ] ConsoleWindow filtra por nível e categoria
- [ ] Hot-reload: textura atualizada sem restart do jogo
- [ ] ImGui não interfere com input do jogo quando não em foco

---

## Dependências

- **Upstream:** [Debug Tools](../fase2/debug.md), [RHI](../fase3/rhi.md), [Asset Manager](../fase3/asset-manager.md)
- **Downstream:** [Scene Editor](scene-editor.md)

---

## Referências

- [Dear ImGui](https://github.com/ocornut/imgui) — biblioteca UI
- [Dear ImGui SDL3 example](https://github.com/ocornut/imgui/blob/master/examples/example_sdl3_opengl3/main.cpp)
- [`docs/fase2/debug.md`](../fase2/debug.md) — Profiler + LogSystem
