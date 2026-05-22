#include "editor/AnimatorController.hpp"
#include <cstring>
#include <cmath>
#include <cstdio>

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

using namespace Animation;

static const ImU32 k_ColCanvas       = IM_COL32(28,  28,  36,  255);
static const ImU32 k_ColGrid         = IM_COL32(45,  45,  58,  255);
static const ImU32 k_ColStateNormal  = IM_COL32(50,  70, 110,  255);
static const ImU32 k_ColStateActive  = IM_COL32(40, 130,  80,  255);
static const ImU32 k_ColStateSel     = IM_COL32(80, 110, 180,  255);
static const ImU32 k_ColStateBorder  = IM_COL32(100,120, 180,  255);
static const ImU32 k_ColStateText    = IM_COL32(220,230,255,  255);
static const ImU32 k_ColArrow        = IM_COL32(160,180,210,  255);
static const ImU32 k_ColArrowActive  = IM_COL32(80, 220, 120,  255);

void AnimatorControllerWindow::setAnimator(Animator* animator) {
    m_animator = animator ? animator : &m_internalAnimator;
    m_nodePositions.clear();
    m_selectedState.clear();
    if (!animator) return;

    float col = 0.0f;
    float row = 0.0f;
    float stepX = 180.0f;
    float stepY =  70.0f;
    int   n     = 0;

    for (auto& pair : animator->states) {
        m_nodePositions[pair.key.cStr()] = StateNodePos{col * stepX + 20.0f, row * stepY + 20.0f};
        ++n;
        col += 1.0f;
        if (n % 4 == 0) { col = 0.0f; row += 1.0f; }
    }
}

ImVec2 AnimatorControllerWindow::nodeCenter(const StateNodePos& pos) const {
    ImVec2 sz = stateNodeSize();
    return {pos.x + sz.x * 0.5f, pos.y + sz.y * 0.5f};
}

void AnimatorControllerWindow::render() {
    if (!m_open) return;

    ImGui::SetNextWindowSizeConstraints({500, 350}, {FLT_MAX, FLT_MAX});
    if (!ImGui::Begin("Animator Controller", &m_open)) {
        ImGui::End();
        return;
    }

    float inspectorWidth = 230.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    float canvasW = availW - inspectorWidth - 6.0f;

    ImGui::BeginChild("##anim_canvas_col", {canvasW, 0.0f}, false, ImGuiWindowFlags_NoScrollbar);
    renderCanvas();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##anim_inspector_col", {inspectorWidth, 0.0f});
    renderParameterPanel();
    ImGui::Separator();
    renderInspector();
    ImGui::EndChild();

    ImGui::End();
}

