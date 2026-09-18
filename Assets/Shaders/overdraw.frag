#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require
#include "debug_common.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    ApplyDebugVisibility();
    // Additive RGB: each covered fragment contributes 1/20. The pipeline
    // enables depth testing/writes only for the depth-tested coverage mode.
    oColor = vec4(0.05, 0.05, 0.05, 1.0);
}