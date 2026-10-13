#ifndef STEER_TONEMAP_GLSL
#define STEER_TONEMAP_GLSL

// ACES-style fitted curve; evaluate once, after HDR effects and exposure.
vec3 toneMapAces(vec3 hdr, vec4 curve, float e) {
    vec3 x = max(hdr, vec3(0.0));
    vec4 p = max(curve, vec4(0.0));
    vec3 denominator = max(x * (p.z * x + p.w) + max(e, 0.0001), vec3(0.0001));
    return clamp((x * (p.x * x + p.y)) / denominator, 0.0, 1.0);
}
vec3 toneMapAces(vec3 hdr) {
    return toneMapAces(hdr, vec4(2.51, 0.03, 2.43, 0.59), 0.14);
}

vec3 linearToSrgb(vec3 color) {
    color = max(color, vec3(0.0));
    return mix(color * 12.92, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
        greaterThan(color, vec3(0.0031308)));
}

vec3 srgbToLinear(vec3 color) {
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)),
        greaterThan(color, vec3(0.04045)));
}
#endif
