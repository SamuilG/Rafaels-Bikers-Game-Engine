#version 450

layout(location = 0) in vec2 v2fTexCoord;

layout(set = 0, binding = 0) uniform sampler2D uSceneTexture; // 原始场景图
layout(set = 0, binding = 1) uniform sampler2D uBloomBlur;    // 模糊后的高亮图

layout(location = 0) out vec4 oColor;

// 建议通过 Push Constant 传入曝光度，方便实时调整
layout(push_constant) uniform BloomParams {
    float exposure;   // 曝光度，默认 1.0
    float bloomStrength; // Bloom 强度，默认 1.0
} params;

void main() {
    vec3 sceneColor = texture(uSceneTexture, v2fTexCoord).rgb;      
    vec3 bloomColor = texture(uBloomBlur, v2fTexCoord).rgb;

    // 1. 简单的加法混合
    vec3 result = sceneColor + bloomColor * params.bloomStrength; 

    // 2. 暂时注释掉复杂的色调映射和 Gamma，只用最原生的输出看看效果
    // vec3 mapped = vec3(1.0) - exp(-result * params.exposure);
    // mapped = pow(mapped, vec3(1.0 / 2.2));

    // 直接输出线性叠加的结果
    oColor = vec4(result, 1.0);
}