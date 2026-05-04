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
// 【新增】：接收来自 C++ Binding 4 的 SSAO 贴图
// ==========================================
layout(set = 0, binding = 4) uniform sampler2D uSsaoMap; 

layout(location = 0) out vec4 oColor;

layout(push_constant) uniform BloomParams {
    float exposure;
    float bloomStrength;
} params;

void main() {
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
    vec4 ssrData    = texture(uSsrColor, uv);

    // ==========================================
    // 【新增】：采样并应用 SSAO
    // ==========================================
    // 从 SSAO 贴图的 R 通道读取遮蔽值 (1.0 = 无阴影，0.0 = 全阴影)
    float ssao = texture(uSsaoMap, uv).r;
    
    // 将环境光遮蔽直接乘到场景颜色上 (正片叠底)
    // 理论上最好只乘到环境光上，但在简单的后处理合成中，直接乘在总颜色上也很有体积感
    sceneColor *= ssao; 

    // ==========================================
    // 叠加 SSR 倒影 (利用 a 通道控制强度)
    // ==========================================
    sceneColor = sceneColor * (1.0 - ssrData.a) + ssrData.rgb * ssrData.a;
    
    // 叠加 Bloom
    vec3 hdrColor = sceneColor + bloomColor * params.bloomStrength; 

    // Reinhard 色调映射
    vec3 mapped = hdrColor / (hdrColor + vec3(1.0));

    oColor = vec4(mapped, 1.0);

}