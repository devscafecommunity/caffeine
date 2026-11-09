#version 450

// Camera motion blur: per-pixel velocity from depth reprojection with last frame's view-projection.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform MotionUBO {
    mat4 uInvViewProj;
    mat4 uPrevViewProj;
    vec4 uParams; // x intensity (shutter fraction), y sample count, z max length (uv)
} motion;

layout(set = 2, binding = 0) uniform sampler2D uScene;
layout(set = 2, binding = 1) uniform sampler2D uDepth;

void main() {
    float d = textureLod(uDepth, v_uv, 0.0).r;
    vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, (1.0 - v_uv.y) * 2.0 - 1.0);
    vec4 world = motion.uInvViewProj * vec4(ndc, d, 1.0);
    world /= world.w;
    vec4 prev = motion.uPrevViewProj * world;
    vec3 color = texture(uScene, v_uv).rgb;
    if (prev.w <= 1e-4) {
        outColor = vec4(color, 1.0);
        return;
    }
    vec2 prevUv = vec2(prev.x / prev.w * 0.5 + 0.5, 1.0 - (prev.y / prev.w * 0.5 + 0.5));
    vec2 velocity = (v_uv - prevUv) * motion.uParams.x;
    float len = length(velocity);
    if (len > motion.uParams.z) velocity *= motion.uParams.z / len;
    int samples = int(clamp(motion.uParams.y, 2.0, 32.0));
    vec3 sum = color;
    for (int i = 1; i < 32; ++i) {
        if (i >= samples) break;
        float t = float(i) / float(samples - 1) - 0.5;
        sum += texture(uScene, v_uv + velocity * t).rgb;
    }
    outColor = vec4(sum / float(samples), 1.0);
}
