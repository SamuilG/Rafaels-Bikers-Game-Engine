#version 450 core

// 1. 明确声明输入输出的位置 (Location)
layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 FragColor;

// 2. 明确声明贴图的描述符集和绑定槽位 (Binding)
layout(set = 0, binding = 0) uniform sampler2D screenTexture; 

// 3. 将独立的 uniform 放进 Push Constant 中 (Vulkan 专属)
layout(push_constant) uniform PushConstants {
    float speedFactor;
} push;

void main()
{
    vec2 center = vec2(0.5, 0.5);
    vec2 uv = TexCoords;
    vec2 dir = uv - center;        // 从中心指向当前像素的向量
    float dist = length(dir);      // 距离中心的距离

    // ==========================================================
    // 【新增】：创建径向掩码 (Vignette Mask)
    // 用于控制视野中心清晰，边缘模糊。
    // smoothstep(inner_clear_radius, outer_full_blur_radius, current_distance)
    // 你可以调整这两个参数来控制中心清晰的范围和渐变的陡缓。
    // ==========================================================
    float vignetteMask = smoothstep(0.01, 0.25, dist);

    // ==========================================================
    // 1. 镜头畸变 (Lens Distortion)
    // 注意：我们将 vignetteMask 应用到这里，保持中心清晰
    // ==========================================================
    float distortionStrength = -0.8 * push.speedFactor * vignetteMask; // 应用掩码
    vec2 distortedUV = center + dir * (1.0 - distortionStrength * dist * dist);

    // ==========================================================
    // 2. 色差 (Chromatic Aberration)
    // 注意：我们将 vignetteMask 应用到这里，保持中心清晰
    // ==============================================================
    float caStrength = 0.04 * push.speedFactor * vignetteMask; // 应用掩码
    vec2 uvR = distortedUV + dir * caStrength;
    vec2 uvG = distortedUV; 
    vec2 uvB = distortedUV - dir * caStrength;

    // ==========================================================
    // 3. 径向动态模糊 (Radial Motion Blur)
    // 注意：我们将 vignetteMask 应用到这里，保持中心清晰
    // ==========================================================
    int samples = 12; 
    float blurStrength = 0.1 * push.speedFactor * vignetteMask; // 应用掩码
    
    vec4 finalColor = vec4(0.0);

    for(int i = 0; i < samples; i++)
    {
        float t = float(i) / float(samples - 1);
        float scale = 1.0 - blurStrength * t;

        vec2 sampleUV_R = center + (uvR - center) * scale;
        vec2 sampleUV_G = center + (uvG - center) * scale;
        vec2 sampleUV_B = center + (uvB - center) * scale;

        finalColor.r += texture(screenTexture, sampleUV_R).r;
        finalColor.g += texture(screenTexture, sampleUV_G).g;
        finalColor.b += texture(screenTexture, sampleUV_B).b;
    }

    finalColor /= float(samples);
    finalColor.a = 1.0;

    FragColor = finalColor;
}