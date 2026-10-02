#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 outColor;

void main() {
    float falloff;
    if (v_uv.y > 1.5 && v_uv.x > 1.5) {
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
