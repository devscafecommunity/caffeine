#include "editor/AnimatorController.hpp"
#include "animation/AnimationSystem.hpp"
#include "animation/SkinLibrary.hpp"
#include "editor/AnimationEditorLink.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef CF_HAS_IMGUI

#include "editor/CurveWidget.hpp"

namespace Caffeine::Editor {

using namespace Animation;

static const ImU32 k_ColCanvas       = IM_COL32(36,  38,  42,  255);
static const ImU32 k_ColGrid         = IM_COL32(46,  48,  53,  255);
static const ImU32 k_ColGridMajor    = IM_COL32(58,  60,  66,  255);
static const ImU32 k_ColStateNormal  = IM_COL32(78,  82,  88,  255);
static const ImU32 k_ColStateDefault = IM_COL32(196, 112,  42,  255);
static const ImU32 k_ColEntry        = IM_COL32(42,  138,  70,  255);
static const ImU32 k_ColExit         = IM_COL32(168,  52,  52,  255);
static const ImU32 k_ColAny          = IM_COL32(42,  128, 126,  255);
static const ImU32 k_ColBorder       = IM_COL32(20,  20,  22,  255);
static const ImU32 k_ColSelected     = IM_COL32(90,  160, 255, 255);
static const ImU32 k_ColText         = IM_COL32(236, 238, 242, 255);
static const ImU32 k_ColArrow        = IM_COL32(214, 218, 224, 230);
static const ImU32 k_ColArrowHover   = IM_COL32(255, 255, 255, 255);
static const ImU32 k_ColProgress     = IM_COL32(80,  150, 255, 255);
static const ImU32 k_ColTimeline     = IM_COL32(110, 220, 140, 255);

static const char* k_EntryName = "Entry";
static const char* k_AnyName = "Any State";
static const char* k_ExitName = kAnimatorExitState;
static const char* k_LayerPayload = "CAF_ANIM_LAYER";

static bool reservedNodeName(const std::string& name) {
    return name == k_EntryName || name == k_AnyName || name == k_ExitName;
}

static f32 distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const f32 dx = b.x - a.x;
    const f32 dy = b.y - a.y;
    const f32 len2 = dx * dx + dy * dy;
    f32 t = len2 > 0.0001f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const f32 px = a.x + dx * t - p.x;
    const f32 py = a.y + dy * t - p.y;
    return std::sqrt(px * px + py * py);
}

void AnimatorControllerWindow::bindSelection(ECS::World* world, ECS::Entity entity,
                                             const std::string& projectRoot) {
    Animator* animator = nullptr;
    if (world && entity.isValid()) {
        if (SkinnedPose* pose = ensurePoseForMesh(*world, entity, projectRoot)) {
            if (const ImportedSkin* skin = findImportedSkin(pose->meshPath)) {
                if (!skin->clipNames.empty() && !world->has<Animator>(entity)) {
                    world->add<Animator>(entity);
                    if (Animator* created = world->get<Animator>(entity)) {
                        for (const std::string& clip : skin->clipNames) {
                            if (clip.empty()) continue;
                            AnimationState state;
                            state.name = clip.c_str();
                            state.motion = clip.c_str();
                            created->states.set(state.name, state);
                            if (created->defaultState.empty()) created->defaultState = state.name;
                        }
                    }
                }
            }
        }
        animator = world->get<Animator>(entity);
    }
    if (animator == m_bound && world == m_world && entity == m_entity) return;
    if (m_preview) stopPreview();
    m_world = world;
    m_entity = entity;
    m_bound = animator;
    setAnimator(animator);
}

void AnimatorControllerWindow::setAnimator(Animator* animator) {
    m_animator = animator ? animator : &m_internalAnimator;
    m_layer = -1;
    m_selection = Selection::None;
    m_selectedState.clear();
    m_selectedTransitionFrom.clear();
    m_selectedTransition = -1;
    m_dragNode.clear();
    m_linkFrom.clear();
    m_linkDrag = false;
    m_status.clear();
    m_renameTarget.clear();
    m_layerNameTarget = -2;
    if (m_animator->defaultState.empty() && !m_animator->states.empty()) {
        m_animator->defaultState = m_animator->states.begin()->key;
    }
    layoutMissingNodes();
}

AnimatorStateMachine& AnimatorControllerWindow::graph() {
    if (m_layer >= 0 && m_layer < static_cast<int>(m_animator->layers.size()) &&
        !m_animator->layers[static_cast<size_t>(m_layer)].isGroup) {
        return m_animator->layers[static_cast<size_t>(m_layer)];
    }
    return *m_animator;
}

bool AnimatorControllerWindow::editingGroup() const {
    return m_layer >= 0 && m_layer < static_cast<int>(m_animator->layers.size()) &&
           m_animator->layers[static_cast<size_t>(m_layer)].isGroup;
}

std::vector<AnimationTransition>* AnimatorControllerWindow::transitionList(const std::string& from) {
    AnimatorStateMachine& sm = graph();
    if (from == k_AnyName) return &sm.anyStateTransitions;
    AnimationState* state = sm.states.get(FixedString<32>(from.c_str()));
    return state ? &state->transitions : nullptr;
}

AnimationTransition* AnimatorControllerWindow::selectedTransition() {
    std::vector<AnimationTransition>* list = transitionList(m_selectedTransitionFrom);
    if (!list || m_selectedTransition < 0 || m_selectedTransition >= static_cast<int>(list->size())) return nullptr;
    return &(*list)[static_cast<size_t>(m_selectedTransition)];
}

bool AnimatorControllerWindow::timelineLinked(const std::string& from, const std::string& to) const {
    if (m_layer != -1 || !m_world || !m_entity.isValid() || from == k_AnyName) return false;
    const SkinnedPose* pose = m_world->get<SkinnedPose>(m_entity);
    if (!pose) return false;
    const ImportedSkin* skin = findImportedSkin(pose->meshPath);
    const std::vector<size_t> order = baseTrackOrder(*pose);
    for (size_t i = 1; i < order.size(); ++i) {
        if (stripStateName(pose->strips[order[i - 1]], m_animator, skin) == from &&
            stripStateName(pose->strips[order[i]], m_animator, skin) == to) {
            return true;
        }
    }
    return false;
}

void AnimatorControllerWindow::transitionEdited(const std::string& from, const AnimationTransition& transition) {
    if (m_layer != -1 || from == k_AnyName || !m_world || !m_entity.isValid()) return;
    SkinnedPose* pose = m_world->get<SkinnedPose>(m_entity);
    if (!pose) return;
    pullTransitionToStrips(*pose, findImportedSkin(pose->meshPath), *m_animator, from, transition.toState.cStr(), transition);
}

std::vector<std::string> AnimatorControllerWindow::clipNames() const {
    if (!m_world || !m_entity.isValid()) return {};
    const SkinnedPose* pose = m_world->get<SkinnedPose>(m_entity);
    const ImportedSkin* skin = pose ? findImportedSkin(pose->meshPath) : nullptr;
    return skin ? skin->clipNames : std::vector<std::string>{};
}

f32 AnimatorControllerWindow::stateDuration(const AnimationState& state) const {
    if (m_world && m_entity.isValid()) {
        if (const SkinnedPose* pose = m_world->get<SkinnedPose>(m_entity)) {
            if (const ImportedSkin* skin = findImportedSkin(pose->meshPath)) {
                const i32 clip = clipForAnimatorState(*skin, state.name.cStr(), state.motion.cStr());
                if (clip >= 0 && clip < static_cast<i32>(skin->clips.size())) {
                    return skin->clips[static_cast<size_t>(clip)].duration;
                }
            }
        }
    }
    return state.clip ? state.clip->duration() : 0.0f;
}

f32 AnimatorControllerWindow::graphStateDuration(const AnimatorStateMachine& sm, const FixedString<32>& name) const {
    const AnimationState* state = sm.states.get(name);
    return state ? stateDuration(*state) : 0.0f;
}

void AnimatorControllerWindow::layoutMissingNodes() {
    AnimatorStateMachine& sm = graph();
    int slot = 0;
    for (auto& pair : sm.states) {
        AnimationState& state = pair.value;
        if (state.hasEditorPosition) continue;
        for (;; ++slot) {
            const f32 x = 240.0f + static_cast<f32>(slot % 3) * 200.0f;
            const f32 y = 40.0f + static_cast<f32>(slot / 3) * 80.0f;
            bool taken = false;
            for (const auto& other : sm.states) {
                if (other.value.hasEditorPosition && std::fabs(other.value.editorX - x) < 60.0f &&
                    std::fabs(other.value.editorY - y) < 40.0f) {
                    taken = true;
                    break;
                }
            }
            if (taken) continue;
            state.editorX = x;
            state.editorY = y;
            state.hasEditorPosition = true;
            ++slot;
            break;
        }
    }
}

std::string AnimatorControllerWindow::uniqueStateName(const char* base) const {
    AnimatorStateMachine& sm = const_cast<AnimatorControllerWindow*>(this)->graph();
    std::string name = reservedNodeName(base) ? std::string(base) + " 1" : std::string(base);
    for (int suffix = 1; sm.states.contains(FixedString<32>(name.c_str())) || reservedNodeName(name); ++suffix) {
        name = std::string(base) + " " + std::to_string(suffix);
    }
    return name;
}

std::string AnimatorControllerWindow::uniqueLayerName(const char* base) const {
    auto taken = [&](const std::string& name) {
        for (const AnimatorLayer& layer : m_animator->layers) {
            if (name == layer.name.cStr()) return true;
        }
        return name == "Base Layer";
    };
    std::string name = base;
    for (int suffix = 1; taken(name); ++suffix) name = std::string(base) + " " + std::to_string(suffix);
    return name;
}

void AnimatorControllerWindow::selectState(const std::string& name) {
    m_selection = Selection::State;
    m_selectedState = name;
    m_selectedTransition = -1;
    if (m_layer == -1) {
        AnimationEditorLink& link = AnimationEditorLink::get();
        link.selectedState = name;
        ++link.selectionSerial;
    }
}

