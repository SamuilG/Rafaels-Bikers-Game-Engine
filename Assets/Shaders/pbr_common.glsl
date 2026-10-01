#ifndef STEER_PBR_COMMON_GLSL
#define STEER_PBR_COMMON_GLSL

const float PBR_PI = 3.14159265359;

// Perceptual roughness is squared once. The same mapping is used for direct
// lighting, environment prefiltering and the DFG lookup table.
float pbrAlpha(float roughness) {
    return max(roughness * roughness, 0.001);
}

float D_GGX(float alpha, float NoH) {
    float a2 = alpha * alpha;
    NoH = clamp(NoH, 0.0, 1.0);
    float d = (1.0 - NoH * NoH) + a2 * NoH * NoH;
    return a2 / (PBR_PI * d * d);
}

// Visibility includes G / (4 * NoL * NoV); do not divide by that term again.
float V_SmithGGXCorrelated(float alpha, float NoV, float NoL) {
    float a2 = alpha * alpha;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 0.000001);
}

vec2 pbrHammersley(uint i, uint count) {
    return vec2(float(i) / float(count), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

mat3 pbrBasis(vec3 N) {
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    return mat3(T, cross(N, T), N);
}

vec3 pbrSampleGGX(vec2 xi, float alpha, vec3 N) {
    float phi = 2.0 * PBR_PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    return normalize(pbrBasis(N) * vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta));
}

// A+B is the single-scattering directional albedo for F=1. Reuse the
// matching DFG integration to restore missing microfacet multiple scattering.
vec3 pbrEnergyCompensation(vec3 F0, vec2 dfg) {
    float energy = clamp(dfg.x + dfg.y, 0.05, 1.0);
    return vec3(1.0) + F0 * (1.0 / energy - 1.0);
}
#endif