void AnimatorControllerWindow::renderCanvas() {
    ImDrawList* dl    = ImGui::GetWindowDrawList();
    ImVec2 origin     = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    ImGui::InvisibleButton("##canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool canvasHovered = ImGui::IsItemHovered();
    bool canvasClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    dl->AddRectFilled(origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, k_ColCanvas);

    float gridStep = 40.0f;
    for (float x = fmodf(m_canvasScrollX, gridStep); x < canvasSize.x; x += gridStep)
        dl->AddLine({origin.x + x, origin.y}, {origin.x + x, origin.y + canvasSize.y}, k_ColGrid);
    for (float y = fmodf(m_canvasScrollY, gridStep); y < canvasSize.y; y += gridStep)
        dl->AddLine({origin.x, origin.y + y}, {origin.x + canvasSize.x, origin.y + y}, k_ColGrid);

    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_canvasScrollX += delta.x;
        m_canvasScrollY += delta.y;
    }

    if (canvasClicked) ImGui::OpenPopup("##canvas_ctx");
    if (ImGui::BeginPopup("##canvas_ctx")) {
        if (ImGui::MenuItem("Add State")) m_addStatePopup = true;
        ImGui::EndPopup();
    }

    if (m_addStatePopup) {
        ImGui::OpenPopup("##new_state");
        m_addStatePopup = false;
    }
    if (ImGui::BeginPopupModal("##new_state", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("State name:");
        ImGui::InputText("##ns", m_newStateName, sizeof(m_newStateName));
        if (ImGui::Button("Add") && m_animator && m_newStateName[0] != '\0') {
            AnimationState s;
            s.name = m_newStateName;
            m_animator->states.set(FixedString<32>(m_newStateName), s);
            m_nodePositions[m_newStateName] = StateNodePos{80.0f, 80.0f};
            memset(m_newStateName, 0, sizeof(m_newStateName));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (!m_animator) {
        dl->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(100,100,120,200), "No animator assigned.");
        return;
    }

    dl->PushClipRect(origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, true);

    for (auto& fromPair : m_animator->states) {
        const FixedString<32>& fromName  = fromPair.key;
        const AnimationState&  fromState = fromPair.value;

        auto fromIt = m_nodePositions.find(fromName.cStr());
        if (fromIt == m_nodePositions.end()) continue;

        ImVec2 fc = nodeCenter(fromIt->second);
        ImVec2 fromCanvas{origin.x + fc.x + m_canvasScrollX, origin.y + fc.y + m_canvasScrollY};

        for (const auto& t : fromState.transitions) {
            auto toIt = m_nodePositions.find(t.toState.cStr());
            if (toIt == m_nodePositions.end()) continue;
            ImVec2 tc = nodeCenter(toIt->second);
            ImVec2 toCanvas{origin.x + tc.x + m_canvasScrollX, origin.y + tc.y + m_canvasScrollY};
            bool isActiveTransition = (m_animator->currentState == fromName &&
                                       m_animator->previousState == t.toState);
            drawTransitionArrow(fromCanvas, toCanvas, isActiveTransition);
        }
    }

    bool deletedNode = false;
    for (auto& nodePair : m_animator->states) {
        if (deletedNode) break;

        const FixedString<32>& name  = nodePair.key;
        AnimationState&        state = nodePair.value;

        auto posIt = m_nodePositions.find(name.cStr());
        if (posIt == m_nodePositions.end()) continue;

        StateNodePos& nodePos  = posIt->second;
        bool isActive   = (m_animator->currentState == name);
        bool isSelected = (m_selectedState == name.cStr());

        ImVec2 nMin{origin.x + nodePos.x + m_canvasScrollX,
                    origin.y + nodePos.y + m_canvasScrollY};
        ImVec2 sz = stateNodeSize();
        ImVec2 nMax{nMin.x + sz.x, nMin.y + sz.y};

        ImU32 bg = isActive ? k_ColStateActive : (isSelected ? k_ColStateSel : k_ColStateNormal);
        dl->AddRectFilled(nMin, nMax, bg, 6.0f);
        dl->AddRect(nMin, nMax, k_ColStateBorder, 6.0f, 0, isSelected ? 2.0f : 1.0f);

        dl->AddText({nMin.x + 8.0f, nMin.y + 8.0f}, k_ColStateText, name.cStr());
        if (state.clip) {
            char info[48];
            snprintf(info, sizeof(info), "%.0f fps", static_cast<float>(state.clip->fps));
            dl->AddText({nMin.x + 8.0f, nMin.y + 24.0f}, IM_COL32(150,180,150,200), info);
        }

        ImGui::SetCursorScreenPos(nMin);
        std::string btnId = "##node_" + std::string(name.cStr());
        ImGui::InvisibleButton(btnId.c_str(), sz);

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            m_selectedState = name.cStr();
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            nodePos.x += delta.x;
            nodePos.y += delta.y;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_selectedState = name.cStr();
            ImGui::OpenPopup(("##node_ctx_" + std::string(name.cStr())).c_str());
        }
        if (ImGui::BeginPopup(("##node_ctx_" + std::string(name.cStr())).c_str())) {
            if (ImGui::MenuItem("Set as Default")) {
                m_animator->currentState = name;
                m_animator->timeInState  = 0.0f;
            }
            if (ImGui::MenuItem("Add Transition")) {
                m_showAddTransition = true;
            }
            if (ImGui::MenuItem("Delete State")) {
                m_animator->states.remove(name);
                m_nodePositions.erase(name.cStr());
                m_selectedState.clear();
                deletedNode = true;
                ImGui::EndPopup();
                break;
            }
            ImGui::EndPopup();
        }
    }

    if (m_showAddTransition && !m_selectedState.empty()) {
        ImGui::OpenPopup("##add_transition");
        m_showAddTransition = false;
    }
    if (ImGui::BeginPopupModal("##add_transition", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("From: %s  ->  To:", m_selectedState.c_str());
        ImGui::InputText("##tt", m_transitionTo, sizeof(m_transitionTo));
        if (ImGui::Button("Add") && m_animator && m_transitionTo[0] != '\0') {
            AnimationState* fromState = m_animator->states.get(FixedString<32>(m_selectedState.c_str()));
            if (fromState) {
                AnimationTransition t;
                t.toState = m_transitionTo;
                fromState->transitions.push_back(t);
            }
            memset(m_transitionTo, 0, sizeof(m_transitionTo));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    dl->PopClipRect();
}

void AnimatorControllerWindow::drawTransitionArrow(ImVec2 from, ImVec2 to, bool isActive) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = isActive ? k_ColArrowActive : k_ColArrow;

    dl->AddLine(from, to, col, 1.5f);

    ImVec2 dir{to.x - from.x, to.y - from.y};
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
    if (len < 1.0f) return;
    dir.x /= len; dir.y /= len;

    ImVec2 mid{(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f};
    float aw = 7.0f;
    float ah = 5.0f;
    ImVec2 perp{-dir.y * ah, dir.x * ah};
    ImVec2 tip{mid.x + dir.x * aw, mid.y + dir.y * aw};
    ImVec2 bl{mid.x - dir.x * aw + perp.x, mid.y - dir.y * aw + perp.y};
    ImVec2 br{mid.x - dir.x * aw - perp.x, mid.y - dir.y * aw - perp.y};
    dl->AddTriangleFilled(tip, bl, br, col);
}

void AnimatorControllerWindow::renderInspector() {
    if (!m_animator || m_selectedState.empty()) {
        ImGui::TextDisabled("Select a state to inspect.");
        return;
    }

    AnimationState* state = m_animator->states.get(FixedString<32>(m_selectedState.c_str()));
    if (!state) return;

    ImGui::TextColored({0.4f,0.8f,1.0f,1.0f}, "%s", m_selectedState.c_str());
    ImGui::Separator();

    ImGui::Text("Speed:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("##spd", &state->speed, 0.01f, 0.0f, 10.0f);

    if (state->clip) {
        ImGui::Text("Clip: %s", state->clip->name.cStr());
        ImGui::Text("FPS: %u  Frames: %zu", state->clip->fps, state->clip->frames.size());
    } else {
        ImGui::TextDisabled("No clip assigned.");
    }

    ImGui::Spacing();
    ImGui::Text("Transitions (%zu):", state->transitions.size());
    for (usize i = 0; i < state->transitions.size(); ++i) {
        const auto& t = state->transitions[i];
        ImGui::BulletText("-> %s  blend=%.2f %s",
            t.toState.cStr(), t.blendTime,
            t.hasExitTime ? "[exit]" : "");
        if (!t.conditions.empty()) {
            for (const auto& c : t.conditions) {
                const char* opStr =
                    c.op == Animation::ConditionOperator::Equals         ? "==" :
                    c.op == Animation::ConditionOperator::NotEquals      ? "!=" :
                    c.op == Animation::ConditionOperator::Greater        ? ">"  :
                    c.op == Animation::ConditionOperator::Less           ? "<"  :
                    c.op == Animation::ConditionOperator::GreaterOrEqual ? ">=" : "<=";
                ImGui::Indent();
                ImGui::TextDisabled("%s %s", c.parameterName.cStr(), opStr);
                ImGui::Unindent();
            }
        }
    }
}

void AnimatorControllerWindow::renderParameterPanel() {
    if (!m_animator) return;

    ImGui::TextColored({0.9f,0.8f,0.4f,1.0f}, "Parameters");
    ImGui::Separator();

    for (auto& p : m_animator->parameters) {
        ImGui::PushID(p.name.cStr());
        switch (p.type) {
            case Animation::ParameterType::Bool: {
                bool v = p.boolValue;
                if (ImGui::Checkbox(p.name.cStr(), &v)) p.boolValue = v;
                break;
            }
            case Animation::ParameterType::Float: {
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat(p.name.cStr(), &p.floatValue, 0.01f);
                break;
            }
            case Animation::ParameterType::Int: {
                int v = p.intValue;
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::DragInt(p.name.cStr(), &v)) p.intValue = static_cast<i32>(v);
                break;
            }
            case Animation::ParameterType::Trigger: {
                if (ImGui::SmallButton(p.name.cStr())) p.triggered = true;
                ImGui::SameLine();
                ImGui::TextDisabled("[T]%s", p.triggered ? "*" : "");
                break;
            }
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    const char* paramTypes[] = {"Bool", "Float", "Int", "Trigger"};
    ImGui::InputText("##pname", m_newParamName, sizeof(m_newParamName));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    ImGui::Combo("##ptype", &m_newParamType, paramTypes, 4);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##param") && m_newParamName[0] != '\0') {
        m_animator->addParameter(m_newParamName, static_cast<Animation::ParameterType>(m_newParamType));
        memset(m_newParamName, 0, sizeof(m_newParamName));
    }
}

}

#endif