void AnimatorControllerWindow::createState(const char* name, const char* motion, f32 x, f32 y) {
    AnimatorStateMachine& sm = graph();
    AnimationState state;
    state.name = name;
    state.motion = motion ? motion : "";
    state.editorX = x;
    state.editorY = y;
    state.hasEditorPosition = true;
    sm.states.set(state.name, state);
    if (sm.defaultState.empty()) sm.defaultState = state.name;
    selectState(name);
}

void AnimatorControllerWindow::renameState(const std::string& from, const std::string& to) {
    if (to.empty() || from == to) return;
    if (reservedNodeName(to)) {
        m_status = to + " is reserved for a graph node.";
        return;
    }
    AnimatorStateMachine& sm = graph();
    const FixedString<32> oldName(from.c_str());
    const FixedString<32> newName(to.c_str());
    if (sm.states.contains(newName)) {
        m_status = "A state named " + to + " already exists.";
        return;
    }
    const AnimationState* existing = sm.states.get(oldName);
    if (!existing) return;
    AnimationState copy = *existing;
    copy.name = newName;
    if (copy.motion.empty()) copy.motion = oldName;
    sm.states.remove(oldName);
    sm.states.set(newName, copy);
    for (auto& pair : sm.states) {
        for (AnimationTransition& transition : pair.value.transitions) {
            if (transition.toState == oldName) transition.toState = newName;
        }
    }
    for (AnimationTransition& transition : sm.anyStateTransitions) {
        if (transition.toState == oldName) transition.toState = newName;
    }
    if (sm.defaultState == oldName) sm.defaultState = newName;
    if (sm.currentState == oldName) sm.currentState = newName;
    if (sm.previousState == oldName) sm.previousState = newName;
    if (sm.fadeFromState == oldName) sm.fadeFromState = newName;
    if (m_layer == -1 && m_world && m_entity.isValid()) {
        if (SkinnedPose* pose = m_world->get<SkinnedPose>(m_entity)) {
            for (ClipStrip& strip : pose->strips) {
                if (from == strip.state) std::snprintf(strip.state, sizeof(strip.state), "%s", to.c_str());
            }
        }
    }
    if (m_selectedState == from) m_selectedState = to;
    if (m_selectedTransitionFrom == from) m_selectedTransitionFrom = to;
    m_renameTarget.clear();
}

void AnimatorControllerWindow::deleteState(const std::string& name) {
    AnimatorStateMachine& sm = graph();
    const FixedString<32> key(name.c_str());
    sm.states.remove(key);
    auto dropTarget = [&](std::vector<AnimationTransition>& transitions) {
        transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
                                         [&](const AnimationTransition& t) { return t.toState == key; }),
                          transitions.end());
    };
    for (auto& pair : sm.states) dropTarget(pair.value.transitions);
    dropTarget(sm.anyStateTransitions);
    if (sm.defaultState == key) {
        sm.defaultState = sm.states.empty() ? FixedString<32>() : sm.states.begin()->key;
    }
    if (sm.currentState == key) sm.currentState = FixedString<32>();
    if (sm.fadeFromState == key) sm.fadeDuration = 0.0f;
    m_selection = Selection::None;
    m_selectedState.clear();
    m_selectedTransition = -1;
}

void AnimatorControllerWindow::addTransition(const std::string& from, const std::string& to) {
    if (from == to) return;
    if (to == k_EntryName || to == k_AnyName) {
        m_status = "Transitions cannot end on Entry or Any State.";
        return;
    }
    AnimatorStateMachine& sm = graph();
    if (from == k_EntryName) {
        if (to == k_ExitName) return;
        sm.defaultState = to.c_str();
        m_selection = Selection::Entry;
        return;
    }
    if (from == k_ExitName) {
        m_status = "Exit has no outgoing transitions. It returns to Entry.";
        return;
    }
    if (from == k_AnyName && to == k_ExitName) {
        m_status = "Any State cannot lead to Exit.";
        return;
    }
    std::vector<AnimationTransition>* list = transitionList(from);
    if (!list) return;
    AnimationTransition transition;
    transition.toState = to.c_str();
    transition.blendTime = 0.25f;
    if (from == k_AnyName) {
        transition.hasExitTime = false;
        m_status = "Any State transitions fire on their conditions. Add one in the inspector.";
    } else {
        transition.hasExitTime = true;
        const f32 duration = graphStateDuration(sm, FixedString<32>(from.c_str()));
        transition.exitTime = duration > 0.0f ? std::max(0.0f, 1.0f - transition.blendTime / duration) : 0.75f;
    }
    list->push_back(transition);
    m_selection = Selection::Transition;
    m_selectedTransitionFrom = from;
    m_selectedTransition = static_cast<int>(list->size()) - 1;
    transitionEdited(from, transition);
}

void AnimatorControllerWindow::playState(const std::string& name) {
    AnimatorStateMachine& sm = graph();
    const FixedString<32> key(name.c_str());
    if (!sm.states.contains(key)) return;
    if (!m_preview) startPreview();
    if (!m_preview) return;
    AnimationTransition transition;
    transition.toState = key;
    transition.blendTime = (sm.currentState.empty() || sm.currentState == key) ? 0.0f : 0.2f;
    AnimationSystem::enterState(sm, transition, [this](const AnimatorStateMachine& graphRef, const FixedString<32>& state) {
        return graphStateDuration(graphRef, state);
    });
}

static void resetGraphPlayback(AnimatorStateMachine& sm) {
    sm.currentState = FixedString<32>();
    sm.previousState = FixedString<32>();
    sm.fadeFromState = FixedString<32>();
    sm.fadeDuration = 0.0f;
    sm.fadeElapsed = 0.0f;
    sm.timeInState = 0.0f;
}

void AnimatorControllerWindow::startPreview() {
    if (!m_bound || m_animator != m_bound) return;
    resetGraphPlayback(*m_animator);
    for (AnimatorLayer& layer : m_animator->layers) resetGraphPlayback(layer);
    m_preview = true;
    runPreview();
}

void AnimatorControllerWindow::stopPreview() {
    m_preview = false;
    if (m_bound && m_animator == m_bound) {
        resetGraphPlayback(*m_animator);
        for (AnimatorLayer& layer : m_animator->layers) resetGraphPlayback(layer);
    }
    AnimationEditorLink::get().animatorPreview = false;
}

void AnimatorControllerWindow::runPreview() {
    if (!m_preview || !m_bound || m_animator != m_bound) return;
    AnimationSystem::stepStateMachine(*m_animator, ImGui::GetIO().DeltaTime,
                                      [this](const AnimatorStateMachine& sm, const FixedString<32>& name) {
                                          return graphStateDuration(sm, name);
                                      });
}

void AnimatorControllerWindow::publishLink() {
    AnimationEditorLink& link = AnimationEditorLink::get();
    if (!m_preview || !m_bound || !m_entity.isValid()) {
        link.animatorPreview = false;
        return;
    }
    link.animatorPreview = true;
    link.entity = m_entity.id();
    link.previewState = m_animator->currentState.cStr();
    link.previewTime = m_animator->timeInState;
}

void AnimatorControllerWindow::consumeLink() {
    AnimationEditorLink& link = AnimationEditorLink::get();
    if (link.timelinePlaying && !m_preview && m_entity.isValid() && link.entity == m_entity.id() &&
        !link.timelineState.empty() && m_selection != Selection::Transition) {
        m_layer = -1;
        m_selection = Selection::State;
        m_selectedState = link.timelineState;
    }
    if (link.focusSerial == m_focusSeen) return;
    m_focusSeen = link.focusSerial;
    const AnimationState* state = m_animator->states.get(FixedString<32>(link.focusState.c_str()));
    if (!state) return;
    m_layer = -1;
    m_selection = Selection::State;
    m_selectedState = link.focusState;
    m_selectedTransition = -1;
    const ImVec2 node = stateNodeSize();
    const f32 x = state->editorX + m_canvasScrollX;
    const f32 y = state->editorY + m_canvasScrollY;
    if (m_canvasSize.x > 0.0f && (x < 0.0f || y < 0.0f || x + node.x > m_canvasSize.x || y + node.y > m_canvasSize.y)) {
        m_canvasScrollX = m_canvasSize.x * 0.5f - (state->editorX + node.x * 0.5f);
        m_canvasScrollY = m_canvasSize.y * 0.5f - (state->editorY + node.y * 0.5f);
    }
}

void AnimatorControllerWindow::render() {
    if (!m_open) {
        if (m_preview) stopPreview();
        return;
    }
    if (m_layer >= static_cast<int>(m_animator->layers.size())) m_layer = -1;
    consumeLink();
    runPreview();
    publishLink();

    ImGui::SetNextWindowSizeConstraints({560, 360}, {FLT_MAX, FLT_MAX});
    if (!ImGui::Begin("Animator Controller", &m_open)) {
        ImGui::End();
        return;
    }

    renderToolbar();

    const float sideWidth = 230.0f;
    const float inspectorWidth = 280.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float canvasW = std::max(180.0f, availW - sideWidth - inspectorWidth - 12.0f);

    ImGui::BeginChild("##anim_params_col", {sideWidth, 0.0f}, true);
    if (ImGui::BeginTabBar("##anim_side")) {
        if (ImGui::BeginTabItem("Layers")) {
            renderLayersPanel();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Parameters")) {
            renderParameterPanel();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##anim_canvas_col", {canvasW, 0.0f}, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (editingGroup()) {
        ImGui::Spacing();
        ImGui::TextDisabled("  This is a layer group. It scales the weight of the layers inside it.");
        ImGui::TextDisabled("  Select a layer in the group to edit its graph.");
    } else {
        renderCanvas();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##anim_inspector_col", {inspectorWidth, 0.0f}, true);
    renderInspector();
    ImGui::EndChild();

    ImGui::End();
}

void AnimatorControllerWindow::renderToolbar() {
    const AnimationEditorLink& link = AnimationEditorLink::get();
    if (m_bound) {
        bool preview = m_preview;
        if (ImGui::Checkbox("Live Preview", &preview)) {
            if (preview) startPreview();
            else stopPreview();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Runs every layer of the state machine in the editor. Change parameters to fire transitions.\n"
                              "The Animation Timeline follows the current state while this is on.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart")) startPreview();
        ImGui::SameLine();
        const AnimatorStateMachine& sm = graph();
        const std::string layerName = m_layer < 0 ? std::string("Base Layer")
                                                  : std::string(m_animator->layers[static_cast<size_t>(m_layer)].name.cStr());
        ImGui::TextDisabled("%s   Current: %s   %.2fs", layerName.c_str(), sm.currentState.empty() ? "-" : sm.currentState.cStr(),
                            sm.timeInState);
        if (sm.fading()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.45f, 0.65f, 1.0f, 1.0f), "blending from %s  %.0f%%", sm.fadeFromState.cStr(),
                               100.0f * sm.fadeElapsed / std::max(sm.fadeDuration, 0.0001f));
        }
        if (!m_preview && m_entity.isValid() && link.entity == m_entity.id() && link.timelineActive(skinFrameCounter())) {
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::TextColored(ImVec4(0.45f, 0.86f, 0.55f, 1.0f), "Timeline: %s", link.timelineState.c_str());
        }
    } else {
        ImGui::TextDisabled("Select a mesh with a skeleton, or add an Animator component.");
    }
    if (!m_status.empty()) {
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.4f, 1.0f), "%s", m_status.c_str());
    }
    ImGui::TextDisabled("Right-click the grid to create states. Drag the dot on a node's right edge to link it. "
                        "Double-click a state to play it.");
}

