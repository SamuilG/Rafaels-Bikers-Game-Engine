#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require
#include "debug_common.glsl"

layout(location = 0) out vec4 oColor;

vec3 MipColor(float lod)
{
    const vec3 colors[6] = vec3[6](
        vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0),
        vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 0.0, 1.0));
    int level = int(floor(lod));
    return mix(colors[level % 6], colors[(level + 1) % 6], fract(lod));
}

void main()
{
    float lastLevel = float(max(textureQueryLevels(uTexColor) - 1, 0));
    float lod = clamp(textureQueryLod(uTexColor, v2fTexCoord).x, 0.0, lastLevel);
    ApplyDebugVisibility();
    oColor = vec4(MipColor(lod), 1.0);
}