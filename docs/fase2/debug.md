# 🔍 Debug Tools

> **Fase:** 2+ (disponível desde a Fase 2, cresce com a engine)  
> **Namespace:** `Caffeine::Debug`  
> **Arquivos:** `src/debug/LogSystem.hpp`, `Profiler.hpp`, `DebugDraw.hpp`  
> **Status:** 📅 Planejado  
> **RF:** RF2.10

---

## Visão Geral

Debug Tools fornece ferramentas de desenvolvimento que ficam sempre disponíveis (mesmo em builds de release configuráveis). Composto por três módulos independentes:

| Módulo | Propósito |
|--------|-----------|
| **LogSystem** | Logging categorizado por nível e categoria |
| **Profiler** | Medição de performance por escopo |
| **DebugDraw** | Visualização geométrica no mundo (AABB, vetores, etc.) |

---

## API Planejada

### LogSystem

```cpp
namespace Caffeine::Debug {

// ============================================================================
// @brief  Nível de severidade do log.
// ============================================================================
enum class LogLevel : u8 {
    Trace = 0,   // Informação muito detalhada (desabilitado em release)
    Info,        // Informação normal de operação
    Warn,        // Situação anormal mas recuperável
    Error,       // Erro que afeta funcionalidade
    Fatal        // Erro irrecuperável — terminação do processo
};

// ============================================================================
// @brief  Sistema de logging categorizado.
//
//  Cada categoria pode ser habilitada/desabilitada independentemente.
//  Útil para silenciar subsistemas muito verbosos.
//
//  Exemplos de categorias: "Physics", "AssetManager", "ECS", "Audio"
// ============================================================================
class LogSystem {
public:
    static LogSystem& instance();

    void log(LogLevel level, const char* category,
             const char* fmt, ...);

    void setLevel(LogLevel minLevel);
    void setCategoryEnabled(const char* category, bool enabled);
    void addSink(std::function<void(LogLevel, const char*, const char*)> sink);

    // Macros convenientes (ver abaixo)
};

}  // namespace Caffeine::Debug

// Macros para uso no código:
#define CF_TRACE(cat, ...)  Caffeine::Debug::LogSystem::instance().log(Caffeine::Debug::LogLevel::Trace, cat, __VA_ARGS__)
#define CF_INFO(cat, ...)   Caffeine::Debug::LogSystem::instance().log(Caffeine::Debug::LogLevel::Info,  cat, __VA_ARGS__)
#define CF_WARN(cat, ...)   Caffeine::Debug::LogSystem::instance().log(Caffeine::Debug::LogLevel::Warn,  cat, __VA_ARGS__)
#define CF_ERROR(cat, ...)  Caffeine::Debug::LogSystem::instance().log(Caffeine::Debug::LogLevel::Error, cat, __VA_ARGS__)
#define CF_FATAL(cat, ...)  Caffeine::Debug::LogSystem::instance().log(Caffeine::Debug::LogLevel::Fatal, cat, __VA_ARGS__)
```

### Profiler

```cpp
namespace Caffeine::Debug {

// ============================================================================
// @brief  Profiler com markers de escopo.
//
//  Mede duração de escopos nomeados e acumula estatísticas entre frames.
//  Compatível com ScopeTimer RAII (ver timer.md).
// ============================================================================
class Profiler {
public:
    static Profiler& instance();

    void beginScope(const char* name);
    void endScope(const char* name);

    struct ScopeStats {
        const char* name;
        u64          callCount;
        f64          totalMs;
        f64          avgMs;
        f64          minMs;
        f64          maxMs;
    };

    void report(Caffeine::Array<ScopeStats, 256>& out) const;
    void reset();  // Zera estatísticas acumuladas

    // Habilita/desabilita coleta (zero overhead quando desabilitado)
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

private:
    bool m_enabled = true;
    HashMap<u64, ScopeStats> m_stats;  // keyed por hash do nome
};

// Macro RAII para escopo automático:
#define CF_PROFILE_SCOPE(name) \
    Caffeine::Debug::ProfileScope _scope_##__LINE__(name)

// ============================================================================
// @brief  RAII helper para marcar escopos automaticamente.
// ============================================================================
class ProfileScope {
public:
    explicit ProfileScope(const char* name);
    ~ProfileScope();
private:
    const char* m_name;
    u64          m_startTicks;
};

}  // namespace Caffeine::Debug
```

### DebugDraw

