#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;

layout(set = 3, binding = 0) uniform Uniforms {
    mat4 uMVP;
    vec4 uAlbedo;
    float uMetallic;
    float uRoughness;
    float uReflectance;
    float uPad;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(vNormal);
    vec3 lightDir = normalize(vec3(0.35, 0.75, 0.55));
    float ndotl = max(dot(n, lightDir), 0.0);
    float diffuse = 0.25 + ndotl * 0.75;

    float specPower = max(4.0, (1.0 - ubo.uRoughness) * 96.0 + 8.0);
    vec3 viewDir = vec3(0.0, 0.0, 1.0);
    vec3 halfVec = normalize(lightDir + viewDir);
    vec3 F0 = mix(vec3(clamp(ubo.uReflectance, 0.0, 1.0)), ubo.uAlbedo.rgb, clamp(ubo.uMetallic, 0.0, 1.0));
    float spec = pow(max(dot(n, halfVec), 0.0), specPower);
    vec3 color = ubo.uAlbedo.rgb * diffuse * (1.0 - F0) + F0 * spec;
    outColor = vec4(color, 1.0);
}
