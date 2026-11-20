#ifndef STEER_PORTAL_GRADE_GLSL
#define STEER_PORTAL_GRADE_GLSL

vec3 gradePortalColor(vec3 color) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    // Keep the black point anchored while the exposure opens up through the midtones.
    float exposure = mix(1.5, 3.25, smoothstep(0.0, 0.05, luminance));
    float brightness = 1.0 - exp(-luminance * exposure);

    // Cyan-green shadows roll into near-white light, with some source color in bright surfaces.
    float highlights = smoothstep(0.29, 0.74, luminance);
    float nearWhite = smoothstep(0.64, 0.98, luminance);
    vec3 tint = mix(vec3(0.69, 1.125, 0.95), vec3(1.045, 1.05, 1.015), highlights);
    vec3 chroma = (color - luminance) * mix(0.15, 0.33, highlights) * (1.0 - 0.8 * nearWhite);
    return clamp(brightness * tint + chroma, 0.0, 1.0);
}

#endif