```cpp
namespace Caffeine::Debug {

// ============================================================================
// @brief  Visualização geométrica sobreposta ao mundo.
//
//  Primitivos acumulados durante o frame e renderizados em endFrame().
//  Requer RHI::CommandBuffer (disponível na Fase 3).
//
//  Útil para: hitboxes de colisão, vetores de velocidade, raycasts, bounds.
// ============================================================================
class DebugDraw {
public:
    static DebugDraw& instance();

    // Primitivos 2D
    void line(Vec2 a, Vec2 b, Color color, f32 duration = 0.0f);
    void rect(Rect2D rect, Color color, f32 duration = 0.0f);
    void circle(Vec2 center, f32 radius, Color color, f32 duration = 0.0f);
    void text(Vec2 pos, const char* text, Color color);
    void arrow(Vec2 from, Vec2 to, Color color);
    void cross(Vec2 center, f32 size, Color color);

    // Controle de frame
    void beginFrame();  // remove primitivos com duration expirada
    void endFrame(RHI::CommandBuffer* cmd, const Render::Camera2D& camera);

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

private:
    struct DebugPrimitive {
        enum class Type { Line, Rect, Circle, Text, Arrow } type;
        Vec2  a, b;
        f32   radius;
        Color color;
        f32   timeLeft;  // -1 = permanente até próximo beginFrame
        char  text[64];
    };

    bool m_enabled = true;
    std::vector<DebugPrimitive> m_primitives;
};

}  // namespace Caffeine::Debug
```

---

## Exemplos de Uso

```cpp
// ── Logging ───────────────────────────────────────────────────
CF_INFO("AssetManager", "Loaded texture: %s (%.1f KB)", path, sizeKB);
CF_WARN("Physics", "Detected %d overlapping bodies — potential tunneling", count);
CF_ERROR("JobSystem", "Worker thread %d crashed: %s", workerId, err);

// Silenciar subsistema verboso:
Caffeine::Debug::LogSystem::instance().setCategoryEnabled("Physics", false);

// ── Profiler ──────────────────────────────────────────────────
{
    CF_PROFILE_SCOPE("ECS::PhysicsSystem");
    physicsSystem.update(world, dt);
}

// Ou manual:
Caffeine::Debug::Profiler::instance().beginScope("BatchRenderer::flush");
batchRenderer.flushCurrentBatch(cmd);
Caffeine::Debug::Profiler::instance().endScope("BatchRenderer::flush");

// Relatório no endFrame:
Array<Caffeine::Debug::Profiler::ScopeStats, 256> stats;
Caffeine::Debug::Profiler::instance().report(stats);
for (auto& s : stats)
    CF_INFO("Perf", "%-30s  avg=%.2fms  calls=%llu", s.name, s.avgMs, s.callCount);

// ── DebugDraw ─────────────────────────────────────────────────
// Em PhysicsSystem (visualiza hitboxes):
world.query(colliderQuery, [](Collider2D& col, Position2D& pos) {
    Rect2D box = { {pos.x + col.offset.x, pos.y + col.offset.y}, col.size };
    Caffeine::Debug::DebugDraw::instance().rect(box, Color::GREEN);
});

// Vetor de velocidade:
Caffeine::Debug::DebugDraw::instance().arrow(pos, pos + vel * 0.1f, Color::YELLOW);
```

---

## Integração com Dear ImGui (Fase 6)

Na Fase 6, o Profiler e Log serão expostos via [Caffeine Studio](../fase6/embedded-ui.md):
- Janela de **Log** com filtro por categoria/nível
- Janela de **Profiler** com frame timeline gráfica
- Overlay de **Debug Draw** sempre visível em modo editor

---

## Configuração por Build

| Feature | Debug Build | Release Build |
|---------|-------------|---------------|
| `LogLevel::Trace` | ✅ Ativo | ❌ Compilado fora |
| `LogLevel::Info/Warn/Error` | ✅ Ativo | ✅ Ativo (configurável) |
| `Profiler` | ✅ Ativo | Opt-in via `CF_PROFILING=1` |
| `DebugDraw` | ✅ Ativo | ❌ Compilado fora |

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| Categorias de log | Silenciar subsistemas verbosos sem perder outros |
| `duration` em DebugDraw | Primitivos persistentes (ex: "mostre AABB por 2 frames") |
| Profiler opt-out | Zero overhead em builds de release |
| Singletons para Debug | Debug é infraestrutura transversal — acesso global é aceitável aqui |

---

## Critério de Aceitação

- [ ] `log()` overhead < 1μs em builds com logging habilitado
- [ ] Profiler: overhead < 100ns por `beginScope`/`endScope`
- [ ] DebugDraw: overhead < 0.1ms em 1000 primitivos/frame
- [ ] Categorias habilitadas/desabilitadas sem recompilação
- [ ] Log thread-safe (múltiplos workers logando simultaneamente)

---

## Dependências

- **Upstream:** [Timer](timer.md) (ScopeTimer usa TimePoint), `Caffeine::Core::Types`
- **Downstream:** [Fase 3 — RHI](../fase3/rhi.md) (DebugDraw precisa de CommandBuffer), [Fase 6 — Studio](../fase6/embedded-ui.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §16 Debug/Development Tools
- [`docs/fase2/timer.md`](timer.md) — `ScopeTimer` RAII
- [`docs/fase6/embedded-ui.md`](../fase6/embedded-ui.md) — ImGui UI para debug
