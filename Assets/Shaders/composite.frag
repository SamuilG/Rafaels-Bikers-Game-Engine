#version 450

layout(location = 0) in vec2 v2fTexCoord;

layout(set = 0, binding = 0) uniform sampler2D uSceneTexture; // Original scene color
layout(set = 0, binding = 1) uniform sampler2D uBloomBlur;    // Blurred bloom color

layout(set = 0, binding = 2) uniform UMosaic {
    int mosaicOn;
    float mosaicSize;
} uMosaic;

layout(location = 0) out vec4 oColor;

// Dynamic Bloom Params
layout(push_constant) uniform BloomParams {
    float exposure;
    float bloomStrength;
} params;

void main() {
    const float gamma = 2.2;
    vec2 uv = v2fTexCoord;
    if (uMosaic.mosaicOn == 1) {
        ivec2 coord = ivec2(gl_FragCoord.xy);
        coord.x -= coord.x % 5;
        coord.y -= coord.y % 3;
        vec2 texSize = vec2(textureSize(uSceneTexture, 0));
        uv = vec2(coord) / texSize;
    }
    vec3 sceneColor = texture(uSceneTexture, uv).rgb;      
    vec3 bloomColor = texture(uBloomBlur, uv).rgb;

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