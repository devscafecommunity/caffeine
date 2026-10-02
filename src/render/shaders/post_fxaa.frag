#version 450

// FXAA 3.11 (quality preset ~12), luma in alpha written by the composite pass.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform FxaaUBO {
    vec4 uTexel;  // xy texel size
    vec4 uParams; // x subpixel quality, y edge threshold, z edge threshold min
} fxaa;

layout(set = 2, binding = 0) uniform sampler2D uInput;

float lumaAt(vec2 uv) { return textureLod(uInput, uv, 0.0).a; }

void main() {
    vec2 t = fxaa.uTexel.xy;
    vec4 center = textureLod(uInput, v_uv, 0.0);
    float lumaM = center.a;
    float lumaS = lumaAt(v_uv + vec2(0.0, t.y));
    float lumaN = lumaAt(v_uv + vec2(0.0, -t.y));
    float lumaE = lumaAt(v_uv + vec2(t.x, 0.0));
    float lumaW = lumaAt(v_uv + vec2(-t.x, 0.0));

    float maxLuma = max(lumaM, max(max(lumaS, lumaN), max(lumaE, lumaW)));
    float minLuma = min(lumaM, min(min(lumaS, lumaN), min(lumaE, lumaW)));
    float range = maxLuma - minLuma;
    if (range < max(fxaa.uParams.z, maxLuma * fxaa.uParams.y)) {
        outColor = vec4(center.rgb, 1.0);
        return;
    }

    float lumaNW = lumaAt(v_uv + vec2(-t.x, -t.y));
    float lumaNE = lumaAt(v_uv + vec2(t.x, -t.y));
    float lumaSW = lumaAt(v_uv + vec2(-t.x, t.y));
    float lumaSE = lumaAt(v_uv + vec2(t.x, t.y));

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float subpixNSWE = lumaNS + lumaWE;
    float lumaNWNE = lumaNW + lumaNE;
    float lumaSWSE = lumaSW + lumaSE;
    float lumaNWSW = lumaNW + lumaSW;
    float lumaNESE = lumaNE + lumaSE;

    float edgeHorz = abs(-2.0 * lumaW + lumaNWSW) + abs(-2.0 * lumaM + lumaNS) * 2.0 +
                     abs(-2.0 * lumaE + lumaNESE);
    float edgeVert = abs(-2.0 * lumaS + lumaSWSE) + abs(-2.0 * lumaM + lumaWE) * 2.0 +
                     abs(-2.0 * lumaN + lumaNWNE);
    bool horzSpan = edgeHorz >= edgeVert;

    float subpixA = subpixNSWE * 2.0 + lumaNWNE + lumaSWSE;
    float lengthSign = horzSpan ? t.y : t.x;
    float luma1 = horzSpan ? lumaN : lumaW;
    float luma2 = horzSpan ? lumaS : lumaE;
    float gradient1 = abs(luma1 - lumaM);
    float gradient2 = abs(luma2 - lumaM);
    bool steeper1 = gradient1 >= gradient2;
    float gradientScaled = 0.25 * max(gradient1, gradient2);
    if (steeper1) lengthSign = -lengthSign;
    float lumaLocalAvg = steeper1 ? 0.5 * (luma1 + lumaM) : 0.5 * (luma2 + lumaM);

    vec2 posB = v_uv;
    if (horzSpan) posB.y += lengthSign * 0.5;
    else posB.x += lengthSign * 0.5;
    vec2 offNP = horzSpan ? vec2(t.x, 0.0) : vec2(0.0, t.y);

    vec2 posN = posB - offNP;
    vec2 posP = posB + offNP;
    float lumaEndN = lumaAt(posN) - lumaLocalAvg;
    float lumaEndP = lumaAt(posP) - lumaLocalAvg;
    bool doneN = abs(lumaEndN) >= gradientScaled;
    bool doneP = abs(lumaEndP) >= gradientScaled;
    const float kSteps[10] = float[10](1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 4.0, 8.0, 8.0, 12.0);
    for (int i = 0; i < 10 && !(doneN && doneP); ++i) {
        if (!doneN) {
            posN -= offNP * kSteps[i];
            lumaEndN = lumaAt(posN) - lumaLocalAvg;
            doneN = abs(lumaEndN) >= gradientScaled;
        }
        if (!doneP) {
            posP += offNP * kSteps[i];
            lumaEndP = lumaAt(posP) - lumaLocalAvg;
            doneP = abs(lumaEndP) >= gradientScaled;
        }
    }

    float dstN = horzSpan ? v_uv.x - posN.x : v_uv.y - posN.y;
    float dstP = horzSpan ? posP.x - v_uv.x : posP.y - v_uv.y;
    bool lumaMLTZero = (lumaM - lumaLocalAvg) < 0.0;
    bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
    bool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
    float spanLength = dstP + dstN;
    bool directionN = dstN < dstP;
    float dst = min(dstN, dstP);
    bool goodSpan = directionN ? goodSpanN : goodSpanP;
    float pixelOffset = goodSpan ? (-dst / spanLength + 0.5) : 0.0;

    float subpixC = clamp(abs(subpixA * (1.0 / 12.0) - lumaM) / range, 0.0, 1.0);
    float subpixH = (-2.0 * subpixC + 3.0) * subpixC * subpixC;
    float subpix = subpixH * subpixH * fxaa.uParams.x;
    float offset = max(pixelOffset, subpix);

    vec2 finalUv = v_uv;
    if (horzSpan) finalUv.y += offset * lengthSign;
    else finalUv.x += offset * lengthSign;
    outColor = vec4(textureLod(uInput, finalUv, 0.0).rgb, 1.0);
}
