#pragma once
#include "core/Types.hpp"
#include "animation/AnimationComponents.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include "math/Vec2.hpp"
#include <string>
#include <vector>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

struct StateNodePos {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

class AnimatorControllerWindow {
public:
    AnimatorControllerWindow() = default;

    void setAnimator(Animation::Animator* animator);
    void bindSelection(ECS::World* world, ECS::Entity entity, const std::string& projectRoot);
    Animation::Animator* getAnimator() const { return m_animator; }

    void render();

    bool isOpen() const  { return m_open; }
    void open()          { m_open = true;  }
    void close()         { m_open = false; }

private:
    enum class Selection { None, State, Transition, Entry, AnyState, Exit };

    void renderToolbar();
    void renderCanvas();
    void renderInspector();
    void renderStateInspector(Animation::AnimationState& state);
    void renderTransitionInspector();
    void renderTransitionTimeline(Animation::AnimationTransition& transition, const std::string& from);
    void renderParameterPanel();
    void renderLayersPanel();
    void renderLayerRow(int index, int depth);
    void renderLayerProperties();
    void runPreview();
    void startPreview();
    void stopPreview();
    void publishLink();
    void consumeLink();

    /// Graph the canvas edits: the base layer or the selected layer.
    Animation::AnimatorStateMachine& graph();
    bool editingGroup() const;
    std::vector<Animation::AnimationTransition>* transitionList(const std::string& from);
    Animation::AnimationTransition* selectedTransition();
    void transitionEdited(const std::string& from, const Animation::AnimationTransition& transition);
    bool timelineLinked(const std::string& from, const std::string& to) const;

    std::vector<std::string> clipNames() const;
    f32 stateDuration(const Animation::AnimationState& state) const;
    f32 graphStateDuration(const Animation::AnimatorStateMachine& sm, const FixedString<32>& name) const;
    void layoutMissingNodes();
    std::string uniqueStateName(const char* base) const;
    std::string uniqueLayerName(const char* base) const;
    void createState(const char* name, const char* motion, f32 x, f32 y);
    void renameState(const std::string& from, const std::string& to);
    void deleteState(const std::string& name);
    void addTransition(const std::string& from, const std::string& to);
    void playState(const std::string& name);
    void selectLayer(int index);
    void addLayer(bool group);
    void deleteLayer(int index);
    void moveLayer(int index, int before, const std::string& parent);
    bool layerInside(int index, const std::string& group) const;
    void selectState(const std::string& name);

    ImVec2 stateNodeSize() const { return {150.0f, 38.0f}; }

    Animation::Animator  m_internalAnimator;
    Animation::Animator* m_animator = &m_internalAnimator;
    Animation::Animator* m_bound = nullptr;
    ECS::World* m_world = nullptr;
    ECS::Entity m_entity = ECS::Entity::INVALID;
    /// -1 is the base layer, otherwise an index into Animator::layers.
    int m_layer = -1;

    Selection m_selection = Selection::None;
    std::string m_selectedState;
    std::string m_selectedTransitionFrom;
    int m_selectedTransition = -1;

    std::string m_dragNode;
    bool m_panning = false;
    std::string m_linkFrom;
    bool m_linkDrag = false;
    std::string m_contextNode;
    std::string m_contextTransitionFrom;
    int m_contextTransition = -1;
    StateNodePos m_contextPos;
    std::string m_status;
    int m_contextLayer = -2;
    int m_barDrag = 0;
    int m_moveLayer = -1;
    int m_moveBefore = -1;
    std::string m_moveParent;

    bool  m_open           = true;
    bool  m_preview        = false;
    float m_canvasScrollX  = 0.0f;
    float m_canvasScrollY  = 0.0f;
    ImVec2 m_canvasSize{0.0f, 0.0f};
    u64   m_focusSeen      = 0;

    char  m_paramFilter[32]   = {};
    char  m_renameBuffer[32]  = {};
    std::string m_renameTarget;
    char  m_layerName[32]     = {};
    int   m_layerNameTarget   = -2;
    char  m_newParamName[32]  = {};
    int   m_newParamType      = 0;
};

}
