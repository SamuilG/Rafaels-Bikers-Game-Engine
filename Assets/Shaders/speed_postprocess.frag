#version 450 core

#include "tonemap.glsl"
#include "portal_grade.glsl"
#include "portal_wave_mask.glsl"

layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D screenTexture;
layout(set = 1, binding = 0) uniform sampler2D sceneDepth;
layout(set = 1, binding = 1) uniform sampler2D waveCells;

layout(push_constant) uniform PushConstants {
    float speedFactor;
    float deathFactor;
    float portalStrength;
    float portalTime;
    float srgbOutput;
    mat4 waveWorldFromClip;
    vec4 waveOriginRadius;
    vec4 waveShape; // width, strength, world texture repeats per metre, unused
} pc;

void main() {
    const vec2 center = vec2(0.5);
    const vec3 luminanceWeights = vec3(0.299, 0.587, 0.114);
    vec2 dir = TexCoords - center;
    float dist = length(dir);
    float speedAmount = pc.speedFactor * smoothstep(0.20, 0.40, dist);

    float distortion = -0.8 * speedAmount;
    vec2 distortedUV = center + dir * (1.0 - distortion * dist * dist);
    vec2 chroma = dir * (0.04 * speedAmount);

    if (pc.portalStrength > 0.0) {
        vec2 extent = vec2(textureSize(screenTexture, 0));
        vec2 aspect = vec2(extent.x / extent.y, 1.0);
        vec2 centered = dir * aspect;
        float radius = length(centered);
        vec2 outward = centered / max(radius, 0.001);
        float edge = smoothstep(0.12, 0.67, radius);
        float wave = sin(radius * 20.0 - pc.portalTime * 3.2);
        vec2 ripple = vec2(sin(centered.y * 18.0 + pc.portalTime * 1.8),
            cos(centered.x * 14.0 - pc.portalTime * 2.1));
        distortedUV += (wave * outward * 0.004 + ripple * 0.002) * edge * pc.portalStrength / aspect;
        chroma += outward * ((0.001 + 0.001 * wave) * edge * pc.portalStrength) / aspect;
    }

    vec2 uvR = distortedUV + chroma;
    vec2 uvG = distortedUV;
    vec2 uvB = distortedUV - chroma;
    const int samples = 12;
    float blurStrength = 0.1 * speedAmount;
    vec3 sceneColor = vec3(0.0);
    for (int i = 0; i < samples; ++i) {
        float t = float(i) / float(samples - 1);
        float scale = 1.0 - blurStrength * t;
        sceneColor.r += texture(screenTexture, center + (uvR - center) * scale).r;
        sceneColor.g += texture(screenTexture, center + (uvG - center) * scale).g;
        sceneColor.b += texture(screenTexture, center + (uvB - center) * scale).b;
    }
    sceneColor /= float(samples);

    if (pc.portalStrength > 0.0 || pc.waveShape.y > 0.0) {
        bool encoded = pc.srgbOutput < 0.5;
        vec3 linear = encoded ? srgbToLinear(sceneColor) : sceneColor;
        if (pc.portalStrength > 0.0) linear = mix(linear, gradePortalColor(linear), pc.portalStrength);
        if (pc.waveShape.y > 0.0) {
            vec2 mask = portalWaveMask(distortedUV, sceneDepth, waveCells, pc.waveWorldFromClip,
                pc.waveOriginRadius, pc.waveShape.xyz, vec4(0.0));
            linear = applyPortalWaveColor(linear, mask * pc.waveShape.y);
        }
        sceneColor = encoded ? linearToSrgb(linear) : linear;
    }

    vec3 wastedColor = smoothstep(0.1, 0.9, vec3(dot(sceneColor, luminanceWeights)));
    FragColor = vec4(mix(sceneColor, wastedColor, pc.deathFactor), 1.0);
}
