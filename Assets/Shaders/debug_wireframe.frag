#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require
#include "debug_common.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    ApplyDebugVisibility();
    oColor = vec4(1.0);
}
