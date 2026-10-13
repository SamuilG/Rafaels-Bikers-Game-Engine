#version 450
#extension GL_GOOGLE_include_directive : require
#include "environment.glsl"

layout(location = 0) in vec3 inUVW;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform EnvironmentSettings {
    layout(offset = 1568) vec4 iblParams;
    layout(offset = 1584) vec4 environmentTint;
} uEnvironment;

// 【注意】：这里是 samplerCube，不是 sampler2D
layout(set = 0, binding = 1) uniform samplerCube skyboxSampler; 

void main() {
    vec3 direction = environmentDirection(inUVW, uEnvironment.iblParams.w);
    vec3 radiance = environmentColor(texture(skyboxSampler, direction).rgb, uEnvironment.environmentTint);
    outColor = vec4(radiance * max(uEnvironment.iblParams.x, 0.0), 1.0);
}