void AnimatorControllerWindow::renderCanvas() {
    AnimatorStateMachine& sm = graph();
    const AnimationEditorLink& link = AnimationEditorLink::get();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.x = std::max(canvasSize.x, 50.0f);
    canvasSize.y = std::max(canvasSize.y, 50.0f);
    m_canvasSize = canvasSize;

    ImGui::InvisibleButton("##canvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool clickedLeft = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool clickedRight = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const ImVec2 canvasMax(origin.x + canvasSize.x, origin.y + canvasSize.y);

    dl->PushClipRect(origin, canvasMax, true);
    dl->AddRectFilled(origin, canvasMax, k_ColCanvas);
    const f32 gridStep = 22.0f;
    {
        const int firstX = static_cast<int>(std::floor(-m_canvasScrollX / gridStep));
        for (int i = firstX;; ++i) {
            const f32 x = origin.x + m_canvasScrollX + static_cast<f32>(i) * gridStep;
            if (x > canvasMax.x) break;
            dl->AddLine({x, origin.y}, {x, canvasMax.y}, (i % 5 == 0) ? k_ColGridMajor : k_ColGrid);
        }
        const int firstY = static_cast<int>(std::floor(-m_canvasScrollY / gridStep));
        for (int i = firstY;; ++i) {
            const f32 y = origin.y + m_canvasScrollY + static_cast<f32>(i) * gridStep;
            if (y > canvasMax.y) break;
            dl->AddLine({origin.x, y}, {canvasMax.x, y}, (i % 5 == 0) ? k_ColGridMajor : k_ColGrid);
        }
    }

    const bool timelineLive = m_layer == -1 && !m_preview && m_entity.isValid() && link.entity == m_entity.id() &&
                              link.timelineActive(skinFrameCounter());
    std::vector<std::string> timelineStates;
    if (m_layer == -1 && m_world && m_entity.isValid()) {
        if (const SkinnedPose* pose = m_world->get<SkinnedPose>(m_entity)) {
            const ImportedSkin* skin = findImportedSkin(pose->meshPath);
            for (const ClipStrip& strip : pose->strips) {
                const std::string name = stripStateName(strip, m_animator, skin);
                if (!name.empty() && std::find(timelineStates.begin(), timelineStates.end(), name) == timelineStates.end()) {
                    timelineStates.push_back(name);
                }
            }
        }
    }

    const ImVec2 nodeSize = stateNodeSize();
    auto toScreen = [&](f32 x, f32 y) { return ImVec2(origin.x + x + m_canvasScrollX, origin.y + y + m_canvasScrollY); };

    struct NodeInfo {
        std::string name;
        ImVec2 min;
        ImVec2 max;
        Selection kind;
    };
    std::vector<NodeInfo> nodes;
    auto addNode = [&](const std::string& name, f32 x, f32 y, Selection kind) {
        const ImVec2 min = toScreen(x, y);
        nodes.push_back({name, min, ImVec2(min.x + nodeSize.x, min.y + nodeSize.y), kind});
    };
    addNode(k_AnyName, sm.anyX, sm.anyY, Selection::AnyState);
    addNode(k_EntryName, sm.entryX, sm.entryY, Selection::Entry);
    addNode(k_ExitName, sm.exitX, sm.exitY, Selection::Exit);
    for (auto& pair : sm.states) {
        addNode(pair.key.cStr(), pair.value.editorX, pair.value.editorY, Selection::State);
    }
    auto findNode = [&](const std::string& name) -> const NodeInfo* {
        for (const NodeInfo& node : nodes) {
            if (node.name == name) return &node;
        }
        return nullptr;
    };
    auto nodeAt = [&](ImVec2 p) -> const NodeInfo* {
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            if (p.x >= it->min.x && p.x <= it->max.x && p.y >= it->min.y && p.y <= it->max.y) return &*it;
        }
        return nullptr;
    };
    auto centerOf = [](const NodeInfo& node) {
        return ImVec2((node.min.x + node.max.x) * 0.5f, (node.min.y + node.max.y) * 0.5f);
    };
    auto edgePoint = [&](const NodeInfo& node, ImVec2 toward) {
        const ImVec2 center = centerOf(node);
        const f32 dx = toward.x - center.x;
        const f32 dy = toward.y - center.y;
        const f32 hx = (node.max.x - node.min.x) * 0.5f;
        const f32 hy = (node.max.y - node.min.y) * 0.5f;
        const f32 sx = std::fabs(dx) < 0.001f ? 1.0e9f : hx / std::fabs(dx);
        const f32 sy = std::fabs(dy) < 0.001f ? 1.0e9f : hy / std::fabs(dy);
        const f32 scale = std::min(sx, sy);
        return ImVec2(center.x + dx * scale, center.y + dy * scale);
    };
    auto portOf = [](const NodeInfo& node) { return ImVec2(node.max.x, (node.min.y + node.max.y) * 0.5f); };

    struct Edge {
        std::string from;
        std::string to;
        int index = -1;
        ImVec2 a;
        ImVec2 b;
        bool entry = false;
        const AnimationTransition* transition = nullptr;
    };
    std::vector<Edge> edges;
    if (!sm.defaultState.empty()) {
        const NodeInfo* entry = findNode(k_EntryName);
        const NodeInfo* target = findNode(sm.defaultState.cStr());
        if (entry && target) {
            edges.push_back({k_EntryName, sm.defaultState.cStr(), -1, edgePoint(*entry, centerOf(*target)),
                             edgePoint(*target, centerOf(*entry)), true, nullptr});
        }
    }
    auto hasReverse = [&](const std::string& from, const std::string& to) {
        const AnimationState* back = sm.states.get(FixedString<32>(to.c_str()));
        if (!back) return false;
        for (const AnimationTransition& t : back->transitions) {
            if (from == t.toState.cStr()) return true;
        }
        return false;
    };
    auto addEdges = [&](const std::string& fromName, const std::vector<AnimationTransition>& transitions) {
        const NodeInfo* from = findNode(fromName);
        if (!from) return;
        for (int i = 0; i < static_cast<int>(transitions.size()); ++i) {
            const AnimationTransition& transition = transitions[static_cast<size_t>(i)];
            const NodeInfo* to = findNode(transition.toState.cStr());
            if (!to || to == from) continue;
            ImVec2 a = edgePoint(*from, centerOf(*to));
            ImVec2 b = edgePoint(*to, centerOf(*from));
            if (hasReverse(fromName, transition.toState.cStr())) {
                const f32 dx = b.x - a.x;
                const f32 dy = b.y - a.y;
                const f32 len = std::max(std::sqrt(dx * dx + dy * dy), 0.001f);
                const ImVec2 offset(-dy / len * 6.0f, dx / len * 6.0f);
                a = ImVec2(a.x + offset.x, a.y + offset.y);
                b = ImVec2(b.x + offset.x, b.y + offset.y);
            }
            edges.push_back({fromName, transition.toState.cStr(), i, a, b, false, &transition});
        }
    };
    addEdges(k_AnyName, sm.anyStateTransitions);
    for (auto& pair : sm.states) addEdges(pair.key.cStr(), pair.value.transitions);

    auto edgeAt = [&](ImVec2 p) -> int {
        int best = -1;
        f32 bestDistance = 6.0f;
        for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
            const f32 d = distanceToSegment(p, edges[static_cast<size_t>(i)].a, edges[static_cast<size_t>(i)].b);
            if (d < bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        return best;
    };

    const ImVec2 mouse = io.MousePos;
    const NodeInfo* hoveredNode = hovered ? nodeAt(mouse) : nullptr;
    const int hoveredEdge = (hovered && !hoveredNode) ? edgeAt(mouse) : -1;
    bool onPort = false;
    if (hoveredNode && hoveredNode->kind != Selection::Exit) {
        const ImVec2 port = portOf(*hoveredNode);
        const f32 dx = mouse.x - port.x;
        const f32 dy = mouse.y - port.y;
        onPort = dx * dx + dy * dy < 100.0f;
    }

    auto selectNode = [&](const NodeInfo& node) {
        if (node.kind == Selection::State) {
            selectState(node.name);
            return;
        }
        m_selection = node.kind;
        m_selectedState.clear();
        m_selectedTransition = -1;
    };
    auto selectEdge = [&](const Edge& edge) {
        if (edge.entry) {
            m_selection = Selection::Entry;
            return;
        }
        m_selection = Selection::Transition;
        m_selectedTransitionFrom = edge.from;
        m_selectedTransition = edge.index;
        if (m_layer == -1 && edge.from != k_AnyName) {
            AnimationEditorLink& mutableLink = AnimationEditorLink::get();
            mutableLink.selectedFrom = edge.from;
            mutableLink.selectedTo = edge.to;
            ++mutableLink.transitionSerial;
        }
    };

    if (clickedLeft) {
        m_status.clear();
        if (!m_linkFrom.empty() && !m_linkDrag) {
            if (hoveredNode) addTransition(m_linkFrom, hoveredNode->name);
            m_linkFrom.clear();
        } else if (onPort) {
            m_linkFrom = hoveredNode->name;
            m_linkDrag = true;
        } else if (hoveredNode) {
            selectNode(*hoveredNode);
            m_dragNode = hoveredNode->name;
        } else if (hoveredEdge >= 0) {
            selectEdge(edges[static_cast<size_t>(hoveredEdge)]);
        } else {
            m_selection = Selection::None;
            m_selectedState.clear();
            m_panning = true;
        }
    }
    if (hoveredNode && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoveredNode->kind == Selection::State) {
        playState(hoveredNode->name);
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        if (!m_dragNode.empty()) {
            if (m_dragNode == k_EntryName) {
                sm.entryX += io.MouseDelta.x;
                sm.entryY += io.MouseDelta.y;
            } else if (m_dragNode == k_AnyName) {
                sm.anyX += io.MouseDelta.x;
                sm.anyY += io.MouseDelta.y;
            } else if (m_dragNode == k_ExitName) {
                sm.exitX += io.MouseDelta.x;
                sm.exitY += io.MouseDelta.y;
            } else if (AnimationState* state = sm.states.get(FixedString<32>(m_dragNode.c_str()))) {
                state->editorX += io.MouseDelta.x;
                state->editorY += io.MouseDelta.y;
            }
        } else if (m_panning) {
            m_canvasScrollX += io.MouseDelta.x;
            m_canvasScrollY += io.MouseDelta.y;
        }
    }
    if ((hovered || active) && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m_canvasScrollX += io.MouseDelta.x;
        m_canvasScrollY += io.MouseDelta.y;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (m_linkDrag) {
            const NodeInfo* target = nodeAt(mouse);
            if (target && target->name != m_linkFrom) addTransition(m_linkFrom, target->name);
            m_linkFrom.clear();
            m_linkDrag = false;
        }
        m_dragNode.clear();
        m_panning = false;
    }
    if (clickedRight) {
        m_linkFrom.clear();
        m_linkDrag = false;
        if (hoveredNode) {
            selectNode(*hoveredNode);
            m_contextNode = hoveredNode->name;
            ImGui::OpenPopup("##node_ctx");
        } else if (hoveredEdge >= 0 && !edges[static_cast<size_t>(hoveredEdge)].entry) {
            const Edge& edge = edges[static_cast<size_t>(hoveredEdge)];
            selectEdge(edge);
            m_contextTransitionFrom = edge.from;
            m_contextTransition = edge.index;
            ImGui::OpenPopup("##edge_ctx");
        } else {
            m_contextPos = {mouse.x - origin.x - m_canvasScrollX - nodeSize.x * 0.5f,
                            mouse.y - origin.y - m_canvasScrollY - nodeSize.y * 0.5f};
            ImGui::OpenPopup("##canvas_ctx");
        }
    }
    if ((hovered || ImGui::IsWindowFocused()) && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_linkFrom.clear();
            m_linkDrag = false;
            m_status.clear();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (m_selection == Selection::State && !m_selectedState.empty()) {
                deleteState(m_selectedState);
            } else if (m_selection == Selection::Transition) {
                std::vector<AnimationTransition>* list = transitionList(m_selectedTransitionFrom);
                if (list && m_selectedTransition >= 0 && m_selectedTransition < static_cast<int>(list->size())) {
                    list->erase(list->begin() + m_selectedTransition);
                }
                m_selection = Selection::None;
            }
        }
    }

    auto drawArrow = [&](ImVec2 a, ImVec2 b, ImU32 color, f32 thickness) {
        dl->AddLine(a, b, color, thickness);
        const f32 dx = b.x - a.x;
        const f32 dy = b.y - a.y;
        const f32 len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) return;
        const ImVec2 dir(dx / len, dy / len);
        const ImVec2 mid((a.x + b.x) * 0.5f + dir.x * 5.0f, (a.y + b.y) * 0.5f + dir.y * 5.0f);
        const ImVec2 back(mid.x - dir.x * 10.0f, mid.y - dir.y * 10.0f);
        const ImVec2 perp(-dir.y * 5.5f, dir.x * 5.5f);
        dl->AddTriangleFilled(mid, ImVec2(back.x + perp.x, back.y + perp.y), ImVec2(back.x - perp.x, back.y - perp.y), color);
    };
    for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
        const Edge& edge = edges[static_cast<size_t>(i)];
        bool selected = false;
        if (edge.entry) selected = m_selection == Selection::Entry;
        else selected = m_selection == Selection::Transition && m_selectedTransitionFrom == edge.from && m_selectedTransition == edge.index;
        ImU32 color = edge.entry ? IM_COL32(230, 160, 90, 230)
                      : edge.from == k_AnyName ? IM_COL32(110, 210, 205, 230)
                      : edge.to == k_ExitName  ? IM_COL32(230, 130, 130, 230)
                                               : k_ColArrow;
        if (i == hoveredEdge) color = k_ColArrowHover;
        if (selected) color = k_ColSelected;

        f32 activeFade = -1.0f;
        ImU32 activeColor = k_ColProgress;
        if (!edge.entry && m_preview && sm.fading() && edge.to == sm.currentState.cStr() &&
            (edge.from == sm.fadeFromState.cStr() || edge.from == k_AnyName)) {
            activeFade = sm.fadeElapsed / std::max(sm.fadeDuration, 0.0001f);
        }
        if (!edge.entry && timelineLive && !link.timelineFadeFrom.empty() && edge.from == link.timelineFadeFrom &&
            edge.to == link.timelineState) {
            activeFade = link.timelineFade;
            activeColor = k_ColTimeline;
        }
        if (activeFade >= 0.0f) {
            drawArrow(edge.a, edge.b, activeColor, 3.5f);
            const f32 u = std::clamp(activeFade, 0.0f, 1.0f);
            dl->AddCircleFilled(ImVec2(edge.a.x + (edge.b.x - edge.a.x) * u, edge.a.y + (edge.b.y - edge.a.y) * u), 5.0f,
                                IM_COL32(255, 255, 255, 255), 12);
        } else {
            drawArrow(edge.a, edge.b, color, selected ? 2.5f : 1.6f);
        }

        if (edge.transition && edge.transition->blendTime > 0.0f) {
            const bool linked = timelineLinked(edge.from, edge.to);
            const f32 dx = edge.b.x - edge.a.x;
            const f32 dy = edge.b.y - edge.a.y;
            const f32 len = std::max(std::sqrt(dx * dx + dy * dy), 0.001f);
            if (len > 90.0f) {
                char text[16];
                std::snprintf(text, sizeof(text), "%.2fs", edge.transition->blendTime);
                const ImVec2 textSize = ImGui::CalcTextSize(text);
                const ImVec2 mid((edge.a.x + edge.b.x) * 0.5f - dy / len * 14.0f, (edge.a.y + edge.b.y) * 0.5f + dx / len * 14.0f);
                const ImVec2 boxMin(mid.x - (textSize.x + 24.0f) * 0.5f, mid.y - 8.0f);
                const ImVec2 boxMax(boxMin.x + textSize.x + 24.0f, mid.y + 8.0f);
                dl->AddRectFilled(boxMin, boxMax, linked ? IM_COL32(30, 70, 44, 230) : IM_COL32(28, 30, 36, 220), 3.0f);
                if (linked) dl->AddRect(boxMin, boxMax, k_ColTimeline, 3.0f);
                drawCurveGlyph(dl, ImVec2(boxMin.x + 4.0f, boxMin.y + 3.0f), ImVec2(boxMin.x + 18.0f, boxMax.y - 3.0f),
                               edge.transition->blendCurve, linked ? k_ColTimeline : IM_COL32(255, 210, 60, 255), 1.2f);
                dl->AddText(ImVec2(boxMin.x + 21.0f, boxMin.y + 1.0f), IM_COL32(220, 224, 232, 255), text);
                if (hovered && mouse.x >= boxMin.x && mouse.x <= boxMax.x && mouse.y >= boxMin.y && mouse.y <= boxMax.y) {
                    ImGui::SetTooltip("%s > %s\nBlend %.2fs, %s%s", edge.from.c_str(), edge.to.c_str(), edge.transition->blendTime,
                                      keyInterpName(edge.transition->blendCurve.mode),
                                      linked ? "\nSynced with the Animation Timeline crossfade." : "");
                }
            }
        }
    }

    for (const NodeInfo& node : nodes) {
        ImU32 fill = k_ColStateNormal;
        if (node.kind == Selection::Entry) fill = k_ColEntry;
        else if (node.kind == Selection::AnyState) fill = k_ColAny;
        else if (node.kind == Selection::Exit) fill = k_ColExit;
        else if (sm.defaultState == FixedString<32>(node.name.c_str())) fill = k_ColStateDefault;
        const bool selected = (node.kind == Selection::State && m_selection == Selection::State && m_selectedState == node.name) ||
                              (node.kind != Selection::State && m_selection == node.kind);
        const bool isHovered = hoveredNode == &node;
        const bool onTimeline = timelineLive && node.kind == Selection::State && node.name == link.timelineState;
        dl->AddRectFilled(node.min, node.max, fill, 5.0f);
        if (isHovered && !selected) dl->AddRectFilled(node.min, node.max, IM_COL32(255, 255, 255, 18), 5.0f);
        ImU32 border = selected ? k_ColSelected : (isHovered ? IM_COL32(170, 176, 188, 255) : k_ColBorder);
        if (onTimeline && !selected) border = k_ColTimeline;
        dl->AddRect(node.min, node.max, border, 5.0f, 0, selected || onTimeline ? 2.5f : 1.2f);
        const ImVec2 textSize = ImGui::CalcTextSize(node.name.c_str());
        dl->PushClipRect(node.min, node.max, true);
        dl->AddText(ImVec2(node.min.x + std::max(8.0f, (nodeSize.x - textSize.x) * 0.5f), node.min.y + (nodeSize.y - textSize.y) * 0.5f - 2.0f),
                    k_ColText, node.name.c_str());
        dl->PopClipRect();
        if (node.kind == Selection::State &&
            std::find(timelineStates.begin(), timelineStates.end(), node.name) != timelineStates.end()) {
            dl->AddCircleFilled(ImVec2(node.max.x - 8.0f, node.min.y + 8.0f), 3.0f, k_ColTimeline, 10);
            if (isHovered && !onPort) ImGui::SetTooltip("Used by a clip on the Animation Timeline.");
        }
        auto drawProgress = [&](f32 progress, ImU32 color) {
            const f32 barY = node.max.y - 5.0f;
            dl->AddRectFilled(ImVec2(node.min.x + 6.0f, barY), ImVec2(node.max.x - 6.0f, barY + 3.0f), IM_COL32(20, 24, 30, 255), 1.5f);
            dl->AddRectFilled(ImVec2(node.min.x + 6.0f, barY),
                              ImVec2(node.min.x + 6.0f + (nodeSize.x - 12.0f) * std::clamp(progress, 0.0f, 1.0f), barY + 3.0f),
                              color, 1.5f);
        };
        if (node.kind == Selection::State && m_preview) {
            const FixedString<32> key(node.name.c_str());
            if (sm.currentState == key) {
                const f32 duration = graphStateDuration(sm, key);
                drawProgress(duration > 0.0f ? std::fmod(sm.timeInState, duration) / duration : 1.0f, k_ColProgress);
            } else if (sm.fading() && sm.fadeFromState == key) {
                const f32 duration = graphStateDuration(sm, key);
                drawProgress(duration > 0.0f ? std::fmod(sm.fadeFromTime, duration) / duration : 1.0f, IM_COL32(80, 150, 255, 120));
            }
        } else if (onTimeline) {
            drawProgress(link.timelineStateProgress, k_ColTimeline);
        }
        if (isHovered && node.kind != Selection::Exit) {
            const ImVec2 port = portOf(node);
            dl->AddCircleFilled(port, onPort ? 6.0f : 4.5f, onPort ? k_ColSelected : IM_COL32(230, 232, 238, 255), 16);
            if (onPort) {
                ImGui::SetTooltip(node.kind == Selection::Entry ? "Drag to a state to make it the default."
                                                                : "Drag to another node to add a transition.");
            }
        }
    }

    if (!m_linkFrom.empty()) {
        if (const NodeInfo* from = findNode(m_linkFrom)) {
            const NodeInfo* target = nodeAt(mouse);
            const ImVec2 end = (target && target != from) ? edgePoint(*target, centerOf(*from)) : mouse;
            drawArrow(edgePoint(*from, end), end, k_ColSelected, 2.0f);
        }
    }

    bool openRename = false;
    if (ImGui::BeginPopup("##node_ctx")) {
        const std::string name = m_contextNode;
        if (name == k_EntryName) {
            if (ImGui::MenuItem("Set Default State...")) {
                m_linkFrom = name;
                m_linkDrag = false;
                m_status = "Click the state Entry should start in. Esc cancels.";
            }
        } else if (name == k_AnyName) {
            if (ImGui::MenuItem("Make Transition")) {
                m_linkFrom = name;
                m_linkDrag = false;
                m_status = "Click the target state. Esc cancels.";
            }
        } else if (name == k_ExitName) {
            ImGui::TextDisabled("Transitions into Exit return the layer to Entry.");
        } else {
            AnimationState* state = sm.states.get(FixedString<32>(name.c_str()));
            if (ImGui::MenuItem("Make Transition")) {
                m_linkFrom = name;
                m_linkDrag = false;
                m_status = "Click the target state or Exit. Esc cancels.";
            }
            if (ImGui::MenuItem("Set as Default State")) sm.defaultState = name.c_str();
            if (ImGui::MenuItem("Play State")) playState(name);
            if (m_layer == -1 && ImGui::MenuItem("Show In Timeline")) {
                AnimationEditorLink& mutableLink = AnimationEditorLink::get();
                mutableLink.selectedState = name;
                ++mutableLink.selectionSerial;
            }
            const std::vector<std::string> clips = clipNames();
            if (state && !clips.empty() && ImGui::BeginMenu("Motion")) {
                for (const std::string& clip : clips) {
                    const bool current = state->motion.empty() ? clip == name : clip == state->motion.cStr();
                    if (ImGui::MenuItem(clip.c_str(), nullptr, current)) state->motion = clip.c_str();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Rename")) openRename = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Delete State", "Del")) deleteState(name);
        }
        ImGui::EndPopup();
    }
    if (openRename) {
        m_renameTarget = m_contextNode;
        std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", m_contextNode.c_str());
        ImGui::OpenPopup("Rename State");
    }
    if (ImGui::BeginPopupModal("Rename State", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Rename") || enter) {
            renameState(m_renameTarget, m_renameBuffer);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##edge_ctx")) {
        std::vector<AnimationTransition>* list = transitionList(m_contextTransitionFrom);
        if (list && m_contextTransition >= 0 && m_contextTransition < static_cast<int>(list->size())) {
            AnimationTransition& transition = (*list)[static_cast<size_t>(m_contextTransition)];
            ImGui::TextDisabled("%s > %s", m_contextTransitionFrom.c_str(), transition.toState.cStr());
            ImGui::MenuItem("Has Exit Time", nullptr, &transition.hasExitTime);
            if (ImGui::BeginMenu("Blend Time")) {
                for (f32 value : {0.0f, 0.1f, 0.25f, 0.5f, 1.0f}) {
                    char label[16];
                    std::snprintf(label, sizeof(label), "%.2fs", value);
                    if (ImGui::MenuItem(label, nullptr, std::fabs(transition.blendTime - value) < 0.001f)) {
                        transition.blendTime = value;
                        transitionEdited(m_contextTransitionFrom, transition);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Blend Curve")) {
                for (KeyInterp mode : kCurveModes) {
                    if (mode == KeyInterp::Custom) continue;
                    if (ImGui::MenuItem(keyInterpName(mode), nullptr, transition.blendCurve.mode == mode)) {
                        applyKeyInterp(transition.blendCurve, mode);
                        transitionEdited(m_contextTransitionFrom, transition);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Transition", "Del")) {
                list->erase(list->begin() + m_contextTransition);
                m_selection = Selection::None;
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##canvas_ctx")) {
        if (ImGui::MenuItem("Create Empty State")) {
            createState(uniqueStateName("New State").c_str(), "", m_contextPos.x, m_contextPos.y);
        }
        const std::vector<std::string> clips = clipNames();
        if (ImGui::BeginMenu("Create State From Clip", !clips.empty())) {
            for (const std::string& clip : clips) {
                if (ImGui::MenuItem(clip.c_str())) {
                    createState(uniqueStateName(clip.c_str()).c_str(), clip.c_str(), m_contextPos.x, m_contextPos.y);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Frame Graph")) {
            m_canvasScrollX = 0.0f;
            m_canvasScrollY = 0.0f;
        }
        ImGui::EndPopup();
    }

    if (sm.states.empty()) {
        dl->AddText(ImVec2(origin.x + 220.0f, origin.y + 60.0f), IM_COL32(150, 154, 166, 220),
                    "Right-click to create a state.");
    }
    dl->PopClipRect();
}

void AnimatorControllerWindow::renderInspector() {
    if (editingGroup()) {
        ImGui::TextDisabled("Group properties are in the Layers tab.");
        return;
    }
    AnimatorStateMachine& sm = graph();
    switch (m_selection) {
        case Selection::State: {
            AnimationState* state = sm.states.get(FixedString<32>(m_selectedState.c_str()));
            if (state) {
                renderStateInspector(*state);
                return;
            }
            break;
        }
        case Selection::Transition:
            renderTransitionInspector();
            return;
        case Selection::Entry: {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "Entry");
            ImGui::Separator();
            ImGui::TextWrapped("The graph starts in the default state (orange). Exit comes back here.");
            if (ImGui::BeginCombo("Default", sm.defaultState.empty() ? "(none)" : sm.defaultState.cStr())) {
                for (auto& pair : sm.states) {
                    if (ImGui::Selectable(pair.key.cStr(), pair.key == sm.defaultState)) sm.defaultState = pair.key;
                }
                ImGui::EndCombo();
            }
            return;
        }
        case Selection::AnyState: {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.85f, 1.0f), "Any State");
            ImGui::Separator();
            ImGui::TextWrapped("Its transitions are checked first, from whatever state is playing. "
                               "They need a condition, or exit time, to fire.");
            ImGui::SeparatorText("Transitions");
            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(sm.anyStateTransitions.size()); ++i) {
                const AnimationTransition& transition = sm.anyStateTransitions[static_cast<size_t>(i)];
                ImGui::PushID(i);
                char label[96];
                std::snprintf(label, sizeof(label), "> %s%s", transition.toState.cStr(),
                              transition.conditions.empty() && !transition.hasExitTime ? "  (never)" : "");
                if (ImGui::Selectable(label, false, 0, ImVec2(ImGui::GetContentRegionAvail().x - 24.0f, 0.0f))) {
                    m_selection = Selection::Transition;
                    m_selectedTransitionFrom = k_AnyName;
                    m_selectedTransition = i;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) removeIndex = i;
                ImGui::PopID();
            }
            if (removeIndex >= 0) sm.anyStateTransitions.erase(sm.anyStateTransitions.begin() + removeIndex);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##add_any", "Add transition to...")) {
                std::string target;
                for (auto& pair : sm.states) {
                    if (ImGui::Selectable(pair.key.cStr())) target = pair.key.cStr();
                }
                ImGui::EndCombo();
                if (!target.empty()) addTransition(k_AnyName, target);
            }
            return;
        }
        case Selection::Exit:
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1.0f), "Exit");
            ImGui::Separator();
            ImGui::TextWrapped("A transition into Exit leaves the current state and goes back through Entry "
                               "to the default state, with the transition's blend.");
            return;
        case Selection::None:
            break;
    }
    ImGui::TextDisabled("Select a state or a transition.");
    ImGui::Spacing();
    ImGui::Text("States: %zu", static_cast<size_t>(sm.states.size()));
    ImGui::Text("Default: %s", sm.defaultState.empty() ? "(none)" : sm.defaultState.cStr());
    ImGui::Text("Any State transitions: %zu", sm.anyStateTransitions.size());
}

void AnimatorControllerWindow::renderStateInspector(AnimationState& state) {
    AnimatorStateMachine& sm = graph();
    if (m_renameTarget != m_selectedState) {
        m_renameTarget = m_selectedState;
        std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", m_selectedState.c_str());
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##state_name", m_renameBuffer, sizeof(m_renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string from = m_selectedState;
        renameState(from, m_renameBuffer);
        return;
    }
    ImGui::TextDisabled("Enter renames the state.");
    ImGui::Separator();

    const std::vector<std::string> clips = clipNames();
    const char* motionPreview = state.motion.empty() ? state.name.cStr() : state.motion.cStr();
    if (ImGui::BeginCombo("Motion", motionPreview)) {
        for (const std::string& clip : clips) {
            if (ImGui::Selectable(clip.c_str(), clip == motionPreview)) state.motion = clip.c_str();
        }
        if (clips.empty()) ImGui::TextDisabled("No imported clips.");
        ImGui::EndCombo();
    }
    ImGui::DragFloat("Speed", &state.speed, 0.01f, 0.0f, 10.0f);
    const f32 duration = stateDuration(state);
    if (duration > 0.0f) ImGui::TextDisabled("Length %.2fs", duration);
    if (sm.defaultState == state.name) {
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.25f, 1.0f), "Default state");
    } else if (ImGui::Button("Set as Default State")) {
        sm.defaultState = state.name;
    }
    ImGui::SameLine();
    if (ImGui::Button("Play")) playState(m_selectedState);
    if (m_layer == -1) {
        ImGui::SameLine();
        if (ImGui::Button("Timeline")) {
            AnimationEditorLink& link = AnimationEditorLink::get();
            link.selectedState = m_selectedState;
            ++link.selectionSerial;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select this state's clip in the Animation Timeline.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Transitions");
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(state.transitions.size()); ++i) {
        const AnimationTransition& transition = state.transitions[static_cast<size_t>(i)];
        ImGui::PushID(i);
        char label[96];
        std::snprintf(label, sizeof(label), "> %s  %.2fs%s", transition.toState.cStr(), transition.blendTime,
                      transition.conditions.empty() ? (transition.hasExitTime ? "  exit time" : "  (never)") : "");
        if (ImGui::Selectable(label, false, 0, ImVec2(ImGui::GetContentRegionAvail().x - 24.0f, 0.0f))) {
            m_selection = Selection::Transition;
            m_selectedTransitionFrom = m_selectedState;
            m_selectedTransition = i;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeIndex = i;
        ImGui::PopID();
    }
    if (removeIndex >= 0) state.transitions.erase(state.transitions.begin() + removeIndex);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##add_transition", "Add transition to...")) {
        const std::string from = m_selectedState;
        std::string target;
        for (auto& pair : sm.states) {
            if (from == pair.key.cStr()) continue;
            if (ImGui::Selectable(pair.key.cStr())) target = pair.key.cStr();
        }
        if (ImGui::Selectable(k_ExitName)) target = k_ExitName;
        ImGui::EndCombo();
        if (!target.empty()) addTransition(from, target);
    }
}

