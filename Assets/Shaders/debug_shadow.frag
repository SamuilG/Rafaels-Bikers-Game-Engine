#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require
#include "debug_common.glsl"

layout(set = 0, binding = 1) uniform sampler2DArrayShadow uShadowMap;
#include "shadow_sampling.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    ApplyDebugVisibility();
    // Directional cascaded-shadow visibility: white is lit, black is shadowed.
    oColor = vec4(vec3(calculate_shadow()), 1.0);
}
