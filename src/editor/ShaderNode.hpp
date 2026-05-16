#pragma once
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include "containers/FixedString.hpp"
#include "containers/StringView.hpp"
#include <string>
#include <vector>
#include <memory>

namespace Caffeine::Editor {

enum class NodeType : u8 {
    TextureSample,
    ColorConstant,
    FloatConstant,
    Multiply,
    Add,
    Lerp,
    Time,
    VertexPosition,
    OutputPBR
};

enum class PinType : u8 {
    Float,
    Vec3,
    Vec4,
    Texture2D
};

static const char* pinTypeToString(PinType t) {
    switch (t) {
        case PinType::Float:     return "float";
        case PinType::Vec3:      return "vec3";
        case PinType::Vec4:      return "vec4";
        case PinType::Texture2D: return "texture2D";
    }
    return "unknown";
}

struct NodePin {
    std::string name;
    PinType type = PinType::Float;
    int connectionID = -1;
    bool isInput = true;
};

class ShaderNode {
public:
    ShaderNode(uint32_t id, NodeType type, std::string title)
        : m_id(id), m_type(type), m_title(std::move(title)) {}
    virtual ~ShaderNode() = default;

    uint32_t id() const { return m_id; }
    NodeType type() const { return m_type; }
    const std::string& title() const { return m_title; }

    std::vector<NodePin>& inputs() { return m_inputs; }
    std::vector<NodePin>& outputs() { return m_outputs; }
    const std::vector<NodePin>& inputs() const { return m_inputs; }
    const std::vector<NodePin>& outputs() const { return m_outputs; }

    virtual std::string generateCode(const std::vector<std::string>& inputVars) const = 0;
    virtual const char* outputVarType() const = 0;
    virtual void renderProperties() {}

protected:
    uint32_t m_id;
    NodeType m_type;
    std::string m_title;
    std::vector<NodePin> m_inputs;
    std::vector<NodePin> m_outputs;
};

// ── Concrete nodes ──

class TextureSampleNode final : public ShaderNode {
public:
    explicit TextureSampleNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "vec4"; }
    void renderProperties() override;
private:
    char m_texturePath[128] = {};
};

class ColorConstantNode final : public ShaderNode {
public:
    explicit ColorConstantNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "vec4"; }
    void renderProperties() override;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

class FloatConstantNode final : public ShaderNode {
public:
    explicit FloatConstantNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "float"; }
    void renderProperties() override;
    float value = 1.0f;
};

class MultiplyNode final : public ShaderNode {
public:
    explicit MultiplyNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override;
};

class AddNode final : public ShaderNode {
public:
    explicit AddNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override;
};

class LerpNode final : public ShaderNode {
public:
    explicit LerpNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "vec4"; }
};

class TimeNode final : public ShaderNode {
public:
    explicit TimeNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "float"; }
};

class VertexPositionNode final : public ShaderNode {
public:
    explicit VertexPositionNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "vec3"; }
};

class OutputPBRNode final : public ShaderNode {
public:
    explicit OutputPBRNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "void"; }
    void renderProperties() override;
};

// Factory
std::unique_ptr<ShaderNode> createNode(NodeType type, uint32_t id);

} // namespace Caffeine::Editor
