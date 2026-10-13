#ifndef STEER_ENVIRONMENT_GLSL
#define STEER_ENVIRONMENT_GLSL
vec3 environmentDirection(vec3 direction, float rotation) {
    // Sample the inverse rotation of the environment around world up.
    float c = cos(rotation);
    float s = sin(rotation);
    return vec3(c * direction.x - s * direction.z, direction.y,
                s * direction.x + c * direction.z);
}
vec3 environmentColor(vec3 radiance, vec4 tintSaturation) {
    float luminance = dot(radiance, vec3(0.2126, 0.7152, 0.0722));
    vec3 color = mix(vec3(luminance), radiance, max(tintSaturation.w, 0.0));
    return max(color, vec3(0.0)) * max(tintSaturation.rgb, vec3(0.0));
}
#endif