void AnimatorControllerWindow::renderTransitionTimeline(AnimationTransition& transition, const std::string& from) {
    AnimatorStateMachine& sm = graph();
    const ImGuiIO& io = ImGui::GetIO();
    const std::string to = transition.toState.cStr();
    f32 sourceLength = from == k_AnyName ? 0.0f : graphStateDuration(sm, FixedString<32>(from.c_str()));
    const FixedString<32> targetKey = to == k_ExitName ? sm.defaultState : transition.toState;
    f32 targetLength = graphStateDuration(sm, targetKey);
    if (sourceLength <= 0.0f) sourceLength = 1.0f;
    if (targetLength <= 0.0f) targetLength = 1.0f;
    const f32 exitStart = transition.hasExitTime ? std::max(transition.exitTime, 0.0f) * sourceLength : sourceLength * 0.5f;
    const f32 blend = std::max(transition.blendTime, 0.0f);
    const f32 targetVisible = targetLength * (1.0f - std::clamp(transition.offset, 0.0f, 1.0f));
    const f32 total = std::max({sourceLength, exitStart + blend, exitStart + targetVisible}) * 1.08f;

    const f32 width = ImGui::GetContentRegionAvail().x;
    const ImVec2 size(width, 70.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const f32 pad = 6.0f;
    const f32 scale = (size.x - pad * 2.0f) / total;
    auto timeToX = [&](f32 t) { return pos.x + pad + t * scale; };

    ImGui::InvisibleButton("##transition_bars", size);
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(26, 28, 34, 255), 4.0f);

    const f32 sourceTop = pos.y + 8.0f;
    const f32 sourceBottom = pos.y + 28.0f;
    const f32 targetTop = pos.y + 36.0f;
    const f32 targetBottom = pos.y + 56.0f;
    dl->AddRectFilled(ImVec2(timeToX(0.0f), sourceTop), ImVec2(timeToX(sourceLength), sourceBottom), IM_COL32(70, 110, 170, 255), 3.0f);
    dl->AddText(ImVec2(timeToX(0.0f) + 4.0f, sourceTop + 3.0f), k_ColText, from.c_str());
    const f32 targetStartX = timeToX(exitStart);
    const f32 targetEndX = timeToX(exitStart + targetVisible);
    dl->AddRectFilled(ImVec2(targetStartX, targetTop), ImVec2(targetEndX, targetBottom), IM_COL32(70, 140, 100, 255), 3.0f);
    dl->PushClipRect(ImVec2(targetStartX, targetTop), ImVec2(targetEndX, targetBottom), true);
    dl->AddText(ImVec2(targetStartX + 4.0f, targetTop + 3.0f), k_ColText, to.c_str());
    dl->PopClipRect();
    const f32 blendEndX = timeToX(exitStart + blend);
    if (blend > 0.0f) {
        dl->AddRectFilled(ImVec2(targetStartX, sourceTop), ImVec2(blendEndX, targetBottom), IM_COL32(255, 255, 255, 30));
        drawCurveGlyph(dl, ImVec2(targetStartX, targetBottom), ImVec2(blendEndX, sourceTop), transition.blendCurve,
                       IM_COL32(255, 210, 60, 255), 1.6f);
    }
    dl->AddLine(ImVec2(targetStartX, sourceTop - 4.0f), ImVec2(targetStartX, targetBottom + 4.0f), IM_COL32(255, 255, 255, 160), 1.0f);
    dl->AddLine(ImVec2(blendEndX, sourceTop - 4.0f), ImVec2(blendEndX, targetBottom + 4.0f), IM_COL32(255, 210, 60, 220), 1.5f);
    char caption[96];
    std::snprintf(caption, sizeof(caption), transition.hasExitTime ? "exit %.2fs   blend %.2fs" : "on condition   blend %.2fs",
                  transition.hasExitTime ? exitStart : blend, blend);
    dl->AddText(ImVec2(pos.x + pad, targetBottom + 1.0f), IM_COL32(150, 156, 170, 255), caption);

    if (ImGui::IsItemClicked()) {
        if (std::fabs(io.MousePos.x - blendEndX) <= 6.0f) m_barDrag = 2;
        else if (transition.hasExitTime && io.MousePos.y >= targetTop && io.MousePos.x >= targetStartX && io.MousePos.x <= targetEndX) m_barDrag = 1;
        else m_barDrag = 0;
    }
    if (!active) m_barDrag = 0;
    if (hovered && m_barDrag == 0) {
        if (std::fabs(io.MousePos.x - blendEndX) <= 6.0f) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ImGui::SetTooltip("Drag to change the blend time.");
        } else if (transition.hasExitTime && io.MousePos.y >= targetTop && io.MousePos.x >= targetStartX && io.MousePos.x <= targetEndX) {
            ImGui::SetTooltip("Drag to change the exit time.");
        }
    }
    if (active && m_barDrag != 0 && io.MouseDelta.x != 0.0f) {
        const f32 delta = io.MouseDelta.x / scale;
        if (m_barDrag == 1) transition.exitTime = std::clamp(transition.exitTime + delta / sourceLength, 0.0f, 10.0f);
        else transition.blendTime = std::clamp(transition.blendTime + delta, 0.0f, 5.0f);
        transitionEdited(from, transition);
    }
}

