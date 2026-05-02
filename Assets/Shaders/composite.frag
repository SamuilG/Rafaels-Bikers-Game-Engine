#version 450

layout(location = 0) in vec2 v2fTexCoord;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uBloomColor;
// binding 2 是 Mosaic UBO，不影响我们
layout(set = 0, binding = 3) uniform sampler2D uSsrColor; // 【新增】：获取 SSR 结果

layout(push_constant) uniform PushConstants {
    float exposure;
    float bloomStrength;
} pc;

void main() {
    vec3 sceneColor = texture(uSceneColor, v2fTexCoord).rgb;
    vec3 bloomColor = texture(uBloomColor, v2fTexCoord).rgb;
    vec4 ssrData    = texture(uSsrColor, v2fTexCoord);
    
    // 【魔法时刻】：将 SSR 的反射颜色按照它的 Alpha(遮罩强度) 叠加到画面上！
    // 这样，只有算出了有效反射的金属区域才会被覆盖，其他地方不受影响。
    sceneColor += ssrData.rgb * ssrData.a; 

    // 添加 Bloom
    sceneColor += bloomColor * pc.bloomStrength;

    // 曝光与色调映射 (ACES等，视你原有的代码而定)
    vec3 mapped = vec3(1.0) - exp(-sceneColor * pc.exposure);

    oColor = vec4(mapped, 1.0);
}