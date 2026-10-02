#version 450

// One triangle covering the viewport; no vertex buffer. Depth sits on the far plane so the
// sky pass only fills pixels the scene left empty (LESS_OR_EQUAL against a cleared 1.0).
layout(location = 0) out vec2 v_uv;

void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}