void AnimatorControllerWindow::renderTransitionInspector() {
    AnimationTransition* selected = selectedTransition();
    if (!selected) {
        m_selection = Selection::None;
        ImGui::TextDisabled("Select a state or a transition.");
        return;
    }
    AnimationTransition& transition = *selected;
    const std::string from = m_selectedTransitionFrom;
    const std::string to = transition.toState.cStr();
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s  >  %s", from.c_str(), to.c_str());
    if (timelineLinked(from, to)) {
        ImGui::TextColored(ImVec4(0.45f, 0.86f, 0.55f, 1.0f), "Synced with the timeline crossfade");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Blend time and curve follow the overlap of these clips on the Animation Timeline, both ways.");
        }
    }
    if (from == k_AnyName) ImGui::TextDisabled("Fires from any state when it is ready.");
    if (to == k_ExitName) ImGui::TextDisabled("Goes back to Entry, then the default state.");
    ImGui::Separator();

    bool changed = false;
    const f32 width = ImGui::GetContentRegionAvail().x;
    ImGui::PushItemWidth(width - 100.0f);
    changed |= ImGui::Checkbox("Has Exit Time", &transition.hasExitTime);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Waits for the exit time. With no conditions, the transition fires right at it.");
    }
    ImGui::BeginDisabled(!transition.hasExitTime);
    changed |= ImGui::DragFloat("Exit Time", &transition.exitTime, 0.01f, 0.0f, 10.0f, "%.2f");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Normalized time of the source state. 1 is the end of its clip.");
    }
    changed |= ImGui::DragFloat("Blend Time", &transition.blendTime, 0.01f, 0.0f, 5.0f, "%.2fs");
    changed |= ImGui::SliderFloat("Offset", &transition.offset, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Normalized time the target state starts at.");
    ImGui::PopItemWidth();
    transition.blendTime = std::max(transition.blendTime, 0.0f);

    ImGui::Spacing();
    renderTransitionTimeline(transition, from);
    ImGui::SeparatorText("Blend Curve");
    changed |= editKeyCurve("blend_curve", transition.blendCurve, width, 90.0f);
    if (changed) transitionEdited(from, transition);

    ImGui::Spacing();
    ImGui::SeparatorText("Conditions");
    if (transition.conditions.empty()) {
        ImGui::TextDisabled(transition.hasExitTime ? "None. Fires at exit time." : "None. This transition never fires.");
    }
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(transition.conditions.size()); ++i) {
        TransitionCondition& condition = transition.conditions[static_cast<size_t>(i)];
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::BeginCombo("##param", condition.parameterName.empty() ? "(parameter)" : condition.parameterName.cStr())) {
            for (const AnimatorParameter& parameter : m_animator->parameters) {
                if (ImGui::Selectable(parameter.name.cStr(), parameter.name == condition.parameterName)) {
                    condition.parameterName = parameter.name;
                    condition.op = parameter.type == ParameterType::Float ? ConditionOperator::Greater : ConditionOperator::Equals;
                    if (parameter.type == ParameterType::Bool) condition.boolValue = true;
                    else if (parameter.type == ParameterType::Float) condition.floatValue = 0.0f;
                    else condition.intValue = 0;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const AnimatorParameter* parameter = m_animator->findParameter(condition.parameterName);
        if (!parameter) {
            ImGui::TextDisabled("missing");
        } else if (parameter->type == ParameterType::Trigger) {
            ImGui::TextDisabled("is fired");
        } else if (parameter->type == ParameterType::Bool) {
            ImGui::SetNextItemWidth(70.0f);
            int value = condition.boolValue ? 0 : 1;
            const char* items[] = {"true", "false"};
            if (ImGui::Combo("##bool", &value, items, 2)) {
                condition.op = ConditionOperator::Equals;
                condition.boolValue = value == 0;
            }
        } else {
            const bool isFloat = parameter->type == ParameterType::Float;
            const char* ops[] = {"Greater", "Less", "Equals", "NotEqual"};
            const ConditionOperator opValues[] = {ConditionOperator::Greater, ConditionOperator::Less,
                                                  ConditionOperator::Equals, ConditionOperator::NotEquals};
            const int opCount = isFloat ? 2 : 4;
            int current = 0;
            for (int o = 0; o < opCount; ++o) {
                if (condition.op == opValues[o]) current = o;
            }
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::Combo("##op", &current, ops, opCount)) condition.op = opValues[current];
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50.0f);
            if (isFloat) {
                ImGui::DragFloat("##value", &condition.floatValue, 0.01f);
            } else {
                int value = condition.intValue;
                if (ImGui::DragInt("##value", &value)) condition.intValue = value;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeIndex = i;
        ImGui::PopID();
    }
    if (removeIndex >= 0) transition.conditions.erase(transition.conditions.begin() + removeIndex);

    ImGui::BeginDisabled(m_animator->parameters.empty());
    if (ImGui::Button("+ Condition")) {
        const AnimatorParameter& parameter = m_animator->parameters.front();
        TransitionCondition condition;
        condition.parameterName = parameter.name;
        condition.op = parameter.type == ParameterType::Float ? ConditionOperator::Greater : ConditionOperator::Equals;
        if (parameter.type == ParameterType::Bool) condition.boolValue = true;
        else if (parameter.type == ParameterType::Float) condition.floatValue = 0.0f;
        else condition.intValue = 0;
        transition.conditions.push_back(condition);
    }
    ImGui::EndDisabled();
    if (m_animator->parameters.empty()) ImGui::TextDisabled("Add a parameter in the Parameters tab first.");

    ImGui::Spacing();
    if (ImGui::Button("Delete Transition")) {
        if (std::vector<AnimationTransition>* list = transitionList(from)) {
            list->erase(list->begin() + m_selectedTransition);
        }
        m_selection = Selection::None;
    }
}

bool AnimatorControllerWindow::layerInside(int index, const std::string& group) const {
    const std::vector<AnimatorLayer>& layers = m_animator->layers;
    if (index < 0 || index >= static_cast<int>(layers.size()) || group.empty()) return false;
    std::string parent = layers[static_cast<size_t>(index)].parentGroup.cStr();
    for (int depth = 0; depth < 16 && !parent.empty(); ++depth) {
        if (parent == group) return true;
        std::string next;
        for (const AnimatorLayer& layer : layers) {
            if (layer.isGroup && parent == layer.name.cStr()) next = layer.parentGroup.cStr();
        }
        parent = next;
    }
    return false;
}

void AnimatorControllerWindow::selectLayer(int index) {
    m_layer = index;
    m_selection = Selection::None;
    m_selectedState.clear();
    m_selectedTransition = -1;
    m_linkFrom.clear();
    m_linkDrag = false;
    m_dragNode.clear();
    m_status.clear();
    if (!editingGroup()) layoutMissingNodes();
}

void AnimatorControllerWindow::addLayer(bool group) {
    std::vector<AnimatorLayer>& layers = m_animator->layers;
    AnimatorLayer layer;
    layer.name = uniqueLayerName(group ? "Group" : "Layer").c_str();
    layer.isGroup = group;
    if (m_layer >= 0 && m_layer < static_cast<int>(layers.size())) {
        const AnimatorLayer& selected = layers[static_cast<size_t>(m_layer)];
        layer.parentGroup = selected.isGroup ? selected.name : selected.parentGroup;
    }
    layers.push_back(layer);
    selectLayer(static_cast<int>(layers.size()) - 1);
}

void AnimatorControllerWindow::deleteLayer(int index) {
    std::vector<AnimatorLayer>& layers = m_animator->layers;
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    const AnimatorLayer removed = layers[static_cast<size_t>(index)];
    if (removed.isGroup) {
        for (AnimatorLayer& layer : layers) {
            if (layer.parentGroup == removed.name) layer.parentGroup = removed.parentGroup;
        }
    }
    layers.erase(layers.begin() + index);
    selectLayer(-1);
}

void AnimatorControllerWindow::moveLayer(int index, int before, const std::string& parent) {
    std::vector<AnimatorLayer>& layers = m_animator->layers;
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    const std::string movedName = layers[static_cast<size_t>(index)].name.cStr();
    if (layers[static_cast<size_t>(index)].isGroup && parent == movedName) return;
    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i].isGroup && parent == layers[i].name.cStr() && layerInside(static_cast<int>(i), movedName)) return;
    }
    const std::string selectedName = m_layer >= 0 && m_layer < static_cast<int>(layers.size())
                                         ? std::string(layers[static_cast<size_t>(m_layer)].name.cStr())
                                         : std::string();
    const std::string beforeName = before >= 0 && before < static_cast<int>(layers.size())
                                       ? std::string(layers[static_cast<size_t>(before)].name.cStr())
                                       : std::string();

    std::vector<AnimatorLayer> block;
    std::vector<AnimatorLayer> rest;
    for (size_t i = 0; i < layers.size(); ++i) {
        const bool inBlock = static_cast<int>(i) == index ||
                             (layers[static_cast<size_t>(index)].isGroup && layerInside(static_cast<int>(i), movedName));
        (inBlock ? block : rest).push_back(layers[i]);
    }
    block.front().parentGroup = parent.c_str();
    if (!beforeName.empty() && beforeName == movedName) return;

    size_t insertAt = rest.size();
    if (!beforeName.empty()) {
        for (size_t i = 0; i < rest.size(); ++i) {
            if (beforeName == rest[i].name.cStr()) insertAt = i;
        }
    } else if (!parent.empty()) {
        for (size_t i = 0; i < rest.size(); ++i) {
            if (rest[i].isGroup && parent == rest[i].name.cStr()) insertAt = i + 1;
        }
        bool advanced = true;
        while (advanced && insertAt < rest.size()) {
            advanced = false;
            std::string ancestor = rest[insertAt].parentGroup.cStr();
            for (int depth = 0; depth < 16 && !ancestor.empty(); ++depth) {
                if (ancestor == parent) {
                    ++insertAt;
                    advanced = true;
                    break;
                }
                std::string next;
                for (const AnimatorLayer& layer : rest) {
                    if (layer.isGroup && ancestor == layer.name.cStr()) next = layer.parentGroup.cStr();
                }
                ancestor = next;
            }
        }
    }
    rest.insert(rest.begin() + static_cast<std::ptrdiff_t>(insertAt), block.begin(), block.end());
    layers = std::move(rest);
    m_layer = -1;
    for (size_t i = 0; i < layers.size(); ++i) {
        if (!selectedName.empty() && selectedName == layers[i].name.cStr()) m_layer = static_cast<int>(i);
    }
}

