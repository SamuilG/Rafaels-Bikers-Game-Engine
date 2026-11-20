#version 450

#include "portal_wave_mask.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outputColor;

layout(set = 0, binding = 0) uniform sampler2D sceneDepth;
layout(set = 0, binding = 1) uniform sampler2D waveCells;
layout(set = 0, binding = 3) uniform sampler2D sceneColor;

layout(push_constant) uniform PortalWave {
    mat4 worldFromClip;
    vec4 originRadius;
    vec4 shape;
    vec4 clipPlane;
} wave;

void main() {
    vec2 mask = portalWaveMask(uv, sceneDepth, waveCells, wave.worldFromClip,
        wave.originRadius, wave.shape.xyz, wave.clipPlane) * wave.shape.y;
    vec4 color = texelFetch(sceneColor, ivec2(gl_FragCoord.xy), 0);
    outputColor = vec4(applyPortalWaveColor(color.rgb, mask), color.a);
}
