# 📜 Scripting

> **Fase:** 6 — O Olimpo  
> **Namespace:** `Caffeine::Script`  
> **Status:** 📅 Planejado (TBD)  
> **RFs:** RF6.6 (scripting hot-reload), RF4.7 (bindings iniciais)

---

## Visão Geral

O módulo de **Scripting** expõe a Caffeine Engine a uma linguagem de script para que designers e scribes possam programar comportamentos de jogo sem escrever C++. O sistema é **opt-in** — jogos puramente em C++ não têm overhead de scripting.

**Estado:** A linguagem de script é **TBD (To Be Decided)**. As duas candidatas são:

| Critério | Lua 5.4 | AngelScript 2.x |
|----------|---------|-----------------|
| Familiaridade na indústria | ✅ Alta (LÖVE, Roblox, WoW) | 🟡 Média |
| Performance | ✅ Muito boa (LuaJIT disponível) | ✅ Boa (compilado) |
| Tipagem | ❌ Dinâmica (erros em runtime) | ✅ Estática (erros na compilação) |
| Integração C++ | 🟡 Via sol2/toLua | ✅ Nativa, API C++ direta |
| Tamanho do runtime | ✅ ~300KB | 🟡 ~600KB |
| Hot-reload | ✅ reload do chunk | 🟡 recompilação necessária |
| **Recomendação** | **✅ Preferida** | Alternativa |

