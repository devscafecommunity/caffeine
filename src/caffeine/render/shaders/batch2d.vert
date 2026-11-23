#version 450

// Batch renderer, vertex stage: 2D sprite and rig triangles already positioned in world space.
// The view-projection is pushed on the vertex stage (set 1, binding 0), like the scene's vertex UBOs.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

layout(set = 1, binding = 0) uniform BatchUBO {
    mat4 uViewProj;
} batch;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;

void main() {
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = batch.uViewProj * vec4(aPosition, 1.0);
}
