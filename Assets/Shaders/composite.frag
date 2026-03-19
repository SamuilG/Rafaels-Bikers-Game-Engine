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
    const float gamma = 2.2;
    vec3 sceneColor = texture(uSceneTexture, v2fTexCoord).rgb;      
    vec3 bloomColor = texture(uBloomBlur, v2fTexCoord).rgb;

    // 1. 加法叠加 Bloom
    vec3 hdrColor = sceneColor + bloomColor * params.bloomStrength; 

    // 2. Reinhard 色调映射 (非常适合让高光变得柔和通透，解决死白)
    // 它能把无限大的亮度值平滑地压缩到 0.0 ~ 1.0 之间
    vec3 mapped = hdrColor / (hdrColor + vec3(1.0));

    // 3. Gamma 校正 (还原正确的显示器对比度)
    //mapped = pow(mapped, vec3(1.0 / gamma));

    // 最终输出
    oColor = vec4(mapped, 1.0);
}