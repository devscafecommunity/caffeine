#version 450

// Water surface: a flat grid uploaded once, displaced here by the Gerstner waves of the fluid (the
// same maths as Fluids::WaveField::sample, so the CPU `water_height` query agrees). Outputs match
// scene_lit.vert, plus `v_foam`: whitecaps where the surface compresses.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(set = 1, binding = 0) uniform WaterUBO {
    mat4 uMVP;
    mat4 uModel;
    vec4 uParams;       // x time (s), y wave count, z foam threshold
    vec4 uWaves[32];    // per wave: (dirX, dirZ, k, omega) then (amplitude, steepness, phase, 0)
} ubo;

layout(location = 0) out vec3 v_worldPos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_texCoord;
layout(location = 3) out vec3 v_tangent;
layout(location = 4) out vec3 v_bitangent;
layout(location = 5) out vec2 v_uvScale;
layout(location = 6) out vec4 v_color;
layout(location = 7) out float v_foam;

void main() {
    vec2 rest = aPosition.xz;
    float t = ubo.uParams.x;
    int count = int(ubo.uParams.y + 0.5);
    vec3 disp = vec3(0.0);
    vec3 dpdx = vec3(1.0, 0.0, 0.0);
    vec3 dpdz = vec3(0.0, 0.0, 1.0);
    float steep = 0.0;
    for (int i = 0; i < 16; ++i) {
        if (i >= count) break;
        vec4 a = ubo.uWaves[i * 2];
        vec4 b = ubo.uWaves[i * 2 + 1];
        vec2 dir = a.xy;
        float k = a.z;
        float theta = k * dot(dir, rest) - a.w * t + b.z;
        float s = sin(theta);
        float c = cos(theta);
        float qa = b.y * b.x;
        float ka = k * b.x;
        disp += vec3(qa * dir.x * c, b.x * s, qa * dir.y * c);
        dpdx += vec3(-qa * dir.x * dir.x * k * s, ka * dir.x * c, -qa * dir.x * dir.y * k * s);
        dpdz += vec3(-qa * dir.x * dir.y * k * s, ka * dir.y * c, -qa * dir.y * dir.y * k * s);
        steep += b.y * ka;
    }
    vec3 local = vec3(aPosition.x + disp.x, aPosition.y + disp.y, aPosition.z + disp.z);
    vec3 n = normalize(cross(dpdz, dpdx));
    float jacobian = dpdx.x * dpdz.z - dpdz.x * dpdx.z;
    float crest = smoothstep(0.06, 0.45, steep);
    float compressed = 1.0 - smoothstep(ubo.uParams.z - 0.12, ubo.uParams.z + 0.25, jacobian);
    v_foam = clamp(compressed * crest, 0.0, 1.0);

    vec4 worldPos = ubo.uModel * vec4(local, 1.0);
    v_worldPos = worldPos.xyz;
    mat3 m = mat3(ubo.uModel);
    vec3 N = normalize(m * n);
    vec3 T = normalize(m * normalize(dpdx));
    v_normal = N;
    v_tangent = T;
    v_bitangent = cross(N, T);
    v_texCoord = aTexCoord;
    v_uvScale = vec2(1.0);
    v_color = vec4(1.0);
    gl_Position = ubo.uMVP * vec4(local, 1.0);
}
