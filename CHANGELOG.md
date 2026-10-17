# Changelog

Todas as alterações notáveis do projeto **Caffeine Engine** / **Doppio IDE** estão documentadas neste ficheiro.

O formato segue [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/) e o histórico completo de **419 commits** (desde 2026-04-06) está no [Apêndice](#apêndice--histórico-completo-de-commits).

Legenda de categorias: **Added** · **Changed** · **Fixed** · **Removed** · **Deprecated** · **Security**

---

## [Unreleased]

_Nada por agora._

---

## [2026-09-23] — Performance do editor, profiler hierárquico & skybox

### Added
- **Profiler hierárquico** — árvore de scopes com `self ms` (exclusivo), `total ms` (inclusivo), `% frame`, `% parent` e caminho completo no tooltip.
- Documentação [`docs/performance/editor-viewport-performance.md`](docs/performance/editor-viewport-performance.md) e índice em [`docs/performance/README.md`](docs/performance/README.md).
- `CF_PROFILE_SCOPE` granular em `SceneEditor`, `SceneViewport`, `GpuSceneRenderer` (`renderWithCamera`, `gather`, `shadows`, `draw`).
- Cache de GPU nos previews (Gameplay / Camera) — re-render só quando câmera ou tamanho mudam.
- Skybox **motion-adaptive**: raster a 384 px enquanto a câmera move; resolução completa após parar.

### Changed
- **Editor viewport ~10 → ~100 FPS** — separação GPU/CPU por frame, shadow UBO uma vez por pass, cache de texturas de terreno, LOD ×2, cap 1280 px, sombras off no editor.
- `GpuSceneRenderer`: samplers de sombra **sempre** ligados (evita SIGSEGV NVIDIA); passes de sombra omitidos no editor.
- `SceneViewport`: skip de `gpuPass` quando câmera/canvas/modo inalterados.
- Previews: throttle GPU (intervalo 2 quando parado), caps de skybox reduzidos (256–640 px).
- Profiler UI: refresh da árvore a cada 15 frames; `Expand All` off por defeito.

### Fixed
- Skybox a piscar e atrasar o terreno nos previews (sky + GPU dessincronizados).
- Erros ImGui `Missing TreePop()` / `PushID/PopID` na árvore do Profiler.
- Stutters ao mover câmera após remoção do throttle do skybox (raster adaptativo + menos GPU redundante).
- Correção de link em `tests/CMakeLists.txt` (`PluginSymbolExports.cpp`) para `CaffeineTest`.

---

## [2026-09-22] — Plugins opcionais & qualidade GPU

### Added
- **Plugin SDK** público (`include/caffeine/plugin/`) com host API, service registry e carregamento dinâmico (`dlopen` + `-rdynamic`).
- Plugins opcionais: **Git**, **Terrain Generator**, **Procedural Tools**, **Post Processing**, **Hello** (demo).
- Painel Git com status, stage/unstage, commit, branches, remotes, stash, log e diff.
- Sistema procedural modular (terrain profile, structure spawner, benchmarks Lua).
- Post-processing modular por efeito + presets benchmark + bindings Lua.
- Terrain heightmap importer, collision mesh builder, algoritmo ultra-realistic (Node.js).
- Roadmaps: engine pillars, rendering, Phong GPU handles, viewport quality.
- Documentação: `plugin-sdk.md`, `git-plugin.md`, terrain, post-processing.

### Changed
- Doppio core **lean**: geração de terrain e UI de features movidas para plugins.
- `CAFFEINE_BUNDLE_DEFAULT_PLUGINS` (OFF por defeito) — plugins não copiados automaticamente.
- Inspector delega drawers de Post Process ao plugin; mensagens quando plugin ausente.
- Preview de post-process no **Scene Viewport** + overlays mais visíveis.
- Command palette: atalho **Ctrl+L** (mantém Ctrl+Shift+P).
- IDs estáveis de plugin (stem do `.so`) — corrige crash SIGSEGV ao carregar múltiplos plugins.
- Melhorias GPU: Phong materials, shadow maps (directional/point/spot), texture LOD/cache, `GpuSceneRenderer`, shaders `scene_lit`/`terrain_lit`.
- `TimerScheduler`, `Camera3DSystem`, `PluginSymbolExports` para símbolos exportados a plugins.
- Viewport: pan/zoom 2D melhorado, click-to-select, qualidade de rendering.

### Removed
- Geração procedural de terrain embutida no core (`src/terrain/generation/*`).
- Bridges builtin terrain/procedural do Doppio (`TerrainBridge`, `ProceduralBridge` no core).

---

## [2026-09-18] — Runtime, build pipeline & polish

### Added
- Pipeline de build do jogo (`BuildDialog`, `BuildSystem`, 7 estágios).
- Executável `caffeine-runtime` para play sem editor.
- Sistema de plugins inicial no Doppio.
- Melhorias de workflow: startup dialog, asset browser, project manager.

### Changed
- Refinamentos de UX do editor e correções gerais ("major fix" ×3).

---

## [2026-05-24] — Testes UI & seleção 3D

### Added
- Framework de testes UI JSON (`TestUIMapper`, `TestRequestHandler`, headless Doppio).
- Raycasting de seleção 3D, multi-select, focus camera (double-click).
- Gizmo com raycast em eixos finitos.

---

## [2026-05-23] — Mesh, prefabs & rendering 3D

### Added
- Pipeline glTF/GLB → `.caf`, mesh cache, LOD framework.
- Drag-and-drop de meshes no viewport, "Save as Prefab".
- Rendering 3D de meshes glTF com texturas embutidas e fill semi-transparente.

---

## [2026-05-20 — 2026-05-22] — Viewport 3D, animação & ECS

### Added
- View modes 2D / 3D / Isometric, orbit camera, grid 3D, gizmos XYZ.
- Componente `Transform` unificado (remove Position2D/Rotation/Scale2D).
- Animator Controller, timeline com scrubber, parâmetros nomeados.
- Hierarquia com herança de transform, auto-expand no drag-drop.

### Changed
- Removidos `Health` e `Velocity2D` (migrado para `RigidBody2D`).

---

## [2026-05-17 — 2026-05-19] — Play mode, inspector & scripting

### Added
- **Play / Pause / Stop** com snapshot de entidades.
- Component registry unificado, Add Component pesquisável.
- Camera Preview panel, frustum Camera2D/3D no viewport.
- Collider debug overlay, tileset loading, dual scripting Lua + C++.
- CAP browsing, waveform preview, drag-drop auto-pack no Asset Browser.
- Grid no viewport, componentes Camera/Light/MeshRenderer.

---

## [2026-05-14 — 2026-05-16] — Doppio IDE (Caffeine Studio)

### Added
- Split **caffeine-core** + **doppio** executável.
- Painéis: Hierarchy, Inspector, Viewport, Asset Browser, Scene Editor orchestrator.
- Undo/redo snapshot-based, SceneSerializer binário, Transform Gizmos.
- Drag-and-drop AssetBrowser→Viewport, ProjectManager, SceneTabManager.
- Asset Pipeline (TextureCompiler, HotReloader), FileWatcher nativo Windows.
- Script Editor, NativeScriptComponent, sandboxing Lua, ECS Lua bindings.
- Particle System, Animation Timeline, Tilemap Editor, Command Palette.
- Material Editor (ShaderGraph + ImNodes), Audio Preview, BuildDialog.
- ProjectStartupDialog com toasts e projetos recentes.

### Changed
- Refactor dos painéis editor para `.hpp/.cpp` com design por membros.

---

## [2026-05-07 — 2026-05-11] — ECS, sistemas & editor base

### Added
- **ECS** archetype-based, CommandBuffer, ComponentQuery.
- **EventBus** pub/sub com dispatch diferido.
- **AssetManager** async/sync, cache, hot-reload.
- **BatchRenderer**, TextureAtlas, Camera2D.
- **SceneManager** + SceneSerializer, Physics2D, UI System, Audio (SDL3 spatial).
- Animation2D state machine, MeshLoader OBJ, Asset Pipeline encoders.
- ImGui integration (Profiler, Console, StatsOverlay).
- Quaternion/SLERP, Camera3D (FPS/Orbital/Follow), Octree, Frustum culling.
- Skeletal animation (bones, blend tree).
- Lua scripting (sol2), `caffeine-studio` executable.

---

## [2026-04-24] — Build scripts & binários

### Added
- Build manager Python, scripts `.sh` / `.bat` / `.fish`.
- Gestão de versões e binários.

---

## [2026-04-14] — Concorrência & RHI

### Added
- High-resolution Timer (µs).
- Game Loop fixed timestep (60 Hz) + interpolação.
- Input System action-based remapeável.
- Debug tools (Log, Profiler, DebugDraw).
- Job System work-stealing (RF2.2–RF2.6).
- **RHI** sobre SDL_GPU: RenderDevice, CommandBuffer (RF3.1–RF3.2).

---

## [2026-04-07] — Fase 1: Fundação atómica

### Added
- Tipos core (`u8`–`u64`, `f32`, platform defines).
- Allocators custom (Linear, Pool, Stack, `IAllocator`).
- Containers (`Vector`, `HashMap`, `FixedString`, `StringView`).
- Math (`Vec2`–`Vec4`, `Mat4`).
- CMake + Catch2 test suite.
- CI GitHub Actions (build + test).

### Changed
- Documentação consolidada em `docs/`, diagramas Mermaid no ROADMAP.

---

## [2026-04-06] — Início do projeto

### Added
- Repositório inicial, README, dev manifesto, roadmap.
- Estrutura de documentação e planos de fases 0–6.

---


---

## Apêndice — Histórico completo de commits

> 419 commits desde 2026-04-06 até 2026-09-22.

### 2026-04 (81 commits)

- `2026-04-06` [`387aeaa`] Initial commit
- `2026-04-06` [`53b9af6`] Create README.md
- `2026-04-06` [`8d2a6b1`] Update README.md
- `2026-04-06` [`dc6bac5`] Create dev_manifesto.md
- `2026-04-06` [`629d4c1`] Create roadmap.md
- `2026-04-06` [`ee8b146`] Update README.md
- `2026-04-06` [`d8e8abe`] feat/major refactor
- `2026-04-06` [`020038d`] Update README.md
- `2026-04-06` [`068c964`] Rename MASTER.md to README.md
- `2026-04-06` [`5f34a2d`] feat/major refactor
- `2026-04-06` [`d8e8d61`] Update README.md
- `2026-04-06` [`06bdae7`] docs: convert ASCII diagrams to Mermaid in ROADMAP.md
- `2026-04-06` [`8d58880`] feat/major refactor
- `2026-04-07` [`2b97375`] feat(core): add core types and platform definitions
- `2026-04-07` [`339cef7`] feat(memory): add custom allocator system
- `2026-04-07` [`7ffde40`] feat(containers): add custom containers and math types
- `2026-04-07` [`79fa9c9`] feat(build): add main include header and CMake setup
- `2026-04-07` [`ede0f3e`] feat(tests): add test system design doc
- `2026-04-07` [`5fc9cf8`] fix(ci): use correct std versions per compiler
- `2026-04-07` [`73cece3`] fix(ci): use integer std versions (20/17 instead of c++20/c++17)
- `2026-04-07` [`5c469a0`] fix(tests): replace MSVC-only /W4 flags with cross-platform equivalents
- `2026-04-07` [`2933910`] fix(tests): add Catch2 subdirectory to include path
- `2026-04-07` [`92cf593`] fix(containers): move HashMap constructor implementation out of class body
- `2026-04-07` [`adab94f`] fix(tests): qualify all Math namespace functions to avoid ambiguity with std::
- `2026-04-07` [`ff95152`] fix(tests): fix CF_INLINE test - macro expands to keywords, not value
- `2026-04-07` [`cef1270`] fix(tests): add CATCH_CONFIG_MAIN to provide main() function
- `2026-04-07` [`49cf0d0`] fix(tests): remove benchmarks - Catch2 v2 doesn't support BENCHMARK
- `2026-04-07` [`11fd8d6`] fix(ci): fix executable paths - tests are in build/tests/
- `2026-04-07` [`c530caa`] simplify(ci): minimal test workflow - build + run only
- `2026-04-07` [`70e8314`] fix(ci): skip stress tests in CI - 1M allocations is too slow
- `2026-04-07` [`f78952f`] fix(containers): add null allocator checks to prevent segfault
- `2026-04-07` [`be79322`] fix(containers): support Vector without allocator - use global new/delete
- `2026-04-07` [`8e6f522`] fix(tests): correct failing test assertions
- `2026-04-07` [`56b6975`] docs(tests): add test summary with architecture principles
- `2026-04-07` [`f858054`] Merge pull request #2 from devscafecommunity/feature/fase-1-test-system
- `2026-04-07` [`3e27864`] docs: consolidate documentation into docs/ directory
- `2026-04-07` [`1456645`] Merge pull request #3 from devscafecommunity/feature/fase-1-atomic-foundation
- `2026-04-07` [`5c2f3fc`] docs: add Phase 1 technical documentation with cross-references
- `2026-04-07` [`29e360d`] docs: add Phase 1 detailed review document
- `2026-04-07` [`e4269f3`] docs: move Phase1 review to docs/reviews/ and add stress test workflow
- `2026-04-07` [`f4ea9b6`] Update docs/containers/vector.md
- `2026-04-07` [`fe41527`] Update docs/containers/vector.md
- `2026-04-07` [`b13753b`] Apply suggestion from @Copilot
- `2026-04-07` [`a6fcaf9`] Apply suggestion from @Copilot
- `2026-04-08` [`62b6d0f`] docs: add 4.2 Caffeine Binary System (.caf) asset format section
- `2026-04-08` [`2dfdaee`] Update README.md
- `2026-04-11` [`aa75781`] Merge pull request #4 from devscafecommunity/feature/fase-1-atomic-foundation
- `2026-04-11` [`c80fb7f`] feat/setup documentation guidelines for job system
- `2026-04-14` [`fea12c2`] feat/setup guide docs
- `2026-04-14` [`6866ed3`] feat(timer): implement high-resolution timer with RF2.1 microsecond precision
- `2026-04-14` [`66a91d1`] Merge pull request #20 from devscafecommunity/9-high-resolution-timer
- `2026-04-14` [`eb4edae`] feat(timer): port high-resolution timer from main branch
- `2026-04-14` [`6a17572`] feat(gameloop): implement fixed timestep game loop with accumulator pattern (RF2.7, RF2.8)
- `2026-04-14` [`04d39cf`] Merge remote-tracking branch 'origin/main' into 11-the-master-game-loop
- `2026-04-14` [`ee19801`] Merge pull request #21 from devscafecommunity/11-the-master-game-loop
- `2026-04-14` [`467d7e5`] feat(input): implement action-based input system with remappable bindings (RF2.9)
- `2026-04-14` [`4f36538`] Merge pull request #24 from devscafecommunity/22-input-system
- `2026-04-14` [`63f116e`] feat/debug tools
- `2026-04-14` [`5275a05`] Merge pull request #25 from devscafecommunity/23-debug-tools
- `2026-04-14` [`9630ee2`] build(rhi): integrate SDL3 dependency with find_package and DLL auto-copy
- `2026-04-14` [`6e3bcb6`] feat(rhi): implement RenderDevice and CommandBuffer abstraction over SDL_GPU (RF3.1, RF3.2)
- `2026-04-14` [`fc2a10c`] build(rhi): make SDL3 optional so CI builds without it
- `2026-04-14` [`166c910`] Merge pull request #46 from devscafecommunity/12-rendering-hardware-interface-rhi
- `2026-04-14` [`7f34033`] feat(threading): implement Job System with work-stealing thread pool (RF2.2-RF2.6)
- `2026-04-14` [`34e9cc8`] Merge pull request #47 from devscafecommunity/10-job-system-worker-threads
- `2026-04-15` [`57c45b0`] docs: consolidate legacy plans/ into fase2-6 structure
- `2026-04-15` [`033b08a`] Merge pull request #49 from devscafecommunity/48-validate-and-standardize-systems
- `2026-04-16` [`d0a8329`] Update README.md
- `2026-04-16` [`c7e3e1d`] docs(readme): update documentation structure with phase details and status
- `2026-04-16` [`13fa742`] Merge branch 'main' into 48-validate-and-standardize-systems
- `2026-04-16` [`c48c78f`] Merge pull request #50 from devscafecommunity/48-validate-and-standardize-systems
- `2026-04-24` [`283b483`] feat: add Python build manager core
- `2026-04-24` [`c86b199`] fix: correct build_manager spec compliance (missing methods, return types, directories)
- `2026-04-24` [`ad2aa24`] feat: add Windows build script (.bat)
- `2026-04-24` [`e5347b0`] feat: add Unix build script (.sh)
- `2026-04-24` [`e6e8873`] docs: add scripts documentation
- `2026-04-24` [`72a8a84`] feat: add Fish shell build script (.fish)
- `2026-04-24` [`c22f359`] feat: add binary management scripts
- `2026-04-24` [`1897f13`] feat: add version manager for binaries
- `2026-04-24` [`3406df0`] docs: add implementation summary for binary management scripts
- `2026-04-24` [`02ca6f7`] Merge pull request #52 from devscafecommunity/51-compilation-and-binary-managment-scripts

### 2026-05 (329 commits)

- `2026-05-07` [`67ee38a`] Task 14: Expose ECS Core in Caffeine.hpp public API
- `2026-05-07` [`6e9ba1b`] Task 15: Add comprehensive ECS documentation and examples
- `2026-05-07` [`4761f52`] Add ECS development artifacts and planning documents
- `2026-05-07` [`756de69`] Fix: Include <functional> in ComponentSet.hpp to enable std::hash specialization
- `2026-05-07` [`50fe0ee`] Merge pull request #53 from devscafecommunity/32-ecs-core
- `2026-05-07` [`bec1364`] feat(io): implement .caf Caffeine Asset Format (Issue #28)
- `2026-05-07` [`5900f39`] fix(io): address validation findings from engineering requirements audit
- `2026-05-07` [`1cde368`] Merge pull request #54 from devscafecommunity/28-formato-caf-caffeine-asset-format
- `2026-05-08` [`07ec865`] feat(events): implement EventBus with pub/sub, deferred dispatch, and thread-safe queue
- `2026-05-08` [`6143b62`] docs: update events.md and caf-format.md to reflect implemented state
- `2026-05-08` [`67ddda7`] Merge pull request #55 from devscafecommunity/17-event-bus
- `2026-05-08` [`ec86b12`] feat(assets): implement AssetManager with async/sync loading, cache, and hot-reload
- `2026-05-08` [`209b1fe`] Merge pull request #56 from devscafecommunity/26-asset-manager
- `2026-05-08` [`c787dcb`] feat(render): implement Camera2D system (RF3.7)
- `2026-05-08` [`6c1f893`] Merge pull request #57 from devscafecommunity/29-camera-system
- `2026-05-08` [`8f228ed`] feat(render): implement BatchRenderer and TextureAtlas (#27)
- `2026-05-08` [`91e3d68`] Merge pull request #58 from devscafecommunity/27-batch-renderer
- `2026-05-08` [`5835df2`] feat(scene): implement SceneManager and SceneSerializer (RF4.5)
- `2026-05-08` [`33fbe84`] fix(scene): remove Color static const members, fix GCC default arg error
- `2026-05-08` [`03fa4f3`] fix(scene): add explicit default ctor to TransitionConfig for GCC compat
- `2026-05-08` [`14acfd3`] fix(scene): move TransitionType+TransitionConfig to namespace scope, fix GCC nested class error
- `2026-05-08` [`4ec9f23`] Merge pull request #59 from devscafecommunity/34-scene-manager
- `2026-05-08` [`e858991`] feat(physics): implement Physics2D system (RF4.10)
- `2026-05-08` [`fb69c78`] fix(physics): qualify ComponentQuery as ECS::ComponentQuery for GCC
- `2026-05-08` [`c7da097`] fix(physics): use std::fabs/sqrt/floor/fmin/fmax for GCC compatibility
- `2026-05-08` [`645d402`] fix(physics): fix std::fabsf in test file for GCC
- `2026-05-08` [`6ddd514`] fix(physics): replace Entity::get<T>() with World::get<T>(entity) for GCC compat
- `2026-05-08` [`8c69ef4`] fix(physics): fix layerMask test - set collider layerMask to exclude query layer
- `2026-05-08` [`a97fb1f`] Merge pull request #60 from devscafecommunity/33-physics-2d
- `2026-05-08` [`90970b6`] feat(ui): implement UI system — RF4.11
- `2026-05-08` [`412d36f`] fix(ui): replace c_str() with cStr() for FixedString GCC compat
- `2026-05-08` [`145fe34`] fix(ui): exclude Canvas from hitTest — layout roots are not interactive targets
- `2026-05-08` [`c143f95`] Merge pull request #61 from devscafecommunity/35-ui-system
- `2026-05-08` [`416cee1`] feat(audio): implement AudioSystem with SDL3 streaming and spatial 2D audio
- `2026-05-08` [`d191486`] Merge pull request #62 from devscafecommunity/31-audio-system
- `2026-05-08` [`7dc9ae7`] feat(animation): implement AnimationSystem with sprite state machine (RF4.9)
- `2026-05-08` [`1e2e05d`] Merge pull request #63 from devscafecommunity/30-animation-system
- `2026-05-08` [`30096cb`] feat(mesh): implement MeshLoader with OBJ parser and MeshSystem (RF5.2)
- `2026-05-08` [`b69fcfa`] Merge pull request #64 from devscafecommunity/40-mesh-loading
- `2026-05-08` [`68589d8`] feat(editor): implement Dear ImGui integration with ProfilerWindow, ConsoleWindow, StatsOverlay (RF6.1, RF6.2)
- `2026-05-08` [`6fb645c`] Merge pull request #65 from devscafecommunity/42-embedded-ui-dear-imgui
- `2026-05-08` [`dbc1559`] feat(pipeline): implement Asset Pipeline with TextureEncoder, AudioEncoder, MeshEncoder, AssetManifest (RF6.5)
- `2026-05-08` [`055f3bc`] fix(pipeline): fix AssetManifest load parsing multiple entries
- `2026-05-09` [`31e055a`] fix(pipeline): fix AssetManifest load parsing of string fields (id, path, type)
- `2026-05-09` [`928cc4c`] fix(pipeline): remove orphaned brace from AssetManifest load
- `2026-05-09` [`e41c8d9`] Merge pull request #66 from devscafecommunity/41-asset-pipeline
- `2026-05-09` [`bc46871`] feat(math): implement Quaternion with SLERP/NLERP, Euler, axis-angle (RF5.1)
- `2026-05-10` [`69bc719`] Update README.md
- `2026-05-10` [`5669b0f`] Update README.md
- `2026-05-10` [`6aa80de`] fix(tests): correct Quat toEuler pitch/yaw, FP comparisons, PoolAllocator assertions, CF_ASSERT signal handling
- `2026-05-10` [`4209091`] Merge pull request #67 from devscafecommunity/36-3d-math-extension
- `2026-05-10` [`46af94d`] feat(editor): implement EditorContext and NameComponent
- `2026-05-10` [`0be4783`] feat(editor): implement Hierarchy, Inspector, Viewport, and AssetBrowser panels
- `2026-05-10` [`b0ee6a5`] feat(editor): implement SceneEditor orchestrator
- `2026-05-10` [`d0d272d`] feat(editor): integrate editor includes into Caffeine.hpp and mark RF6.3/RF6.4 complete
- `2026-05-10` [`f0130e2`] Merge pull request #68 from devscafecommunity/43-scene-editor
- `2026-05-10` [`c461c5c`] feat(spatial): implement Octree with AABB3D, Frustum, and spatial queries
- `2026-05-10` [`ef1b64b`] feat(spatial): integrate Octree module into main engine header
- `2026-05-10` [`97fb937`] docs(spatial): update spatial partitioning documentation for Octree module
- `2026-05-10` [`1025e0b`] Merge pull request #69 from devscafecommunity/43-scene-editor
- `2026-05-10` [`97af1c6`] feat(math): add Mat4::inverted() using cofactor expansion
- `2026-05-10` [`228162c`] feat(camera3d): implement Camera3D with FPS, Orbital, Follow modes and frustum culling
- `2026-05-10` [`c556d52`] test(camera3d): add 20+ test cases for Camera3D
- `2026-05-10` [`6de934c`] fix(camera3d): correct Mat4::inverted() transpose bug and Camera3D sync directions
- `2026-05-10` [`b364116`] Merge pull request #70 from devscafecommunity/37-camera-3d
- `2026-05-10` [`e305f90`] feat(skeletal-animation): implement Bone/Skeleton, keyframe interpolation, blend tree, and ECS system
- `2026-05-10` [`b1efbc3`] Merge pull request #71 from devscafecommunity/39-skeletal-animation
- `2026-05-11` [`578605a`] feat(script): implement Lua scripting with sol2 and fix SIGSEGV from archetype mutation
- `2026-05-11` [`f6f5ecf`] Merge pull request #72 from devscafecommunity/39-skeletal-animation
- `2026-05-11` [`e32eb57`] docs: remove old phase-based documentation structure
- `2026-05-11` [`70848a6`] docs: reorganize documentation into functional modules
- `2026-05-11` [`9ccda52`] docs: add Caffeine Studio IDE planning (M1-M4 milestones)
- `2026-05-11` [`493b651`] Merge pull request #73 from devscafecommunity/docs/docs-reorganization
- `2026-05-11` [`cdf9b4f`] ci: add release workflow for v0.0.1-beta with core, ide, and bundle artifacts
- `2026-05-11` [`bf024d7`] ci: fix release workflow - separate core/full builds to avoid ImGui API errors
- `2026-05-11` [`f78401f`] feat(ide): add caffeine-studio executable and fix ImGui 1.91.9 API compatibility
- `2026-05-11` [`76c5db5`] fix(ide): fix ImGui 1.91.9 API compat and AssetManager constructor
- `2026-05-11` [`1848649`] fix(ide): change InspectorPanel HashMap key from ComponentID to u32
- `2026-05-14` [`5bb352b`] docs(planning): add T0 - core/IDE decoupling into caffeine-engine and doppio
- `2026-05-14` [`1e4a782`] refactor(build): split into caffeine-core library and doppio IDE executable (T0)
- `2026-05-14` [`d6f939f`] fix(tools): add missing <cstring> include in TextureEncoder.hpp
- `2026-05-14` [`80aaa56`] Merge pull request #82 from devscafecommunity/74-caffeine-studio-ide-plano-de-desenvolvimento
- `2026-05-14` [`881e136`] feat(editor): implement EditorContext with snapshot-based undo/redo
- `2026-05-14` [`4c924cd`] Merge pull request #83 from devscafecommunity/75-editorcontext-undoredo
- `2026-05-14` [`7b425ed`] feat(editor): implement dedicated HierarchyPanel with search, context menu, delete key
- `2026-05-14` [`806a30b`] Fix build: add missing HierarchyPanel.hpp include in test_editor.cpp
- `2026-05-14` [`98df3de`] Merge pull request #84 from devscafecommunity/76-hierarchy-panel
- `2026-05-14` [`7361406`] refactor(editor): split SceneViewport into .hpp/.cpp with member-based design (RF6.4)
- `2026-05-14` [`1fc0865`] Merge pull request #85 from devscafecommunity/78-scene-viewport
- `2026-05-14` [`e0e4bbe`] refactor(editor): split AssetBrowser into .hpp/.cpp with filesystem navigation (RF6.5)
- `2026-05-14` [`5b91eda`] Merge pull request #86 from devscafecommunity/79-asset-browser-v1
- `2026-05-14` [`c27ae3e`] refactor(editor): split InspectorPanel into .hpp/.cpp with component drawers (RF6.3)
- `2026-05-14` [`eb74478`] Merge pull request #87 from devscafecommunity/77-inspector-panel
- `2026-05-14` [`c262926`] feat(editor): split SceneEditor into .hpp/.cpp with dockspace, menus, console, profiler (RF6.6)
- `2026-05-14` [`9cdd3c2`] Merge pull request #88 from devscafecommunity/80-scene-editor-orchestrator
- `2026-05-14` [`64f0f86`] feat(editor): SceneSerializer with binary save/load + unsaved changes popup (RF6.7)
- `2026-05-14` [`0a07a0e`] Merge pull request #89 from devscafecommunity/81-saveload-scene
- `2026-05-14` [`ede11e9`] docs: add Transform Gizmos design document
- `2026-05-14` [`2dc4298`] docs: add Transform Gizmos implementation plan
- `2026-05-14` [`671a48d`] feat(editor): implement Transform Gizmos with translate/rotate/scale modes
- `2026-05-14` [`cbb395c`] Merge origin/main into 39-skeletal-animation
- `2026-05-14` [`52ce138`] fix: remove glm dependency, use Caffeine::Vec2/Vec3 instead
- `2026-05-14` [`ddbd1c4`] Merge pull request #97 from devscafecommunity/39-skeletal-animation
- `2026-05-15` [`0260fdf`] feat(editor): DragDropSystem with AssetBrowser→Viewport and Inspector asset drops (RF6.2)
- `2026-05-15` [`227a87e`] chore: retrigger CI
- `2026-05-15` [`77b0c05`] Merge pull request #98 from devscafecommunity/91-drag-and-drop
- `2026-05-15` [`4431659`] docs: add Asset Pipeline design document
- `2026-05-15` [`dc842ae`] feat(core): add native Windows FileWatcher (ReadDirectoryChangesW)
- `2026-05-15` [`36aa01d`] feat(assets): add AssetPipeline with IAssetCompiler interface and manifest system
- `2026-05-15` [`d4be1cd`] feat(assets): add TextureCompiler with stb_image PNG/JPG to .caf conversion
- `2026-05-15` [`4b3c025`] fix(core): guard FileWatcher Windows API behind _WIN32 for cross-platform build
- `2026-05-15` [`15ff33d`] Merge pull request #99 from devscafecommunity/92-asset-pipeline-integration
- `2026-05-15` [`e095133`] feat(assets): implement HotReloader for .caf asset hot-reloading
- `2026-05-15` [`03dd9f2`] docs: add HotReloader design document
- `2026-05-15` [`fb3eb55`] Merge pull request #100 from devscafecommunity/93-hot-reload-de-assets
- `2026-05-15` [`a9ed4b3`] feat(editor): implement ProjectManager for project lifecycle
- `2026-05-15` [`5aec7d4`] fix: add missing <algorithm> include for std::find on GCC/Linux
- `2026-05-15` [`65d7246`] Merge pull request #101 from devscafecommunity/94-project-manager
- `2026-05-15` [`8292dc7`] feat(editor): implement AssetBrowser v2 with data/UI separation
- `2026-05-15` [`a3e5119`] Merge pull request #102 from devscafecommunity/95-asset-browser-v2
- `2026-05-15` [`137e577`] feat(editor): add SceneTabManager with data/UI layers
- `2026-05-15` [`7301491`] feat(editor): integrate SceneTabManager into SceneEditor
- `2026-05-15` [`2aa4a9b`] test(editor): add SceneTabManager tests
- `2026-05-15` [`81eaf5a`] Merge pull request #103 from devscafecommunity/96-multiple-scene-tabs
- `2026-05-15` [`466de9e`] build: add caffeine-ui interface library and caffeine-combined executable targets
- `2026-05-15` [`a071c6d`] fix(assets,audio): fix MeshLoader createBuffer signature and SDL3 audio device constant
- `2026-05-15` [`858c074`] fix(rhi): fix frame lifecycle — acquire swapchain in beginFrame, create real render pass in beginRenderPass
- `2026-05-15` [`88fbcc4`] fix(editor): fix doppio startup crash — enable docking, guard null viewport texture, add imgui_internal
- `2026-05-15` [`3a91665`] feat(apps): add caffeine-combined app demonstrating core + ui integration
- `2026-05-15` [`3c9885a`] Merge pull request #104 from devscafecommunity/96-multiple-scene-tabs
- `2026-05-15` [`30eff55`] feat(editor): add native Script Editor for .lua files
- `2026-05-15` [`737d0cd`] fix(doppio): fix SceneViewport default Config + add ScriptEditorWindow to build
- `2026-05-15` [`fa32f00`] Merge pull request #113 from devscafecommunity/105-31-lua-vm
- `2026-05-15` [`b03e195`] feat(script): add NativeScriptComponent for C++ scripts
- `2026-05-15` [`e620b92`] Merge pull request #114 from devscafecommunity/106-32-script-component-system
- `2026-05-15` [`a00f557`] feat(script): complete sandboxing implementation
- `2026-05-15` [`824c359`] Merge pull request #115 from devscafecommunity/108-34-sandboxing
- `2026-05-15` [`ca8dbc1`] feat(script): add addTransform/addSprite aliases to match spec API
- `2026-05-15` [`cd301c2`] Merge pull request #116 from devscafecommunity/107-33-ecs-lua-bindings
- `2026-05-15` [`0122d6b`] feat(editor): add ScriptComponent to InspectorPanel
- `2026-05-16` [`b78c2d1`] Merge pull request #117 from devscafecommunity/110-36-editor-script-integration
- `2026-05-16` [`fb3853e`] feat(graphics): add Particle System
- `2026-05-16` [`47e431e`] Merge pull request #118 from devscafecommunity/111-37-particle-system
- `2026-05-16` [`a0dfb46`] feat(editor): add Animation Timeline Editor panel
- `2026-05-16` [`a982a0d`] Merge pull request #127 from devscafecommunity/119-41-animation-timeline-editor
- `2026-05-16` [`a615c03`] feat(editor): add Tilemap Editor panel
- `2026-05-16` [`ec33ede`] Merge pull request #128 from devscafecommunity/120-42-tilemap-editor
- `2026-05-16` [`7e93ba4`] feat(editor): add Command Palette
- `2026-05-16` [`55a6eda`] feat(editor): integrate new panels into SceneEditor
- `2026-05-16` [`5e090b1`] fix: suppress unused parameter warnings
- `2026-05-16` [`46f12b5`] fix: resolve CI build warnings and test failures
- `2026-05-16` [`9aae1b8`] Merge pull request #129 from devscafecommunity/feature/command-palette
- `2026-05-16` [`59f7db8`] docs: add Material & Shader Editor design document
- `2026-05-16` [`4a292f8`] feat(editor): add ShaderNode hierarchy with code generation
- `2026-05-16` [`9b58e63`] feat(editor): add ShaderGraph with GLSL compilation
- `2026-05-16` [`458804b`] feat(editor): add Material Editor panel with ImNodes graph
- `2026-05-16` [`380282a`] feat(editor): add PreviewRenderer with ImGui fallback
- `2026-05-16` [`b108fcf`] feat(editor): add edge case handling for node deletion and empty graph
- `2026-05-16` [`00e2dce`] docs: add implementation plan, remove fmt dependency from shader files
- `2026-05-16` [`93348df`] Merge branch 'main' into 122-43-material-shader-editor
- `2026-05-16` [`90f6d9a`] Merge pull request #130 from devscafecommunity/122-43-material-shader-editor
- `2026-05-16` [`8ae3c74`] feat(editor): add Audio Preview & Spatial Placement (#121)
- `2026-05-16` [`7fbca5d`] fix(editor): add ImNodes CreateContext and fix query ordering to prevent MaterialEditorPanel crash
- `2026-05-16` [`8400763`] test: add E2E testing infrastructure with imgui_test_engine and DoppioTest
- `2026-05-16` [`81855ed`] docs: complete frontend audit - implement drawRigidBody2D, document unfinished stubs
- `2026-05-16` [`477d959`] feat: add universal build script for easy project compilation
- `2026-05-16` [`d65b19b`] docs: add BuildSystem Integration design document
- `2026-05-16` [`80d5611`] feat: implement BuildSystem integration with 7-stage pipeline, BuildDialog UI, and AssetCooker stubs
- `2026-05-16` [`81e8245`] fix: add imgui_test_engine sources to doppio executable linking
- `2026-05-16` [`0bf8067`] feat: implement ProjectStartupDialog - critical project startup workflow
- `2026-05-16` [`c1c7d67`] feat: add drag-drop support to ScriptEditorWindow - drag .lua files from Asset Browser
- `2026-05-16` [`0413395`] feat: add drag-drop support to AudioPreviewPanel - drag .wav/.ogg files from Asset Browser
- `2026-05-16` [`60d5957`] feat: implement AnimationTimeline playback with delta time - add frame timing and advance animations during render
- `2026-05-16` [`238637f`] feat: implement TilemapEditor visual grid canvas with tile painting and tools - adds interactive grid display with brush, bucket, eraser, and picker tools
- `2026-05-16` [`e118639`] fix: use BeginPopupModal for ProjectStartupDialog to ensure proper rendering
- `2026-05-16` [`d4e48d7`] Fix ProjectStartupDialog: switch from modal popup to regular window, simplify UI
- `2026-05-16` [`a135349`] Fix: ProjectStartupDialog ImGui rendering
- `2026-05-16` [`4cb2f45`] docs: add ProjectStartupDialog tabs design specification
- `2026-05-16` [`a4aef38`] docs: add ProjectStartupDialog tabs implementation plan
- `2026-05-16` [`6ef22e6`] feat: add toast notification data structure to ProjectStartupDialog
- `2026-05-16` [`946af26`] feat: implement toast notification methods (showToast, updateToasts)
- `2026-05-16` [`af1a708`] feat: implement toast notification rendering with ImGui
- `2026-05-16` [`fa5b334`] feat: refactor render() into tab-based architecture with BeginTabBar
- `2026-05-16` [`6e7b7f9`] feat: add recent projects state variables to ProjectStartupDialog
- `2026-05-16` [`690affe`] feat: implement Open Recent tab with search filtering
- `2026-05-16` [`4f1031f`] feat: implement Browse Projects tab with results list
- `2026-05-16` [`27177fc`] feat: implement file picker for project creation and browsing
- `2026-05-16` [`ba207d4`] fix: prevent state pollution between multiple file pickers
- `2026-05-16` [`6096d96`] fix: file picker state management and integration
- `2026-05-16` [`67e85c5`] fix: prevent file picker from re-opening after cancellation
- `2026-05-16` [`ec1a61b`] fix: call ImGui::End() regardless of Begin() return value
- `2026-05-16` [`feea0ee`] fix: ImGui ID conflicts in Open Recent and Browse tabs
- `2026-05-16` [`107ed2c`] fix: close ProjectStartupDialog when project is selected
- `2026-05-16` [`adf49c4`] docs: add comprehensive session summary for ProjectStartupDialog implementation
- `2026-05-17` [`23d3f57`] chore: add .worktrees/ to gitignore
- `2026-05-17` [`fc6094d`] docs: add three-phase design for doppio editor integration + phase 2-3 features
- `2026-05-17` [`45db648`] docs: add detailed three-phase implementation plan with bite-sized tasks
- `2026-05-17` [`fc4c941`] feat: add CAP file browsing mode to Asset Browser
- `2026-05-17` [`9abec52`] feat: add audio waveform generator for Asset Browser previews
- `2026-05-17` [`715a95c`] feat: add drag-drop auto-pack import to Asset Browser
- `2026-05-17` [`b2e2108`] feat: add placeholder for game.cap auto-load on project open
- `2026-05-17` [`5c2187b`] feat: add Phase 1 integration tests with Catch2
- `2026-05-17` [`bb5135d`] chore: update caf-pack submodule reference (Task 2.1: Mesh Processor)
- `2026-05-17` [`7ed1c73`] chore: update caf-pack submodule reference (Task 2.2: Compression)
- `2026-05-17` [`24a517c`] Submodule: Update caf-pack to include Task 2.3 (Asset ID Header Generator)
- `2026-05-17` [`4ad5118`] Task 3.1: Implement async asset loading infrastructure
- `2026-05-17` [`33ec08d`] Task 3.2: Document complete ecosystem workflow
- `2026-05-17` [`0c7f14d`] Task 3.3: Add Phase 2 & 3 verification tests
- `2026-05-17` [`9103dc0`] fix: add missing CapLoader and AudioWaveformRenderer to DoppioTest
- `2026-05-17` [`2a5de94`] fix: update caf-pack submodule to use FNV-1a per specification
- `2026-05-17` [`4596a2d`] feat: add grid rendering to Scene Viewport
- `2026-05-17` [`d5fb975`] feat: add Camera2D and Camera3D components
- `2026-05-17` [`45b9d6b`] feat: add Light components (Directional, Point, Spot)
- `2026-05-17` [`c5f5bd0`] feat: add MeshRenderer components
- `2026-05-17` [`770c4a0`] feat: add entity type selector UI to Hierarchy Panel
- `2026-05-17` [`99152ef`] fix: rename ECS components to avoid namespace collisions
- `2026-05-18` [`6eaad41`] Fix namespace collision and build errors - remove using namespace Caffeine from editor headers
- `2026-05-18` [`fa7b949`] Fix caf-pack subdirectory reference in CMakeLists.txt
- `2026-05-18` [`0ccdfb8`] fix: make caf-pack submodule optional in CMake build
- `2026-05-18` [`57bd09d`] fix: make editor dependencies optional for headless build
- `2026-05-18` [`88d768a`] fix: wrap caf-pack dependencies in AssetBrowser and disable tests
- `2026-05-18` [`e097e67`] fix: resolve ProjectStartupDialog linker errors in doppio
- `2026-05-18` [`2767410`] fix: enable FilePicker on Windows for Browse and Recent tabs
- `2026-05-18` [`40a575b`] feat: apply cleaner default layout profile on IDE startup
- `2026-05-18` [`05ef449`] fix: increase FilePicker window size to reduce scrolling
- `2026-05-18` [`417a0d4`] feat: enable Lua scripting by default for editor build
- `2026-05-18` [`ac7125a`] feat(editor): add play mode state and system members to SceneEditor
- `2026-05-18` [`d30b98d`] feat(editor): implement play/pause/stop mode with entity snapshot and system ticking
- `2026-05-18` [`dd6e12f`] feat(editor): add physics collider debug overlay to scene viewport
- `2026-05-18` [`ae5161d`] feat(editor): add script component inspector drawer with path and load button
- `2026-05-18` [`1f47794`] feat(editor): add tileset asset loading with texture-based tile palette
- `2026-05-18` [`59929fa`] feat(editor): wire tileset load command into command palette
- `2026-05-18` [`5f33437`] feat(editor): add Collider2D, Velocity2D and Health inspector drawers
- `2026-05-18` [`52ab107`] fix(editor): sync recent projects list from ProjectManager on every render
- `2026-05-18` [`b0ddf27`] feat: multi-select, copy/paste/duplicate, physics debug toggle, snap to grid, undo coverage, settings apply
- `2026-05-18` [`71ccdf6`] feat: add GameObject primitives, UI components, and UISystem integration
- `2026-05-18` [`3cd1d10`] docs: add Inspector 2.0 implementation plan
- `2026-05-18` [`092717e`] feat(ecs): add Transform component and DisabledTag
- `2026-05-18` [`0bdef6d`] feat(editor): add InspectorWidgets helper library
- `2026-05-18` [`71eef32`] feat(editor): add ComponentRegistry with all component entries
- `2026-05-18` [`7b4148c`] feat(inspector): unified Transform drawer and ComponentHeader lifecycle
- `2026-05-18` [`79fea16`] feat(inspector): asset picker fields for Sprite, AudioSource, MeshFilter
- `2026-05-18` [`8d97efe`] feat(inspector): searchable Add Component menu via ComponentRegistry
- `2026-05-18` [`9790e25`] feat(hierarchy): new entities created with Transform component
- `2026-05-19` [`70c0a23`] fix: 4 editor bugs - transform sync, gizmo, component popup, tab flickering
- `2026-05-19` [`de326a0`] feat: dual script system - Lua and C++ scripts
- `2026-05-19` [`60f61e2`] refactor: move game scripts from scripts/ to assets/scripts/
- `2026-05-19` [`3191715`] feat(editor): move play/pause/stop buttons into main menu bar
- `2026-05-19` [`b03e83f`] fix(editor): add missing Position2D constraint in enterPlayMode query to prevent segfault
- `2026-05-19` [`2e4979c`] feat(editor): empty entities now include Transform/Position2D/Rotation/Scale2D by default
- `2026-05-19` [`d4101f3`] feat(viewport): draw diamond marker for empty entities with Position2D but no Sprite
- `2026-05-19` [`59434b2`] fix(editor): add Transform/Position2D to entities created via toolbar + button
- `2026-05-19` [`d035dac`] feat(editor): collider debug color per-collider + ColorEdit4 in inspector
- `2026-05-19` [`800b1aa`] fix(physics): remove Collider2D dependency from integrate/sleep; auto-add Velocity2D with RigidBody2D
- `2026-05-19` [`454fef1`] fix(collider): world-unit sizes — default 1x1, drag step 0.01 so colliders appear at correct scale
- `2026-05-19` [`cdc2ad4`] fix(physics): correct gravity (-9.81 not *60), disable sleep under gravity, lower sleep threshold to 0.05
- `2026-05-19` [`b781a2f`] fix(collider): default size 1x1 radius 0.5 in ComponentRegistry (was hardcoded 64x64)
- `2026-05-19` [`1d88045`] feat(editor): Camera Preview dock panel — shows scene from camera POV, No Cameras detected fallback
- `2026-05-19` [`1563e7a`] chore: remove stale example script files
- `2026-05-20` [`ad87924`] feat(editor): Camera2D auto-adds Position2D; draw camera frustum in scene viewport
- `2026-05-20` [`ce447ae`] feat(ecs): unify Transform component, remove Position2D/Rotation/Scale2D
- `2026-05-20` [`5b6e004`] feat(editor): add ViewMode + camera orbit state to EditorContext
- `2026-05-20` [`1005469`] feat(viewport): 3D/2D/Iso modes, XYZ gizmos, orbit camera, 3D grid
- `2026-05-20` [`08b802a`] fix(viewport): infinite grid, 3D pan, projected gizmo axes
- `2026-05-20` [`654a565`] fix(viewport): extend grid to horizon, normalize gizmo axis lengths
- `2026-05-20` [`2269540`] fix(viewport): depth-clip grid, arrow key navigation, revert middle mouse
- `2026-05-20` [`874e3e2`] fix(viewport): arrow key ux, grid quadrant, gizmo Z fallback, nav widget label
- `2026-05-20` [`1467b5c`] fix(viewport): near-plane clip grid lines instead of skipping
- `2026-05-20` [`2897f47`] fix(viewport): camera-oriented mini gizmo, gizmo Z/Y overlap guard, arrow key directions
- `2026-05-20` [`13d241a`] docs: add formal LaTeX specification for Caffeine Engine internals
- `2026-05-20` [`d478a11`] refactor(docs): split caffeine-internals into folder structure with sub-files
- `2026-05-20` [`2745396`] fix(docs): prevent orphan headings and split lists with needspace + widow/club penalties
- `2026-05-20` [`91d2b3d`] docs: complete caffeine-internals LaTeX spec with 11 new chapters
- `2026-05-20` [`b455de7`] fix(editor): TransformGizmo absolute drag, MaterialEditorPanel layout + fix(docs): LaTeX errors, add provisional logo to cover
- `2026-05-20` [`f118777`] Changes
- `2026-05-21` [`94be56e`] docs(latex): fix LaTeX syntax errors in specbox environments and compile PDF
- `2026-05-21` [`a6a7662`] fix(gizmo): world-space axes \u2014 remove hardcoded screen-space fallback, show dot when axis collapses toward camera
- `2026-05-21` [`bf30b8b`] fix(editor): TransformGizmo analytic foreshortening for 3D world-space axes
- `2026-05-21` [`21df694`] fix(editor): drawGizmo 3D analytic foreshortening with depth sort and alpha
- `2026-05-21` [`f249394`] feat(editor): gizmo 3D rotate rings, axis hover highlight, X/Y/Z key constraint
- `2026-05-21` [`50a45a6`] fix(editor): gizmo local space + inspector 3D rotation/scale fields
- `2026-05-21` [`12d4e94`] feat(editor): entity hierarchy with transform inheritance
- `2026-05-21` [`56092cd`] feat(editor): auto-expand parent after drag-drop + Unparent action
- `2026-05-21` [`943c412`] feat(hierarchy): transform/disabled/layer inheritance
- `2026-05-22` [`b63c0b1`] feat(animation): named parameter system with SetBool/SetFloat/SetTrigger and HashMap range-for
- `2026-05-22` [`5f59e97`] feat(editor): AnimationTimeline ruler, scrubber and keyframe diamonds
- `2026-05-22` [`1376a53`] feat(editor): AnimatorController window with state machine canvas and internal animator
- `2026-05-22` [`a0b511d`] feat(editor): wire AnimatorController into SceneEditor docking, command palette and View menu
- `2026-05-22` [`56fe630`] refactor(ecs): remove game-specific Health struct and migrate Velocity2D into RigidBody2D
- `2026-05-22` [`a90dcbd`] refactor(editor): remove Health and Velocity2D from inspector, registry, serializer and hierarchy
- `2026-05-22` [`ee2c2e4`] refactor(engine): remove Health and Velocity2D from scene serializer and Lua scripting bindings
- `2026-05-22` [`d9fbb23`] feat(editor): AssetBrowser updates
- `2026-05-22` [`d9fb901`] docs(animation): add scripting reference chapter 11 covering animation API
- `2026-05-23` [`bf94a16`] fix: support Position3D/Rotation3D in light gizmo rendering
- `2026-05-23` [`4ab0a9d`] docs: add chapter on polygons and 3D representations to internal reference
- `2026-05-23` [`3fb5eaa`] feat: implement glTF/glb mesh encoding to .caf format
- `2026-05-23` [`351e759`] feat: add mesh asset resolution to runtime AssetManager
- `2026-05-23` [`e925027`] feat: add prefab asset resolution to runtime AssetManager
- `2026-05-23` [`95c61e1`] feat: enable mesh drag-and-drop in scene viewport
- `2026-05-23` [`2892631`] feat: add 'Save as Prefab' command to inspector
- `2026-05-23` [`01aa9cd`] fix: update PrefabSerializer to use CAF format instead of custom binary
- `2026-05-23` [`3ee31f7`] feat: complete mesh and prefab pipeline wiring with editor integration
- `2026-05-23` [`883b45a`] wiring: integrate mesh and prefab components with editor systems
- `2026-05-23` [`0f33e1d`] feat: Implement 3D glTF mesh rendering in viewport
- `2026-05-23` [`03d5d30`] feat: add mesh caching system to avoid reloading per frame
- `2026-05-23` [`dbc6319`] feat: integrate mesh cache into viewport rendering
- `2026-05-23` [`0d222a3`] feat: add filled geometry rendering with semi-transparent blue fill
- `2026-05-23` [`d6568a9`] feat: load and display embedded glTF textures in viewport
- `2026-05-23` [`826fae3`] feat: add LOD level generation framework for mesh simplification
- `2026-05-23` [`53bfd40`] docs: add detailed implementation plan for gizmo raycasting with VP-inverse
- `2026-05-23` [`6737367`] feat(gizmo): add screenToWorldRay helper for VP-inverse raycasting
- `2026-05-23` [`a4dd94e`] feat(gizmo): add rayToAxisSegmentDistance for finite axis picking
- `2026-05-23` [`bafbc47`] feat(gizmo): integrate raycasting into intersectTest for world-space picking
- `2026-05-23` [`561ec8e`] fix(gizmo): add edge case handling for VP inverse and axis length validation
- `2026-05-23` [`9a569f3`] docs(gizmo): add performance & threshold tuning analysis
- `2026-05-23` [`3d8c1a9`] feat(selection): add rayIntersectsAABB and raycastSelectEntity functions
- `2026-05-23` [`3044f14`] feat(selection): integrate click detection and raycasting into viewport
- `2026-05-23` [`6f200fc`] fix(selection): add edge case handling for invalid AABBs and point entities
- `2026-05-23` [`f76d062`] feat(editor): add delete key to remove selected entity with undo
- `2026-05-23` [`f3e8a10`] feat(editor): add multi-select with Shift+Click (toggle selection)
- `2026-05-23` [`638db40`] feat(editor): add double-click to focus camera on selected entity
- `2026-05-24` [`3ce2bf0`] feat: add TestUIMapper and TestRequestHandler headers for UI test framework
- `2026-05-24` [`ccb42ea`] feat: implement TestUIMapper::captureViewportState and clickAtCoordinate
- `2026-05-24` [`91b0f7e`] feat: implement TestRequestHandler JSON parsing and response generation
- `2026-05-24` [`2d0886b`] feat: integrate TestRequestHandler into SceneViewport render loop
- `2026-05-24` [`3b7346c`] feat: add JSON-based UI test framework with headless Doppio
- `2026-05-24` [`5950274`] Content update
- `2026-05-26` [`38668c8`] fix: correct 3D rendering pipeline and add automated UI tests
- `2026-05-29` [`0a7146b`] fix(math): correct Mat4::transformVector() for non-diagonal matrices

### 2026-09 (9 commits)

- `2026-09-18` [`716a990`] feat(editor): add build pipeline, runtime, plugins, and editor UX fixes
- `2026-09-18` [`72cc4dc`] feat(editor): polish project workflow, asset browser, and editor UX
- `2026-09-18` [`461971e`] Major changes
- `2026-09-18` [`90abf19`] major fix
- `2026-09-19` [`33b9bd2`] major fix
- `2026-09-20` [`60e83a2`] major fix
- `2026-09-21` [`d9aa52d`] updating documentation
- `2026-09-22` [`ba774aa`] Add optional editor plugin SDK and move feature tooling out of Doppio core.
- `2026-09-22` [`cfbc012`] Improve GPU viewport rendering quality, shadows, and texture LOD.
