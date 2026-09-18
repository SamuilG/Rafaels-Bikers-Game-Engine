#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require
#include "debug_common.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    ApplyDebugVisibility();
    // Inspect the material color without lighting, tone mapping or translucency.
    oColor = vec4(texture(uTexColor, v2fTexCoord).rgb * pc.baseColorFactor.rgb, 1.0);
}
