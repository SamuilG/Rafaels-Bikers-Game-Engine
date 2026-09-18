#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require
#include "debug_common.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    ApplyDebugVisibility();
    vec2 clip = DebugClipDistances();
    float depth = clamp(DebugViewDepth(), clip.x, clip.y);
    // Logarithmic view distance gives useful contrast across a large world.
    float value = log(depth / clip.x) / log(clip.y / clip.x);
    oColor = vec4(vec3(value), 1.0);
}