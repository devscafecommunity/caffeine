#version 450

// Batch renderer, fragment stage: the texture (white when the mesh is untextured) multiplied by the vertex tint.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uTexture;

void main() {
    outColor = texture(uTexture, vTexCoord) * vColor;
}
