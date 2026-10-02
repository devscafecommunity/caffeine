# Forward render features

> **Componente:** `ForwardRenderFeaturesComponent`  
> **Resolver:** `Render::resolveForwardRenderFeatures`  
> **Lua:** `caffeine.forwardrender.*`  
> **GPU:** `GpuSceneRenderer` (`resolveFeaturesFromScene` por defeito)

Instancing, IBL, oclusão CPU, reflexos e volumétricos **não** vivem nas preferências do editor. São capacidades da cena — como o stack de pós-processamento — que o renderer resolve a cada frame.

## Componente ECS

Adiciona **Rendering → Forward Render Features** a uma entidade de ambiente (por exemplo `RenderEnvironment` na cena `dense_outdoor.caf`).

Sub-blocos:

| Bloco | Função |
|-------|--------|
| Instancing | `drawIndexedInstanced`, até 256 por batch (ligado por defeito) |
| IBL | Difuso a partir do céu (**desligado por defeito** — a skybox não ilumina). Especulares (metais) continuam a refletir o céu. |
| Occlusion | Oclusão coarse na CPU (off por defeito) |
| Reflections | Planar, screen-space, probe |
| Volumetrics | Nevoeiro analítico no forward shader |

Sem componente na cena, `budgetSafeForwardDefaults()` devolve `RenderFeatureSettings` com:

| Capacidade | Default |
|------------|---------|
| Instancing | ligado (256/batch) |
| IBL difuso | desligado (skybox = fundo + reflexões; ligar para o céu iluminar) |
| Occlusion | desligado |
| Reflections | **Probe** + screen-space trace |
| Volumetrics | desligado |

O componente é gravado no `.caf` (tipo 45).

## Lua

```lua
caffeine.forwardrender.ensure(entityId)
caffeine.forwardrender.setReflections(entityId, true, 1) -- planar
caffeine.forwardrender.setVolumetrics(entityId, true, 2) -- medium
caffeine.forwardrender.setOcclusion(entityId, true)
```

## C++

```cpp
auto& fx = world.add<ECS::ForwardRenderFeaturesComponent>(entity);
fx.reflections.enabled = true;
fx.reflections.mode = Render::ReflectionMode::Planar;

Render::RenderFeatureSettings settings = Render::resolveForwardRenderFeatures(world);
```

Para testes ou ferramentas que injetem parâmetros manualmente, `GpuSceneRenderOptions::resolveFeaturesFromScene = false` e preenche `features` à mão.

## Ver também

- [`reflections.md`](reflections.md)
- [`volumetric-lighting.md`](volumetric-lighting.md)
- [`post-processing.md`](post-processing.md)
