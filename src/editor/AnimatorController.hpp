#pragma once
#include "core/Types.hpp"
#include "animation/AnimationComponents.hpp"
#include "math/Vec2.hpp"
#include <string>
#include <unordered_map>

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
    Animation::Animator* getAnimator() const { return m_animator; }

    void render();

    bool isOpen() const  { return m_open; }
    void open()          { m_open = true;  }
    void close()         { m_open = false; }

private:
    void renderCanvas();
    void renderInspector();
    void renderParameterPanel();

    void drawStateNode(const std::string& name, const Animation::AnimationState& state, bool isActive);
    void drawTransitionArrow(ImVec2 from, ImVec2 to, bool isActive);

    ImVec2 stateNodeSize() const { return {150.0f, 44.0f}; }
    ImVec2 nodeCenter(const StateNodePos& pos) const;

    Animation::Animator  m_internalAnimator;
    Animation::Animator* m_animator = &m_internalAnimator;

    std::unordered_map<std::string, StateNodePos> m_nodePositions;

    std::string m_selectedState;
    std::string m_selectedTransitionFrom;
    std::string m_selectedTransitionTo;

    bool  m_open           = true;
    bool  m_draggingNode   = false;
    float m_canvasScrollX  = 0.0f;
    float m_canvasScrollY  = 0.0f;
    bool  m_addStatePopup  = false;

    char  m_newStateName[32]  = {};
    char  m_newParamName[32]  = {};
    int   m_newParamType      = 0;
    char  m_transitionTo[32]  = {};
    bool  m_showAddTransition = false;
};

}
