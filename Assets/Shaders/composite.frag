#version 450

layout(location = 0) in vec2 v2fTexCoord;

layout(set = 0, binding = 0) uniform sampler2D uSceneTexture; 
layout(set = 0, binding = 1) uniform sampler2D uBloomBlur;    

layout(set = 0, binding = 2) uniform UMosaic {
    int mosaicOn;
    float mosaicSize;
} uMosaic;

layout(set = 0, binding = 3) uniform sampler2D uSsrColor;

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
    // 叠加 SSR 倒影 (利用 a 通道控制强度)
    // ==========================================
    //sceneColor += ssrData.rgb * ssrData.a; 
    sceneColor = sceneColor * (1.0 - ssrData.a) + ssrData.rgb * ssrData.a;
    // 叠加 Bloom
    vec3 hdrColor = sceneColor + bloomColor * params.bloomStrength; 

    // Reinhard 色调映射
    vec3 mapped = hdrColor / (hdrColor + vec3(1.0));

    oColor = vec4(mapped, 1.0);
}