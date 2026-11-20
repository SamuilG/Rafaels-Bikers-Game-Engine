#version 450

#include "tonemap.glsl"
#include "portal_grade.glsl"

layout(set = 0, binding = 0) uniform sampler2D sceneDepth;
layout(set = 0, binding = 3) uniform sampler2D sceneColor;
layout(set = 1, binding = 0) uniform sampler2D groundBaseColor;
struct SceneLight {
    vec4 position;
    vec4 color;
    vec4 direction;
    vec4 params;
};
layout(std140, set = 2, binding = 0) uniform SceneCamera {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 position;
    SceneLight lights[16];
    vec4 legacyLightPosition;
    vec4 legacyLightColor;
    uint lightCount;
    layout(offset = 1552) vec4 portalClipPlane;
} camera;
layout(push_constant) uniform GroundWave {
    mat4 viewProjection;
    vec4 originRadius;
    vec4 shape;
    vec4 grid;
    vec4 baseColorFactor;
} wave;

layout(location = 0) in vec3 baseWorld;
layout(location = 1) in vec3 displacedWorld;
layout(location = 2) flat in vec3 faceNormal;
layout(location = 3) flat in vec3 facet;
layout(location = 4) in vec2 groundUv;
layout(location = 5) in float edgeDistance;
layout(location = 0) out vec4 outputColor;

float depthAt(vec2 uv) {
    ivec2 size = textureSize(sceneDepth, 0);
    ivec2 pixel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    return texelFetch(sceneDepth, pixel, 0).r;
}

float viewZ(float depth) {
    return (depth * camera.projection[3][3] - camera.projection[3][2]) /
        (camera.projection[2][2] - depth * camera.projection[2][3]);
}

vec3 sourceMaterial(vec3 color) {
    vec3 hdr = color * wave.baseColorFactor.rgb;
    return wave.grid.y > 1.5 ? hdr : toneMapAces(hdr * wave.grid.w);
}

float tiltShading() {
    vec3 direction = normalize(vec3(-0.53, 0.66, 0.48));
    float brightest = 0.0;
    for (uint i = 0; i < min(camera.lightCount, 16u); ++i) {
        const SceneLight light = camera.lights[i];
        if (light.position.w == 0.0 && light.color.a > brightest &&
            dot(light.position.xyz, light.position.xyz) > 0.0001) {
            direction = normalize(light.position.xyz);
            brightest = light.color.a;
        }
    }
    vec3 originalNormal = normalize(cross(dFdx(baseWorld), dFdy(baseWorld)));
    originalNormal *= originalNormal.y >= 0.0 ? 1.0 : -1.0;
    float original = 0.44 + 0.78 * max(0.0, dot(originalNormal, direction));
    float lifted = 0.44 + 0.78 * max(0.0, dot(normalize(faceNormal), direction));
    return clamp(lifted / original, 0.58, 1.22);
}

vec3 groundTop() {
    vec4 texel = texture(groundBaseColor, groundUv);
    float shading = tiltShading();
    if (texel.a < wave.baseColorFactor.a) discard;

    // Reuse the lit ground sample only when it is the original surface.
    // Its authored texture and UV remain available behind occluders and offscreen.
    bool encoded = wave.grid.y < 0.5;
    vec4 clip = wave.viewProjection * vec4(baseWorld, 1.0);
    if (clip.w > 0.0) {
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)))) {
            float depth = depthAt(uv);
            float originalZ = (camera.view * vec4(baseWorld, 1.0)).z;
            if (depth < 1.0 && abs(viewZ(depth) - originalZ) < max(0.065, abs(originalZ) * 0.010)) {
                vec3 color = textureLod(sceneColor, uv, 0.0).rgb;
                if (encoded) color = srgbToLinear(color);
                color *= shading;
                return encoded ? linearToSrgb(color) : color;
            }
        }
    }

    vec3 color = sourceMaterial(texel.rgb * (0.82 * shading));
    return encoded ? linearToSrgb(color) : color;
}

