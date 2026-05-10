#pragma once

#include "core/Types.hpp"
#include "core/Platform.hpp"
#include "core/Compiler.hpp"
#include "core/Assertions.hpp"

#include "core/io/CafTypes.hpp"
#include "core/io/Crc32.hpp"
#include "core/io/BlobLoader.hpp"
#include "core/io/CafWriter.hpp"

#include "memory/Allocator.hpp"
#include "memory/LinearAllocator.hpp"
#include "memory/PoolAllocator.hpp"
#include "memory/StackAllocator.hpp"

#include "containers/Vector.hpp"
#include "containers/HashMap.hpp"
#include "containers/StringView.hpp"
#include "containers/FixedString.hpp"

#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include "math/Mat4.hpp"
#include "math/Math.hpp"
#include "math/Quat.hpp"

// ECS System
#include "ecs/World.hpp"
#include "ecs/ComponentID.hpp"
#include "ecs/ComponentSet.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Archetype.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/SystemRegistry.hpp"
#include "ecs/CommandBuffer.hpp"
#include "ecs/Components.hpp"

// Events
#include "events/EventBus.hpp"
#include "events/Events.hpp"

// Assets
#include "assets/AssetTypes.hpp"
#include "assets/AssetHandle.hpp"
#include "assets/AssetManager.hpp"

// Render
#include "render/Camera2D.hpp"
#include "render/Camera3D.hpp"
#include "render/TextureAtlas.hpp"
#ifdef CF_HAS_SDL3
#include "render/BatchRenderer.hpp"
#endif

// Scene
#include "scene/SceneComponents.hpp"
#include "scene/SceneSerializer.hpp"
#include "scene/SceneManager.hpp"

// Spatial
#include "spatial/Octree.hpp"

// Physics
#include "physics/PhysicsComponents2D.hpp"
#include "physics/PhysicsSystem2D.hpp"

// UI
#include "ui/UIComponents.hpp"
#include "ui/UISystem.hpp"

// Audio
#include "audio/AudioComponents.hpp"
#ifdef CF_HAS_SDL3
#include "audio/AudioSystem.hpp"
#endif

// Animation
#include "animation/AnimationComponents.hpp"
#include "animation/AnimationSystem.hpp"
#include "animation/SkeletalAnimation.hpp"

// Mesh (3D)
#include "ecs/Components3D.hpp"
#include "assets/MeshTypes.hpp"
#include "assets/MeshLoader.hpp"

// Editor (Dear ImGui)
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
#ifdef CF_HAS_SDL3
#ifdef CF_HAS_IMGUI
#include "editor/ImGuiIntegration.hpp"
#endif
#endif

// Tools (Asset Pipeline)
#include "tools/PipelineTypes.hpp"
#include "tools/TextureEncoder.hpp"
#include "tools/AudioEncoder.hpp"
#include "tools/MeshEncoder.hpp"
#include "tools/AssetManifest.hpp"
#include "tools/AssetPipeline.hpp"