void AnimatorControllerWindow::renderLayerRow(int index, int depth) {
    std::vector<AnimatorLayer>& layers = m_animator->layers;
    AnimatorLayer& layer = layers[static_cast<size_t>(index)];
    ImGui::PushID(index);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (!layer.isGroup) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (m_layer == index) flags |= ImGuiTreeNodeFlags_Selected;
    if (layer.isGroup) ImGui::SetNextItemOpen(layer.expanded);
    char label[96];
    std::snprintf(label, sizeof(label), "%s%s%s", layer.isGroup ? "[G] " : "", layer.name.cStr(), layer.muted ? "  (muted)" : "");
    const f32 rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    if (layer.muted) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool open = ImGui::TreeNodeEx("##layer", flags, "%s", label);
    if (layer.muted) ImGui::PopStyleColor();
    if (layer.isGroup) layer.expanded = open;
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selectLayer(index);
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(k_LayerPayload, &index, sizeof(int));
        ImGui::Text("%s", layer.name.cStr());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(k_LayerPayload)) {
            const int source = *static_cast<const int*>(payload->Data);
            if (source != index && !layerInside(index, layers[static_cast<size_t>(source)].name.cStr())) {
                m_moveLayer = source;
                if (layer.isGroup) {
                    m_moveBefore = -1;
                    m_moveParent = layer.name.cStr();
                } else {
                    m_moveBefore = index;
                    m_moveParent = layer.parentGroup.cStr();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("##layer_ctx")) {
        if (ImGui::MenuItem("Mute", nullptr, layer.muted)) layer.muted = !layer.muted;
        if (!layer.isGroup && ImGui::MenuItem("Duplicate")) {
            AnimatorLayer copy = layer;
            copy.name = uniqueLayerName(layer.name.cStr()).c_str();
            layers.insert(layers.begin() + index + 1, copy);
            ImGui::EndPopup();
            ImGui::PopID();
            return;
        }
        if (!layer.parentGroup.empty() && ImGui::MenuItem("Move Out of Group")) {
            m_moveLayer = index;
            m_moveBefore = -1;
            std::string grandparent;
            for (const AnimatorLayer& other : layers) {
                if (other.isGroup && other.name == layer.parentGroup) grandparent = other.parentGroup.cStr();
            }
            m_moveParent = grandparent;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(layer.isGroup ? "Delete Group" : "Delete Layer")) m_contextLayer = index;
        ImGui::EndPopup();
    }
    ImGui::SameLine(rowRight - 34.0f);
    ImGui::TextDisabled("%.2f", layer.weight);
    if (layer.isGroup && open) {
        const std::string groupName = layer.name.cStr();
        if (depth < 8) {
            for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
                if (i != index && groupName == layers[static_cast<size_t>(i)].parentGroup.cStr()) renderLayerRow(i, depth + 1);
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void AnimatorControllerWindow::renderLayersPanel() {
    std::vector<AnimatorLayer>& layers = m_animator->layers;
    if (ImGui::SmallButton("+ Layer")) addLayer(false);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Group")) addLayer(true);
    ImGui::SameLine();
    ImGui::TextDisabled("drag to reorder");

    if (ImGui::Selectable("Base Layer", m_layer == -1)) selectLayer(-1);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(k_LayerPayload)) {
            m_moveLayer = *static_cast<const int*>(payload->Data);
            m_moveBefore = layers.empty() ? -1 : 0;
            m_moveParent.clear();
        }
        ImGui::EndDragDropTarget();
    }
    auto groupExists = [&](const FixedString<32>& name) {
        for (const AnimatorLayer& layer : layers) {
            if (layer.isGroup && layer.name == name) return true;
        }
        return false;
    };
    m_contextLayer = -2;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        const AnimatorLayer& layer = layers[static_cast<size_t>(i)];
        if (layer.parentGroup.empty() || !groupExists(layer.parentGroup)) renderLayerRow(i, 0);
        if (static_cast<int>(layers.size()) <= i) break;
    }
    ImGui::InvisibleButton("##layer_drop_end", ImVec2(std::max(ImGui::GetContentRegionAvail().x, 1.0f), 14.0f));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(k_LayerPayload)) {
            m_moveLayer = *static_cast<const int*>(payload->Data);
            m_moveBefore = -1;
            m_moveParent.clear();
        }
        ImGui::EndDragDropTarget();
    }
    if (m_moveLayer >= 0) {
        moveLayer(m_moveLayer, m_moveBefore, m_moveParent);
        m_moveLayer = -1;
    }
    if (m_contextLayer >= 0) {
        deleteLayer(m_contextLayer);
        m_contextLayer = -2;
    }
    if (layers.empty()) ImGui::TextDisabled("Layers play on top of the base layer,\nfor example an upper-body wave.");
    ImGui::Separator();
    renderLayerProperties();
}

