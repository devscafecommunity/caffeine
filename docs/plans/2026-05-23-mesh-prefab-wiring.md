# MESH & PREFAB ASSET PIPELINE - ANÁLISE COMPLETA & WIRING PLAN

## 📊 STATUS ATUAL

### ✅ Implementado (Base Foundation)
1. **MeshLoader::parseGLTF()** - Interpreta .glb/.gltf via tinygltf
2. **MeshEncoder** - Serializa Mesh → .caf binary
3. **AssetManager::resolveMesh()** - Zero-copy mesh loading
4. **PrefabSerializer** - Save/load entities em CAF format
5. **AssetManager::resolvePrefab()** - Prefab resolution
6. **Mesh drag-drop** - SceneViewport auto-cria MeshFilterComponent + MeshRendererComponent
7. **Save as Prefab button** - InspectorPanel com FilePicker

### ❌ Falta para Integração Completa (Wiring)

## 🔴 GAPS CRÍTICOS

### Gap 1: Scene Serialization Incompleta
**Problema**: SceneSerializer não conhece MeshFilterComponent/MeshRendererComponent
**Consequência**: Cenas com meshes não salvam/carregam
**Solução**: Extend SceneSerializer com type IDs 18-19

### Gap 2: Prefab Instances Não Rastreadas
**Problema**: Sem componente PrefabInstance, não há como saber se entity veio de prefab
**Consequência**: Sem suporte a prefab linking/updates
**Solução**: Criar PrefabComponents.hpp com PrefabInstance struct

### Gap 3: Asset Cooking Incompleto
**Problema**: AssetCooker não tem CookMeshes()
**Consequência**: Build não converte .obj/.gltf/.glb → .caf automaticamente
**Solução**: Add AssetCooker::CookMeshes() com MeshLoader + MeshEncoder

### Gap 4: Sem UI para Instanciar Prefabs
**Problema**: Sem menu "Place Prefab..." no editor
**Consequência**: Usuários não conseguem colocar prefabs após salvar
**Solução**: Add menu item em HierarchyPanel + FilePicker

### Gap 5: Prefab Instances Não Persistem
**Problema**: Scenes não salvam/carregam PrefabInstance data
**Consequência**: Prefab instances perdidas ao fechar/reabrir
**Solução**: Extend SceneSerializer com type ID 20 para PrefabInstance

### Gap 6: UX Fraca na Save Prefab
**Problema**: Sem validação/feedback ao salvar prefab
**Consequência**: Usuários não sabem se funcionou
**Solução**: Add notifications + error handling

## 📋 PLANO DE WIRING (6 FASES SEQUENCIAIS)

### FASE 1: SceneSerializer - Mesh Support
- [ ] Add type IDs: kTypeMeshFilter=18, kTypeMeshRenderer=19
- [ ] collectMeshFilterComponents()
- [ ] collectMeshRendererComponents()
- [ ] applyMeshFilterComponent()
- [ ] applyMeshRendererComponent()
- **Files**: src/editor/SceneSerializer.hpp/cpp
- **Est. Tempo**: 30 min
- **Critério**: Scene save/load round-trip preserves mesh paths

### FASE 2: PrefabInstance Component
- [ ] Create src/ecs/PrefabComponents.hpp
- [ ] struct PrefabInstance { prefabPath, rootEntityId }
- [ ] Register no ComponentRegistry
- **Files**: src/ecs/PrefabComponents.hpp
- **Est. Tempo**: 15 min
- **Critério**: Component appears in registry, no compile errors

### FASE 3: AssetCooker - Mesh Cooking
- [ ] Add AssetCooker::CookMeshes()
- [ ] Integrate MeshLoader + MeshEncoder
- [ ] Hook na build system
- [ ] Create output .caf files
- **Files**: src/editor/AssetCooker.hpp/cpp
- **Est. Tempo**: 45 min
- **Critério**: Build generates .caf from source meshes

### FASE 4: HierarchyPanel - Place Prefab Menu
- [ ] Add "Place Prefab..." menu item
- [ ] FilePicker → select .caf prefab
- [ ] PrefabSerializer::load()
- [ ] Create PrefabInstance wrapper
- [ ] Entity appears in viewport
- **Files**: src/editor/HierarchyPanel.cpp
- **Est. Tempo**: 40 min
- **Critério**: Can place prefab via menu, entity visible

### FASE 5: SceneSerializer - Prefab Instance Support
- [ ] Add type ID: kTypePrefabInstance=20
- [ ] collectPrefabInstanceComponents()
- [ ] applyPrefabInstanceComponent()
- [ ] Serialize prefabPath
- **Files**: src/editor/SceneSerializer.hpp/cpp
- **Est. Tempo**: 30 min
- **Critério**: Scene with prefab instances save/load correctly

### FASE 6: UX Polish - Notifications & Validation
- [ ] Validate entity in savePrefab()
- [ ] Add error notifications
- [ ] Add success notifications with file path
- [ ] Ask for destination directory
- [ ] Handle edge cases
- **Files**: src/editor/InspectorPanel.cpp
- **Est. Tempo**: 25 min
- **Critério**: UX feedback working, errors caught

## 🎯 TOTAL EFFORT
**Estimated**: 3-4 horas de implementação
**Complexity**: Média (integração, não novo algoritmo)
**Risk**: Baixo (mudanças localizadas)

## 📐 DEPENDENCIES

```
Fase 1 ← Fase 2 ← Fase 3 (independent)
                ↓
        Fase 4 ← Fase 5 ← Fase 6 (independent)
```

**Path crítico**: 1 → 2 → 4 → 5
**Path paralelo**: 3 pode ser feito em paralelo com 1-2

## ✅ ACCEPTANCE CRITERIA (POR FASE)

### Fase 1
- Mesh scene save/load preserves MeshFilterComponent.customMeshPath
- Mesh scene save/load preserves MeshRendererComponent.meshPath

### Fase 2
- PrefabInstance component compiles
- Appears in Component Add menu

### Fase 3
- CookMeshes() callable from AssetCooker
- Generates .caf files from .obj/.gltf/.glb
- Files in correct output directory

### Fase 4
- "Place Prefab..." menu exists
- FilePicker works
- Entity instantiates from prefab

### Fase 5
- PrefabInstance data serializes
- Scene save includes prefab path
- Scene load recreates instance

### Fase 6
- Notifications appear on save
- Errors caught and displayed
- User can select directory

## 🚀 PRÓXIMOS PASSOS

1. User approval deste plano
2. Start FASE 1: SceneSerializer mesh support
3. Sequential implementation das 6 fases
4. Testing após cada fase
5. Final integration test: full workflow