vec2 visibleRefractionUv(vec2 candidate, vec2 fallback, vec2 margin) {
    vec2 sampleUv = clamp(candidate, margin, 1.0 - margin);
    // Every color sample must stay behind the slab.
    return depthAt(sampleUv) + 0.000025 < gl_FragCoord.z ? fallback : sampleUv;
}

vec3 fractureRefraction(vec3 ray, vec3 viewNormal, float grazing) {
    vec2 extent = vec2(textureSize(sceneColor, 0));
    vec2 margin = 0.5 / extent;
    vec2 uv = gl_FragCoord.xy / extent;
    vec3 transmitted = refract(ray, viewNormal, 1.0 / 1.46);
    float thickness = clamp(wave.grid.z / 0.16, 0.0, 2.0);
    vec2 bend = (transmitted - ray).xy * vec2(extent.y / extent.x, -1.0) * (0.057 * thickness);
    float dispersion = mix(0.07, 0.23, grazing) * mix(0.65, 1.0, facet.x);
    vec2 middle = visibleRefractionUv(uv + bend, uv, margin);
    vec2 red = visibleRefractionUv(uv + bend * (1.0 - dispersion), middle, margin);
    vec2 blue = visibleRefractionUv(uv + bend * (1.0 + dispersion), middle, margin);
    return vec3(
        textureLod(sceneColor, red, 0.0).r,
        textureLod(sceneColor, middle, 0.0).g,
        textureLod(sceneColor, blue, 0.0).b);
}

void main() {
    if (dot(camera.portalClipPlane, vec4(displacedWorld, 1.0)) < 0.0) discard;
    bool encoded = wave.grid.y < 0.5;
    if (facet.y == 3.0) {
        vec4 texel = texture(groundBaseColor, groundUv);
        if (texel.a < wave.baseColorFactor.a) discard;
        float opening = facet.z;
        float width = mix(0.011, 0.078, opening);
        float feather = clamp(fwidth(edgeDistance), 0.006, 0.055);
        float fissure = 1.0 - smoothstep(width, width + feather, edgeDistance);
        float opacity = max(fissure * facet.x * 0.91, opening * 0.97);
        vec3 material = sourceMaterial(texel.rgb);
        vec3 color = mix(vec3(0.006, 0.009, 0.011), material, mix(0.06, 0.18, fissure));
        outputColor = vec4(encoded ? linearToSrgb(color) : color, opacity);
        return;
    }

    float opacity = smoothstep(0.018, 0.27, facet.x);
    vec3 ray = normalize((camera.view * vec4(displacedWorld, 1.0)).xyz);
    vec3 viewNormal = normalize(mat3(camera.view) * faceNormal);
    float grazing = 1.0 - clamp(abs(dot(ray, viewNormal)), 0.0, 1.0);
    bool top = facet.y == 0.0;
    vec3 linear;
    if (top) {
        vec3 color = groundTop();
        linear = encoded ? srgbToLinear(color) : color;
        linear *= mix(1.0 - 0.13 * facet.z, 1.0, smoothstep(0.0, 0.085, edgeDistance));
    } else {
        vec4 texel = texture(groundBaseColor, groundUv);
        if (texel.a < wave.baseColorFactor.a) discard;
        vec3 material = sourceMaterial(texel.rgb);
        float lighting = clamp(dot(faceNormal, vec3(-0.23, 0.46, 0.28)) + 0.45, 0.0, 1.0);
        vec3 cut = material * (facet.y == 1.0 ? mix(0.13, 0.33, lighting) : 0.09);
        vec3 refracted = fractureRefraction(ray, viewNormal, grazing);
        if (encoded) refracted = srgbToLinear(refracted);
        linear = mix(cut, refracted, mix(0.09, 0.22, grazing));
    }
    float tint = opacity * (top ? mix(0.05, 0.15, grazing) : mix(0.11, 0.26, grazing));
    linear = mix(linear, gradePortalColor(linear), tint);
    outputColor = vec4(encoded ? linearToSrgb(linear) : linear, opacity);
}
