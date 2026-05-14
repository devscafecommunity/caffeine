#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Doppio — Caffeine Studio IDE Master Header
//
//  Inclui o core da engine + todos os headers do editor/IDE.
//  Apenas para uso no binário doppio — NÃO incluir em projetos
//  que usam apenas a Caffeine Engine (use Caffeine.hpp para esse fim).
// ═══════════════════════════════════════════════════════════════════

// ── Engine Core ─────────────────────────────────────────────────
#include "Caffeine.hpp"

// ── Editor panels (header-only, ImGui code is CF_HAS_IMGUI-guarded) ─
#include "editor/EditorTypes.hpp"
#include "editor/EditorContext.hpp"
#include "editor/ConsoleWindow.hpp"
#include "editor/ProfilerWindow.hpp"
#include "editor/StatsOverlay.hpp"
#include "editor/HierarchyPanel.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/SceneViewport.hpp"
#include "editor/AssetBrowser.hpp"
#include "editor/SceneEditor.hpp"

#ifdef CF_HAS_IMGUI
#include "editor/ImGuiIntegration.hpp"
#endif
