#version 450

layout(location = 0) in float v_depth;
layout(location = 0) out float outDepth;

void main() {
    outDepth = v_depth;
}
