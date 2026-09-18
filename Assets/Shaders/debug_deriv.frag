#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require
#include "debug_common.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    float depth = DebugViewDepth();
    // Evaluate derivatives before discard, while all fragment lanes are active.
    vec2 relativeSlope = abs(vec2(dFdx(depth), dFdy(depth))) / depth;
    vec2 signal = vec2(1.0) - exp(-relativeSlope * 80.0);
    ApplyDebugVisibility();
    oColor = vec4(signal, 0.0, 1.0);
}