> **Decisão preliminar:** Lua + [sol2](https://github.com/ThePhD/sol2) para binding automático C++→Lua. A decisão final será tomada na Fase 4 (RF4.7).

---

## API Planejada

```cpp
namespace Caffeine::Script {

// ============================================================================
// @brief  Componente ECS que associa um script Lua a uma entidade.
// ============================================================================
struct ScriptComponent {
    Assets::AssetHandle<ScriptAsset> script;  // handle para o ficheiro .lua

    // Funções padrão do ciclo de vida (chamadas pelo ScriptSystem):
    // - onCreate(entity)    chamado uma vez após criação
    // - onUpdate(entity, dt) chamado todo o frame
    // - onDestroy(entity)  chamado antes de destruição
    // - onCollision(entity, other) chamado ao colidir (requer Physics2D)
};

// ============================================================================
// @brief  Asset que representa um script Lua compilado.
// ============================================================================
struct ScriptAsset {
    std::string   sourcePath;   // caminho do .lua no disco
    std::string   source;       // código-fonte carregado
    // Bytecode Lua compilado (se LuaJIT for usado):
    std::vector<u8> bytecode;
};

// ============================================================================
// @brief  VM Lua principal. Um único estado Lua por aplicação.
//
//  Expõe ao Lua:
//  - Entity: criação, destruição, get/set de componentes
//  - Transform: posição, rotação, escala
//  - Input: isKeyDown, getAxis
//  - Events: on(), emit()
//  - Debug: log(), draw()
//
//  Threading: a VM Lua é single-threaded. Scripts rodam no thread principal.
// ============================================================================
class LuaVM {
public:
    bool init();
    void shutdown();

    // Executa um ficheiro .lua e regista as suas funções globais
    bool loadScript(std::string_view path);

    // Hot-reload: recarrega um ficheiro .lua em disco sem reiniciar a VM
    bool reloadScript(std::string_view path);

    // Chama uma função Lua por nome com argumentos arbitrários
    template<typename... Args>
    bool call(std::string_view function, Args&&... args);

    // Acesso ao estado sol2 (para registar bindings customizados)
    sol::state& state() { return m_lua; }

private:
    sol::state m_lua;

    void registerCaffeineBindings();   // regista toda a API da engine
    void registerEntityBindings();
    void registerTransformBindings();
    void registerInputBindings();
    void registerEventBindings();
    void registerDebugBindings();
    void registerMathBindings();
};

// ============================================================================
// @brief  Sistema ECS que executa scripts em entidades com ScriptComponent.
//
//  Por frame:
//  1. Itera todas as entidades com ScriptComponent
//  2. Para cada entidade, chama script.onUpdate(entity, dt)
//  3. Detecta erros Lua e regista via LogSystem (sem crash)
// ============================================================================
class ScriptSystem : public ECS::ISystem {
public:
    explicit ScriptSystem(LuaVM* vm);

    void onInit(ECS::World& world) override;
    void onUpdate(ECS::World& world, f32 dt) override;
    void onShutdown(ECS::World& world) override;

    // Chama onCreate() em todas as entidades com ScriptComponent recém-criadas
    void initNewScripts(ECS::World& world);

private:
    LuaVM* m_vm;

    void callLifecycle(std::string_view fn, ECS::Entity entity, f32 dt = 0.0f);
    void handleLuaError(std::string_view fn, ECS::Entity entity,
                        const sol::error& err);
};

// ============================================================================
// @brief  Watcher de ficheiros de script para hot-reload.
//
//  Em modo editor (CF_EDITOR_MODE), monitoriza a pasta /scripts/.
//  Quando um .lua é modificado, recarrega via LuaVM::reloadScript().
// ============================================================================
class ScriptWatcher {
public:
    void init(LuaVM* vm, std::string_view scriptsDir);
    void shutdown();

    // Chame no loop principal para verificar mudanças
    void poll();

private:
    LuaVM*                                    m_vm = nullptr;
    std::string                               m_dir;
    HashMap<std::string, std::filesystem::file_time_type> m_mtimes;
};

}  // namespace Caffeine::Script
```

---

## Bindings Lua — API Exposta

### Entity & Components

```lua
-- Criar e destruir entidades:
local entity = caffeine.world.create()
caffeine.world.destroy(entity)

-- Adicionar/remover componentes:
caffeine.world.addTransform(entity, { x=100, y=200, rotation=0, scaleX=1, scaleY=1 })
caffeine.world.addSprite(entity, { texture="hero.caf", r=1, g=1, b=1, a=1 })

-- Obter/modificar componentes:
local t = caffeine.world.getTransform(entity)
t.x = t.x + 10
caffeine.world.setTransform(entity, t)
```

### Input

```lua
-- Teclado:
if caffeine.input.isKeyDown("Space") then
    jump()
end

-- Eixos analógicos (gamepad ou teclado mapeado):
local horizontal = caffeine.input.getAxis("Horizontal")   -- -1 a 1
local vertical   = caffeine.input.getAxis("Vertical")

-- Mouse:
local mx, my = caffeine.input.mousePosition()
if caffeine.input.isMouseButtonDown(1) then  -- botão esquerdo
    shoot(mx, my)
end
```

### Eventos

```lua
-- Subscrever evento:
local handle = caffeine.events.on("OnCollision2D", function(entityA, entityB)
    if entityA == hero then
        takeDamage(10)
    end
end)

-- Emitir evento customizado:
caffeine.events.emit("OnPlayerDied", { score = 1500 })

-- Cancelar subscrição:
caffeine.events.off(handle)
```

### Debug

```lua
-- Log:
caffeine.debug.log("Hero position: " .. t.x .. ", " .. t.y)
caffeine.debug.warn("Low HP!")
caffeine.debug.error("Critical failure!")

-- Debug draw:
caffeine.debug.drawCircle(t.x, t.y, 32, 1, 0, 0, 1)  -- x, y, radius, r, g, b, a
caffeine.debug.drawRect(0, 0, 100, 100, 0, 1, 0, 1)
```

### Math

```lua
local v1 = caffeine.math.vec2(10, 20)
local v2 = caffeine.math.vec2(5, 15)
local sum = caffeine.math.add(v1, v2)      -- { x=15, y=35 }
local len = caffeine.math.length(v1)       -- 22.36...
local dir = caffeine.math.normalize(v1)
local dot = caffeine.math.dot(v1, v2)
```

---

## Exemplo Completo: Hero Script

```lua
-- scripts/hero.lua
-- Script de comportamento do herói

local SPEED  = 200.0
local JUMP_V = -400.0

local isGrounded = false

function onCreate(entity)
    caffeine.debug.log("Hero created: " .. entity)
end

function onUpdate(entity, dt)
    local t = caffeine.world.getTransform(entity)
    local rb = caffeine.world.getRigidBody2D(entity)

    -- Movimento horizontal
    local h = caffeine.input.getAxis("Horizontal")
    rb.velocityX = h * SPEED

    -- Pulo
    if caffeine.input.isKeyDown("Space") and isGrounded then
        rb.velocityY = JUMP_V
        isGrounded = false
    end

    -- Flip do sprite
    local sprite = caffeine.world.getSprite(entity)
    if h < 0 then sprite.flipX = true
    elseif h > 0 then sprite.flipX = false
    end
    caffeine.world.setSprite(entity, sprite)
    caffeine.world.setRigidBody2D(entity, rb)
end

function onCollision(entity, other)
    -- Assume que o chão tem o componente "Ground"
    if caffeine.world.hasComponent(other, "Ground") then
        isGrounded = true
    end
end

function onDestroy(entity)
    caffeine.debug.log("Hero destroyed")
end
```

---

## Hot-Reload de Scripts

```
[Editor Mode ativo: CF_EDITOR_MODE=1]
         │
         ▼
ScriptWatcher::poll()  (chamado todo frame)
         │
   Detecta mudança em scripts/hero.lua
         │
         ▼
LuaVM::reloadScript("scripts/hero.lua")
         │
   Recarrega o chunk Lua
   (globals são substituídos na VM)
         │
         ▼
Da próxima invocação de onUpdate():
   usa o código novo — sem restart
```

---

## Segurança e Sandboxing

Por defeito, scripts Lua têm acesso **restrito**:

| Permitido | Bloqueado |
|-----------|-----------|
| Todas as APIs `caffeine.*` | `os.*` (sistema de ficheiros/processos) |
| `math.*`, `string.*`, `table.*` | `io.*` (I/O directo) |
| `require` (apenas módulos do jogo) | `load`, `loadstring` (execução dinâmica) |
| `pcall`, `xpcall` (error handling) | `debug.*` (acesso a internals da VM) |

Em modo editor (`CF_EDITOR_MODE`), as restrições de `io.*` podem ser relaxadas para permitir salvar dados de save state.

---

## Critério de Aceitação

- [ ] LuaVM inicializa e executa um script `.lua` simples
- [ ] ScriptComponent pode ser adicionado a uma entidade via código C++ ou editor
- [ ] `onUpdate(entity, dt)` é chamado todo frame para todas as entidades com ScriptComponent
- [ ] Bindings de Transform (get/set posição, rotação, escala) funcionam correctamente
- [ ] Bindings de Input (isKeyDown, getAxis) funcionam correctamente
- [ ] Erros Lua são capturados e logados sem crash do jogo
- [ ] Hot-reload: modificar um `.lua` em disco recarrega o script sem reiniciar a aplicação
- [ ] Sandbox: `os.execute()` retorna erro em scripts de jogo

---

## Dependências

- **Upstream:**
  - [ECS](../fase4/ecs.md) — `World`, `Entity`, `ISystem`, `ComponentID`
  - [Input](../fase2/input.md) — `InputManager`
  - [Events](../fase4/events.md) — `EventBus`
  - [Debug Tools](../fase2/debug.md) — `LogSystem`, `DebugDraw`
  - [Asset Manager](../fase3/asset-manager.md) — carregamento de ficheiros `.lua`
  - [Scene Editor](scene-editor.md) — atribuição de scripts via InspectorPanel
- **Downstream:**
  - — (módulo de utilizador final)

---

## Referências

- [`docs/fase4/ecs.md`](../fase4/ecs.md) — ECS World e sistemas
- [`docs/fase4/events.md`](../fase4/events.md) — EventBus
- [`docs/fase2/input.md`](../fase2/input.md) — InputManager
- [`docs/MASTER.md`](../MASTER.md) — Documentação unificada
- [Lua 5.4 Reference Manual](https://www.lua.org/manual/5.4/)
- [sol2 — C++ / Lua binding library](https://github.com/ThePhD/sol2)
- [LuaJIT](https://luajit.org/) — JIT compiler para Lua (performance)
- [AngelScript](https://www.angelcode.com/angelscript/) — alternativa estaticamente tipada
