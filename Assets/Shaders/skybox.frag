#version 450

layout(location = 0) in vec3 inUVW;
layout(location = 0) out vec4 outColor;

// 【注意】：这里是 samplerCube，不是 sampler2D
layout(set = 0, binding = 1) uniform samplerCube skyboxSampler; 

void main() {
    outColor = texture(skyboxSampler, inUVW);
}