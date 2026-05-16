#include "editor/ShaderNode.hpp"
#include <imgui.h>
#include <string>
#include <cstdio>
#include <cstdarg>

namespace Caffeine::Editor {

// Simple format helper to avoid fmt dependency
static std::string str(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

static std::string floatToStr(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%gf", static_cast<double>(v));
    return buf;
}

static std::string vec4ToStr(const float c[4]) {
    char buf[128];
    snprintf(buf, sizeof(buf), "vec4(%gf, %gf, %gf, %gf)",
        static_cast<double>(c[0]), static_cast<double>(c[1]),
        static_cast<double>(c[2]), static_cast<double>(c[3]));
    return buf;
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
    return str("    vec4 var_%u = texture(sampler_%s, texCoord);", m_id, sampled.c_str());
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
    return str("    vec4 var_%u = %s;", m_id, vec4ToStr(color).c_str());
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
    return str("    float var_%u = %s;", m_id, floatToStr(value).c_str());
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
    return str("    float var_%u = %s * %s;", m_id, a.c_str(), b.c_str());
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
    return str("    float var_%u = %s + %s;", m_id, a.c_str(), b.c_str());
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
    return str("    vec4 var_%u = mix(%s, %s, %s);", m_id, from.c_str(), to.c_str(), t.c_str());
}

TimeNode::TimeNode(uint32_t id)
    : ShaderNode(id, NodeType::Time, "Time")
{
    m_outputs.push_back({"Time", PinType::Float, -1, false});
}

std::string TimeNode::generateCode(const std::vector<std::string>& inputVars) const {
    (void)inputVars;
    return str("    float var_%u = u_time;", m_id);
}

VertexPositionNode::VertexPositionNode(uint32_t id)
    : ShaderNode(id, NodeType::VertexPosition, "Vertex Position")
{
    m_outputs.push_back({"Pos", PinType::Vec3, -1, false});
}

std::string VertexPositionNode::generateCode(const std::vector<std::string>& inputVars) const {
    (void)inputVars;
    return str("    vec3 var_%u = v_position;", m_id);
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

    return str(
        "    float metallic_%u = %s;\n"
        "    float roughness_%u = %s;\n"
        "    // PBR Output: albedo=%s, metallic=%s, roughness=%s, normal=%s",
        m_id, metal.c_str(),
        m_id, rough.c_str(),
        albedo.c_str(), metal.c_str(), rough.c_str(), normal.c_str()
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