void AnimatorControllerWindow::renderLayerProperties() {
    std::vector<AnimatorLayer>& layers = m_animator->layers;
    if (m_layer < 0 || m_layer >= static_cast<int>(layers.size())) {
        ImGui::TextUnformatted("Base Layer");
        ImGui::TextDisabled("Always full weight. Layers above\nblend over it in list order.");
        return;
    }
    AnimatorLayer& layer = layers[static_cast<size_t>(m_layer)];
    if (m_layerNameTarget != m_layer) {
        m_layerNameTarget = m_layer;
        std::snprintf(m_layerName, sizeof(m_layerName), "%s", layer.name.cStr());
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##layer_name", m_layerName, sizeof(m_layerName));
    if (ImGui::IsItemDeactivatedAfterEdit() && m_layerName[0] != '\0' && std::string(m_layerName) != layer.name.cStr()) {
        const std::string wanted = uniqueLayerName(m_layerName);
        const FixedString<32> oldName = layer.name;
        layer.name = wanted.c_str();
        if (layer.isGroup) {
            for (AnimatorLayer& other : layers) {
                if (other.parentGroup == oldName) other.parentGroup = layer.name;
            }
        }
        std::snprintf(m_layerName, sizeof(m_layerName), "%s", wanted.c_str());
    }
    ImGui::PushItemWidth(-90.0f);
    ImGui::Checkbox("Mute", &layer.muted);
    ImGui::SliderFloat("Weight", &layer.weight, 0.0f, 1.0f, "%.2f");
    ImGui::TextDisabled("Effective weight %.2f", effectiveLayerWeight(*m_animator, layer));
    if (!layer.isGroup) {
        int blend = layer.blend == LayerBlendMode::Additive ? 1 : 0;
        const char* modes[] = {"Override", "Additive"};
        if (ImGui::Combo("Blending", &blend, modes, 2)) layer.blend = blend == 1 ? LayerBlendMode::Additive : LayerBlendMode::Override;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Override replaces the pose of the masked body parts.\nAdditive adds this layer's motion on top.");
        }
        ImGui::PopItemWidth();
        ImGui::SeparatorText("Body Mask");
        static const char* parts[] = {"Root", "Body", "Head", "Left Arm", "Right Arm", "Left Leg", "Right Leg"};
        for (int bit = 0; bit < 7; ++bit) {
            unsigned int mask = layer.mask;
            if (ImGui::CheckboxFlags(parts[bit], &mask, 1u << bit)) layer.mask = mask;
            if (bit % 2 == 0 && bit < 6) ImGui::SameLine(110.0f);
        }
        if (ImGui::SmallButton("All")) layer.mask = static_cast<u32>(BodyMask::All);
        ImGui::SameLine();
        if (ImGui::SmallButton("None")) layer.mask = 0u;
        ImGui::SameLine();
        if (ImGui::SmallButton("Upper Body")) {
            layer.mask = static_cast<u32>(BodyMask::Body) | static_cast<u32>(BodyMask::Head) |
                         static_cast<u32>(BodyMask::LeftArm) | static_cast<u32>(BodyMask::RightArm);
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%zu states", static_cast<size_t>(layer.states.size()));
        if (ImGui::SmallButton("Copy States From Base")) {
            layer.states = m_animator->states;
            layer.defaultState = m_animator->defaultState;
            layer.anyStateTransitions = m_animator->anyStateTransitions;
            resetGraphPlayback(layer);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replaces this layer's graph with a copy of the base layer's.");
    } else {
        ImGui::PopItemWidth();
        int children = 0;
        for (const AnimatorLayer& other : layers) {
            if (other.parentGroup == layer.name) ++children;
        }
        ImGui::TextDisabled("%d layers inside. Drop layers on the group.", children);
    }
    ImGui::Spacing();
    if (ImGui::Button(layer.isGroup ? "Delete Group" : "Delete Layer")) deleteLayer(m_layer);
}

void AnimatorControllerWindow::renderParameterPanel() {
    if (!m_animator) return;

    if (ImGui::SmallButton("+")) ImGui::OpenPopup("##add_param");
    if (ImGui::BeginPopup("##add_param")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("Name", m_newParamName, sizeof(m_newParamName), ImGuiInputTextFlags_EnterReturnsTrue);
        const char* paramTypes[] = {"Float", "Int", "Bool", "Trigger"};
        const ParameterType types[] = {ParameterType::Float, ParameterType::Int, ParameterType::Bool, ParameterType::Trigger};
        ImGui::Combo("Type", &m_newParamType, paramTypes, 4);
        if ((ImGui::Button("Add") || enter) && m_newParamName[0] != '\0') {
            m_animator->addParameter(m_newParamName, types[std::clamp(m_newParamType, 0, 3)]);
            std::memset(m_newParamName, 0, sizeof(m_newParamName));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##param_filter", "Search", m_paramFilter, sizeof(m_paramFilter));

    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(m_animator->parameters.size()); ++i) {
        AnimatorParameter& p = m_animator->parameters[static_cast<size_t>(i)];
        if (m_paramFilter[0] != '\0' && std::string(p.name.cStr()).find(m_paramFilter) == std::string::npos) continue;
        ImGui::PushID(i);
        const char* glyph = "B";
        if (p.type == ParameterType::Float) glyph = "F";
        else if (p.type == ParameterType::Int) glyph = "I";
        else if (p.type == ParameterType::Trigger) glyph = "T";
        ImGui::TextDisabled("%s", glyph);
        ImGui::SameLine();
        ImGui::TextUnformatted(p.name.cStr());
        if (ImGui::BeginPopupContextItem("##param_ctx")) {
            if (ImGui::MenuItem("Delete Parameter")) removeIndex = i;
            ImGui::EndPopup();
        }
        ImGui::SameLine(110.0f);
        ImGui::SetNextItemWidth(-1.0f);
        switch (p.type) {
            case ParameterType::Bool: {
                bool v = p.boolValue;
                if (ImGui::Checkbox("##v", &v)) p.boolValue = v;
                break;
            }
            case ParameterType::Float:
                ImGui::DragFloat("##v", &p.floatValue, 0.01f);
                break;
            case ParameterType::Int: {
                int v = p.intValue;
                if (ImGui::DragInt("##v", &v)) p.intValue = static_cast<i32>(v);
                break;
            }
            case ParameterType::Trigger:
                if (ImGui::SmallButton(p.triggered ? "Set" : "Fire")) p.triggered = true;
                break;
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) m_animator->parameters.erase(m_animator->parameters.begin() + removeIndex);
    if (m_animator->parameters.empty()) {
        ImGui::TextDisabled("No parameters. Use + to add one.");
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Right-click a parameter to delete it.");
}

}

#endif
