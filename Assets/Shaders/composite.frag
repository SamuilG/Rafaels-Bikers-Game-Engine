#version 450

layout(location = 0) in vec2 v2fTexCoord;

layout(set = 0, binding = 0) uniform sampler2D uSceneTexture; 
layout(set = 0, binding = 1) uniform sampler2D uBloomBlur;    

layout(set = 0, binding = 2) uniform UMosaic {
    int mosaicOn;
    float mosaicSize;
} uMosaic;

layout(set = 0, binding = 3) uniform sampler2D uSsrColor;

// ==========================================
// 接收来自 C++ Binding 4 的 SSAO 贴图
// ==========================================
layout(set = 0, binding = 4) uniform sampler2D uSsaoMap; 

layout(location = 0) out vec4 oColor;

layout(push_constant) uniform BloomParams {
    float exposure;
    float bloomStrength;
    float editorBackdrop;
    float srgbOutput;
} params;

vec3 srgbToLinear(vec3 color) {
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)), greaterThan(color, vec3(0.04045)));
}

void main() {
    vec2 uv = v2fTexCoord;
    if (uMosaic.mosaicOn == 1) {
        ivec2 coord = ivec2(gl_FragCoord.xy);
        coord.x -= coord.x % 5;
        coord.y -= coord.y % 3;
        vec2 texSize = vec2(textureSize(uSceneTexture, 0));
        uv = vec2(coord) / texSize;
    }
    
    vec4 sceneSample = texture(uSceneTexture, uv);
    // The editor clears only empty background pixels to zero alpha, and omits
    // the main skybox. Apply the neutral surface after scene tone mapping so
    // exposure/SSAO cannot tint the paper backdrop. Matches EditorTheme.hpp.
    if (params.editorBackdrop > 0.5 && sceneSample.a < 0.0001) {
        vec3 viewport = vec3(229.0, 227.0, 216.0) / 255.0;
        vec3 paper = vec3(240.0, 238.0, 225.0) / 255.0;
        vec3 backdrop = mix(viewport, paper, (1.0 - v2fTexCoord.y) * 0.25);
        if (params.srgbOutput > 0.5) backdrop = srgbToLinear(backdrop);
        vec3 glow = max(texture(uBloomBlur, uv).rgb * params.bloomStrength * params.exposure, vec3(0.0));
        // Retain bloom spilling past object silhouettes without exposing the backdrop.
        oColor = vec4(backdrop + (vec3(1.0) - backdrop) * (glow / (glow + vec3(1.0))), 1.0);
        return;
    }
    vec3 sceneColor = sceneSample.rgb;
    vec3 bloomColor = texture(uBloomBlur, uv).rgb;
    vec4 ssrData    = texture(uSsrColor, uv);

    // ==========================================
    // 1. 采样并应用 SSAO
    // ==========================================
    float ssao = texture(uSsaoMap, uv).r;
    
    // 将环境光遮蔽直接乘到场景底色上（正片叠底，压暗角落）
    sceneColor *= ssao; 

    // ==========================================
    // 2. 叠加 SSR 倒影（修正变量名与混合逻辑）
    // ==========================================
    // 使用线性插值 (mix)：在有反射的地方显示反射，无反射的地方保留被 SSAO 压暗的底色
    sceneColor = mix(sceneColor, ssrData.rgb, ssrData.a);
    
    // ==========================================
    // 3. 叠加 Bloom 并进行色调映射
    // ==========================================
    vec3 hdrColor = (sceneColor + bloomColor * params.bloomStrength) * params.exposure;

    // Reinhard 色调映射
    vec3 mapped = hdrColor / (hdrColor + vec3(1.0));

    oColor = vec4(mapped, 1.0);
}
