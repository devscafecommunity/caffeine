#version 450

// Infinite ground grid on y = 0, anti-aliased with screen-space derivatives.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform GridUBO {
    mat4 uInvViewProj;
    mat4 uViewProj;
    vec4 uCamera;    // xyz position, w fade distance
    vec4 uParams;    // x cell size, y major every, z opacity, w line width (px)
    vec4 uMinor;     // rgb minor line color (linear)
    vec4 uMajor;
    vec4 uAxisX;
    vec4 uAxisZ;
} grid;

float gridCoverage(vec2 coord, float widthPx) {
    vec2 deriv = max(fwidth(coord), vec2(1e-6));
    vec2 dist = abs(fract(coord - 0.5) - 0.5) / deriv;
    float line = min(dist.x, dist.y);
    // Lines thinner than a pixel fade instead of shimmering.
    float density = max(deriv.x, deriv.y);
    float fadeDense = 1.0 - smoothstep(0.25, 0.6, density);
    return (1.0 - smoothstep(widthPx * 0.5, widthPx * 0.5 + 1.0, line)) * fadeDense;
}

float axisCoverage(float coord, float widthPx) {
    float deriv = max(fwidth(coord), 1e-6);
    return 1.0 - smoothstep(widthPx * 0.5, widthPx * 0.5 + 1.0, abs(coord) / deriv);
}

void main() {
    vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, (1.0 - v_uv.y) * 2.0 - 1.0);
    vec4 nearH = grid.uInvViewProj * vec4(ndc, 0.0, 1.0);
    vec4 farH = grid.uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 nearP = nearH.xyz / nearH.w;
    vec3 farP = farH.xyz / farH.w;
    vec3 dir = farP - nearP;
    if (abs(dir.y) < 1e-6) discard;
    float t = -nearP.y / dir.y;
    if (t <= 0.0) discard;
    vec3 pos = nearP + dir * t;

    vec4 clip = grid.uViewProj * vec4(pos, 1.0);
    float depth = clip.z / clip.w;
    if (depth > 1.0) discard;
    gl_FragDepth = max(depth, 0.0);

    float cell = max(grid.uParams.x, 1e-4);
    float major = max(grid.uParams.y, 2.0);
    float widthPx = max(grid.uParams.w, 0.5);
    float minorA = gridCoverage(pos.xz / cell, widthPx);
    float majorA = gridCoverage(pos.xz / (cell * major), widthPx * 1.6);
    float axisX = axisCoverage(pos.z, widthPx * 2.4);   // X axis runs along z = 0
    float axisZ = axisCoverage(pos.x, widthPx * 2.4);

    vec3 color = grid.uMinor.rgb;
    float alpha = minorA * grid.uMinor.a;
    color = mix(color, grid.uMajor.rgb, majorA);
    alpha = max(alpha, majorA * grid.uMajor.a);
    color = mix(color, grid.uAxisX.rgb, axisX);
    alpha = max(alpha, axisX * grid.uAxisX.a);
    color = mix(color, grid.uAxisZ.rgb, axisZ);
    alpha = max(alpha, axisZ * grid.uAxisZ.a);

    float dist = length(pos - grid.uCamera.xyz);
    float fade = 1.0 - smoothstep(grid.uCamera.w * 0.35, grid.uCamera.w, dist);
    // Grazing angles alias no matter what; fade them out.
    vec3 viewDir = normalize(dir);
    fade *= smoothstep(0.02, 0.15, abs(viewDir.y));
    alpha *= fade * grid.uParams.z;
    if (alpha <= 0.002) discard;
    outColor = vec4(color, alpha);
}
