#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_local;
layout(location = 0) out vec4 outColor;

void main() {
    float falloff;
    if (v_uv.x < -1000.0) {
        vec3 cam = vec3(v_uv.y, v_color.a, v_uv.x + 1000000.0);
        vec3 surf = v_local;
        vec3 ray = surf - cam;
        float rayLen = length(ray);
        if (rayLen < 1e-4) discard;
        vec3 dir = ray / rayLen;
        vec3 invDir = vec3(abs(dir.x) > 1e-5 ? 1.0 / dir.x : 1e5,
                           abs(dir.y) > 1e-5 ? 1.0 / dir.y : 1e5,
                           abs(dir.z) > 1e-5 ? 1.0 / dir.z : 1e5);
        vec3 tA = (vec3(-1.0) - surf) * invDir;
        vec3 tB = (vec3(1.0) - surf) * invDir;
        vec3 tFar = max(tA, tB);
        float thickness = max(min(tFar.x, min(tFar.y, tFar.z)), 0.0);
        float tClosest = clamp(-dot(surf, dir), 0.0, thickness);
        float radial = length(surf + dir * tClosest);
        falloff = exp(-radial * radial * 3.0) * clamp(thickness * 0.65, 0.0, 1.0);
        if (falloff <= 0.02) discard;
        outColor = vec4(v_color.rgb * falloff, falloff);
        return;
    } else if (v_uv.y > 1.5 && v_uv.x > 1.5) {
        float ax = abs(v_uv.x - 2.5) * 2.0;
        float ay = abs(v_uv.y - 2.5) * 2.0;
        falloff = 1.0 - smoothstep(0.55, 1.0, max(ax, ay));
    } else if (v_uv.y > 1.5) {
        float across = abs(v_uv.x * 2.0 - 1.0);
        falloff = 1.0 - smoothstep(0.2, 1.0, across);
    } else {
        vec2 p = v_uv * 2.0 - 1.0;
        falloff = 1.0 - smoothstep(0.15, 1.0, length(p));
    }
    if (falloff <= 0.001) discard;
    float alpha = v_color.a * falloff;
    outColor = vec4(v_color.rgb * alpha, alpha);
}
