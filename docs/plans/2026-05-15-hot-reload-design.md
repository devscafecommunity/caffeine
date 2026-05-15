# Hot-Reload de Assets — Design

**Date:** 2026-05-15
**Issue:** #93
**Milestone:** M2 — Visual Editing & Assets
**Namespace:** `Caffeine::Assets`

## Overview

Monitoramento em background do diretório `assets/processed/` para detectar alterações em arquivos `.caf` recém-compilados pelo Asset Pipeline e acionar recargas in-place no `AssetManager` — sem reiniciar a engine ou perder o estado da cena no editor.

## Arquitetura

```
[ assets/processed/*.caf ]  ←── AssetPipeline escreve .caf atualizado
       │
       ▼
  HotReloader (background thread)
       │  poll a cada 500ms
       │  debounce 100ms (configurável)
       │
       ├── detecta timestamp change
       ├── aguarda estabilidade (debounce)
       └── enfileira path em m_PendingReloads (mutex)
       │
       ▼
  HotReloader::Update()  ←── chamado na main thread (ex: EditorContext::Update)
       │
       ├── desenfileira batch sob mutex
       ├── AssetManager::reloadAsset(path)
       │     ├── reseta LinearAllocator + resolved data
       │     ├── loadInternal(id) — fopen, BlobLoader, resolveEntry
       │     └── retorna assetId
       ├── determina tipo via getResolved<T>()
       └── NotificationCallback(assetInfo) — editor toast
```

## Componentes

### `HotReloader` (class)

| Método | Descrição |
|--------|-----------|
| `Start(manager, processedDir, debounceMs)` | Inicia thread de polling |
| `Stop()` | Join thread, limpa estado |
| `Update()` | Processa fila de reloads na main thread |
| `SetNotificationCallback(cb)` | Hook para notificação visual (editor toast) |
| `IsRunning()` | Estado atual |

### `HotReloadedAsset` (struct)

| Campo | Tipo | Descrição |
|-------|------|-----------|
| `assetId` | `u32` | ID do asset recarregado |
| `name` | `std::string` | Nome do arquivo sem extensão |
| `type` | `AssetType` | Texture / Audio / Shader |

### `AssetManager::reloadAsset(path)`

Método público adicionado para trigger externo de reload:

1. Busca `path` em `m_pathIndex` (com `m_basePath` prefixado)
2. Reseta: `allocator.reset()`, `header/metadata/payload = nullptr`, `resolved = {}`
3. Subtrai `sizeBytes` de `m_cachedBytes`
4. Status → `Unloaded`
5. Libera mutex
6. `loadInternal(id)` — recarrega do disco, re-resolve
7. Retorna `assetId` (ou `~0u` se não encontrado)

## Threading

- **Poll thread** (criada em `Start()`):
  - Itera `directory_iterator` no diretório processado a cada 500ms
  - Compara `last_write_time` com snapshot anterior
  - Máquina de estados 3-state: *unmodified → debouncing → ready*
  - Escreve em `m_PendingReloads` sob `m_Mutex`
- **Main thread** (via `Update()`):
  - Lê `m_PendingReloads` sob `m_Mutex`, troca com vetor vazio
  - Executa reload fora do lock
  - Dispara callback de notificação

## Debounce

Três estados por arquivo monitorado:

1. **Unmodified** (`debounceStart == -1`): timestamp estável entre polls
2. **First change detected** (`debounceStart >= 0`): timestamp mudou, timer inicia
3. **Debounce elapsed** (`>= debounceMs` sem novas mudanças): reload é enfileirado

## Testes

5 testes em `tests/test_assetmanager.cpp`:

| Teste | O que verifica |
|-------|---------------|
| `default state is not running` | `IsRunning() == false` sem `Start()` |
| `Start/Stop lifecycle` | Transições de estado, idempotência de `Stop()` |
| `Start twice does not crash` | `Start()` → `Start()` → `Stop()` |
| `Update is safe when not running` | `Update()` antes de `Start()` e após `Stop()` |
| `integration reload and callback` | Poll descobre mudança, `Update()` recarrega, callback dispara |

## Uso no Editor

```cpp
// Inicialização
m_HotReloader.Start(&m_AssetManager, "assets/processed/", 100);
m_HotReloader.SetNotificationCallback([](const HotReloadedAsset& a) {
    EditorToast::Show(ICON_FA_SYNC " {} recarregado", a.name);
});

// A cada frame (main thread)
m_HotReloader.Update();
```
