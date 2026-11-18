#version 450

// Alpha-tested shadow: cut-out cards (leaves, petals, grass) only shadow where they are opaque.
layout(location = 0) in float v_depth;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out float outDepth;

layout(set = 2, binding = 0) uniform sampler2D uAlbedoMap;
layout(set = 3, binding = 0) uniform CutoutUBO {
    vec4 uCutout; // x alpha cutoff
    vec4 uUv;     // xy tiling, zw offset
} cutout;

void main() {
    float alpha = texture(uAlbedoMap, v_uv * cutout.uUv.xy + cutout.uUv.zw).a;
    if (alpha < cutout.uCutout.x) discard;
    outDepth = v_depth;
}
