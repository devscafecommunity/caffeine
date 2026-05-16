#include "editor/ShaderNode.hpp"
#include <imgui.h>
#include <fmt/format.h>

namespace Caffeine::Editor {

static std::string floatToStr(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%gf", static_cast<double>(v));
    return buf;
}

static std::string vec4ToStr(const float c[4]) {
    return fmt::format("vec4({}f, {}f, {}f, {}f)", c[0], c[1], c[2], c[3]);
}

TextureSampleNode::TextureSampleNode(uint32_t id)
    : ShaderNode(id, NodeType::TextureSample, "Texture Sample")
{
    m_inputs.push_back({"UV", PinType::Vec4, -1, true});
    m_outputs.push_back({"RGBA", PinType::Vec4, -1, false});
}

std::string TextureSampleNode::generateCode(const std::vector<std::string>& inputVars) const {
    (void)inputVars;
    std::string sampled = m_texturePath[0] ? m_texturePath : "u_texture";
    return fmt::format("    vec4 var_{} = texture(sampler_{}, texCoord);", m_id, sampled);
}

void TextureSampleNode::renderProperties() {
    ImGui::InputText("Path", m_texturePath, sizeof(m_texturePath));
}

ColorConstantNode::ColorConstantNode(uint32_t id)
    : ShaderNode(id, NodeType::ColorConstant, "Color")
{
    m_outputs.push_back({"RGBA", PinType::Vec4, -1, false});
}

std::string ColorConstantNode::generateCode(const std::vector<std::string>& inputVars) const {
    (void)inputVars;
    return fmt::format("    vec4 var_{} = {};", m_id, vec4ToStr(color));
}

void ColorConstantNode::renderProperties() {
    ImGui::ColorEdit4("Color", color);
}

FloatConstantNode::FloatConstantNode(uint32_t id)
    : ShaderNode(id, NodeType::FloatConstant, "Float")
{
    m_outputs.push_back({"Value", PinType::Float, -1, false});
}

std::string FloatConstantNode::generateCode(const std::vector<std::string>& inputVars) const {
    (void)inputVars;
    return fmt::format("    float var_{} = {}f;", m_id, floatToStr(value));
}

void FloatConstantNode::renderProperties() {
    ImGui::SliderFloat("Value", &value, 0.0f, 10.0f);
}

MultiplyNode::MultiplyNode(uint32_t id)
    : ShaderNode(id, NodeType::Multiply, "Multiply")
{
    m_inputs.push_back({"A", PinType::Float, -1, true});
    m_inputs.push_back({"B", PinType::Float, -1, true});
    m_outputs.push_back({"Out", PinType::Float, -1, false});
}

std::string MultiplyNode::generateCode(const std::vector<std::string>& inputVars) const {
    std::string a = inputVars.size() > 0 && !inputVars[0].empty() ? inputVars[0] : "1.0f";
    std::string b = inputVars.size() > 1 && !inputVars[1].empty() ? inputVars[1] : "1.0f";
    return fmt::format("    float var_{} = {} * {};", m_id, a, b);
}

const char* MultiplyNode::outputVarType() const { return "float"; }

AddNode::AddNode(uint32_t id)
    : ShaderNode(id, NodeType::Add, "Add")
{
    m_inputs.push_back({"A", PinType::Float, -1, true});
    m_inputs.push_back({"B", PinType::Float, -1, true});
    m_outputs.push_back({"Out", PinType::Float, -1, false});
}

std::string AddNode::generateCode(const std::vector<std::string>& inputVars) const {
    std::string a = inputVars.size() > 0 && !inputVars[0].empty() ? inputVars[0] : "0.0f";
    std::string b = inputVars.size() > 1 && !inputVars[1].empty() ? inputVars[1] : "0.0f";
    return fmt::format("    float var_{} = {} + {};", m_id, a, b);
}

const char* AddNode::outputVarType() const { return "float"; }

LerpNode::LerpNode(uint32_t id)
    : ShaderNode(id, NodeType::Lerp, "Lerp")
{
    m_inputs.push_back({"From", PinType::Vec4, -1, true});
    m_inputs.push_back({"To", PinType::Vec4, -1, true});
    m_inputs.push_back({"T", PinType::Float, -1, true});
    m_outputs.push_back({"Out", PinType::Vec4, -1, false});
}

std::string LerpNode::generateCode(const std::vector<std::string>& inputVars) const {
    std::string from = inputVars.size() > 0 && !inputVars[0].empty() ? inputVars[0] : "vec4(0.0f)";
    std::string to   = inputVars.size() > 1 && !inputVars[1].empty() ? inputVars[1] : "vec4(1.0f)";
    std::string t    = inputVars.size() > 2 && !inputVars[2].empty() ? inputVars[2] : "0.5f";
    return fmt::format("    vec4 var_{} = mix({}, {}, {});", m_id, from, to, t);
}

TimeNode::TimeNode(uint32_t id)
    : ShaderNode(id, NodeType::Time, "Time")
{
    m_outputs.push_back({"Time", PinType::Float, -1, false});
}

std::string TimeNode::generateCode(const std::vector<std::string>& inputVars) const {
    (void)inputVars;
    return fmt::format("    float var_{} = u_time;", m_id);
}

VertexPositionNode::VertexPositionNode(uint32_t id)
    : ShaderNode(id, NodeType::VertexPosition, "Vertex Position")
{
    m_outputs.push_back({"Pos", PinType::Vec3, -1, false});
}

std::string VertexPositionNode::generateCode(const std::vector<std::string>& inputVars) const {
    (void)inputVars;
    return fmt::format("    vec3 var_{} = v_position;", m_id);
}

OutputPBRNode::OutputPBRNode(uint32_t id)
    : ShaderNode(id, NodeType::OutputPBR, "PBR Output")
{
    m_inputs.push_back({"Albedo", PinType::Vec4, -1, true});
    m_inputs.push_back({"Normal", PinType::Vec3, -1, true});
    m_inputs.push_back({"Metallic", PinType::Float, -1, true});
    m_inputs.push_back({"Roughness", PinType::Float, -1, true});
}

std::string OutputPBRNode::generateCode(const std::vector<std::string>& inputVars) const {
    std::string albedo  = inputVars.size() > 0 && !inputVars[0].empty() ? inputVars[0] : "vec4(1.0f)";
    std::string normal  = inputVars.size() > 1 && !inputVars[1].empty() ? inputVars[1] : "vec3(0.0f, 0.0f, 1.0f)";
    std::string metal   = inputVars.size() > 2 && !inputVars[2].empty() ? inputVars[2] : "0.0f";
    std::string rough   = inputVars.size() > 3 && !inputVars[3].empty() ? inputVars[3] : "0.5f";

    return fmt::format(
        "    float metallic_{} = {};\n"
        "    float roughness_{} = {};\n"
        "    // PBR Output: albedo={}, metallic={}, roughness={}, normal={}",
        m_id, metal,
        m_id, rough,
        albedo, metal, rough, normal
    );
}

void OutputPBRNode::renderProperties() {
    ImGui::TextUnformatted("PBR Material Output (end point)");
}

std::unique_ptr<ShaderNode> createNode(NodeType type, uint32_t id) {
    switch (type) {
        case NodeType::TextureSample:   return std::make_unique<TextureSampleNode>(id);
        case NodeType::ColorConstant:   return std::make_unique<ColorConstantNode>(id);
        case NodeType::FloatConstant:   return std::make_unique<FloatConstantNode>(id);
        case NodeType::Multiply:        return std::make_unique<MultiplyNode>(id);
        case NodeType::Add:             return std::make_unique<AddNode>(id);
        case NodeType::Lerp:            return std::make_unique<LerpNode>(id);
        case NodeType::Time:            return std::make_unique<TimeNode>(id);
        case NodeType::VertexPosition:  return std::make_unique<VertexPositionNode>(id);
        case NodeType::OutputPBR:       return std::make_unique<OutputPBRNode>(id);
    }
    return nullptr;
}

} // namespace Caffeine::Editor
