#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) out float oAoColor; // 只输出一个单通道的 AO 灰度图即可

layout(set = 0, binding = 0) uniform sampler2D uDepthMap;
layout(set = 0, binding = 1) uniform sampler2D uNormalMap;
layout(set = 0, binding = 2) uniform sampler2D uNoiseMap; // 一个 4x4 的随机旋转向量贴图

layout(scalar, set = 0, binding = 3) uniform UScene {
    mat4 view;
    mat4 projection;
    mat4 projCam;
    vec4 cameraPos;
} uScene;

// 我们可以像 SSR 一样把这些参数放进 PushConstant
layout(push_constant) uniform PushConstants {
    float radius;    // 采样半径 (如 0.5)
    float bias;      // 偏移量，防止平面上长出麻子 (如 0.025)
    float intensity; // 强度 (如 1.5)
} pc;

// 这里需要 C++ 端生成一个半球状的 64 个随机样本向量数组传进来。
// 为了简化 Shader，你也可以用一个大的 UBO 或者 SSBO 传进来。
layout(set = 0, binding = 4) uniform SSAOKernel {
    vec4 samples[64]; 
} uKernel;

vec3 GetViewPos(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = inverse(uScene.projection) * ndc;
    return viewPos.xyz / viewPos.w;
}

void main() {
    oAoColor = 0.5; // 输出中度灰色
}