#version 450
#extension GL_GOOGLE_include_directive : require
#include "pbr_common.glsl"

layout(set = 0, binding = 0) uniform samplerCube uEnvironment;
layout(location = 0) out vec4 oColor;
layout(push_constant) uniform BakeParams {
    int face;
    int mode; // 0: GGX prefilter, 1: diffuse convolution, 2: DFG integration
    float roughness;
    uint sampleCount;
    float size;
} pc;

vec3 cubeDirection(vec2 uv, int face) {
    vec2 p = uv * 2.0 - 1.0;
    if (face == 0) return normalize(vec3( 1.0, -p.y, -p.x));
    if (face == 1) return normalize(vec3(-1.0, -p.y,  p.x));
    if (face == 2) return normalize(vec3( p.x,  1.0,  p.y));
    if (face == 3) return normalize(vec3( p.x, -1.0, -p.y));
    if (face == 4) return normalize(vec3( p.x, -p.y,  1.0));
    return normalize(vec3(-p.x, -p.y, -1.0));
}

vec3 prefilter(vec3 N) {
    if (pc.roughness < 0.0001) return textureLod(uEnvironment, N, 0.0).rgb;
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    float alpha = pbrAlpha(pc.roughness);
    // Split-sum approximation assumes V=N for the environment convolution.
    for (uint i = 0u; i < pc.sampleCount; ++i) {
        vec3 H = pbrSampleGGX(pbrHammersley(i, pc.sampleCount), alpha, N);
        vec3 L = normalize(2.0 * dot(N, H) * H - N);
        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0) {
            sum += textureLod(uEnvironment, L, 0.0).rgb * NoL;
            weight += NoL;
        }
    }
    return sum / max(weight, 0.0001);
}

vec3 diffuseConvolution(vec3 N) {
    vec3 sum = vec3(0.0);
    mat3 basis = pbrBasis(N);
    // Cosine-weighted sampling stores irradiance / pi: a constant environment
    // stays constant, and runtime diffuse evaluation needs no extra pi factor.
    for (uint i = 0u; i < pc.sampleCount; ++i) {
        vec2 xi = pbrHammersley(i, pc.sampleCount);
        float phi = 2.0 * PBR_PI * xi.x;
        float radius = sqrt(xi.y);
        vec3 L = basis * vec3(radius * cos(phi), radius * sin(phi), sqrt(1.0 - xi.y));
        sum += textureLod(uEnvironment, L, 0.0).rgb;
    }
    return sum / float(pc.sampleCount);
}

vec2 integrateDfg(float NoV, float roughness) {
    vec3 N = vec3(0.0, 0.0, 1.0);
    vec3 V = vec3(sqrt(max(1.0 - NoV * NoV, 0.0)), 0.0, NoV);
    float alpha = pbrAlpha(roughness);
    vec2 sum = vec2(0.0);
    for (uint i = 0u; i < pc.sampleCount; ++i) {
        vec3 H = pbrSampleGGX(pbrHammersley(i, pc.sampleCount), alpha, N);
        float VoH = max(dot(V, H), 0.0);
        vec3 L = normalize(2.0 * VoH * H - V);
        float NoL = max(L.z, 0.0);
        if (NoL > 0.0) {
            float visibility = V_SmithGGXCorrelated(alpha, NoV, NoL);
            float weight = 4.0 * visibility * NoL * VoH / max(H.z, 0.0001);
            float Fc = pow(1.0 - VoH, 5.0);
            sum += vec2(1.0 - Fc, Fc) * weight;
        }
    }
    return sum / float(pc.sampleCount);
}

void main() {
    vec2 uv = gl_FragCoord.xy / pc.size;
    if (pc.mode == 2) {
        oColor = vec4(integrateDfg(max(uv.x, 0.0001), uv.y), 0.0, 1.0);
    } else {
        vec3 N = cubeDirection(uv, pc.face);
        oColor = vec4(pc.mode == 0 ? prefilter(N) : diffuseConvolution(N), 1.0);
    }
}
