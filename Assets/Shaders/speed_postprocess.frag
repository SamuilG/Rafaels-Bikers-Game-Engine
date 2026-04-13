#version 450 core

// 1. 明确声明输入输出的位置 (Location)
layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 FragColor;

// 2. 明确声明贴图的描述符集和绑定槽位 (Binding)
// 注意：如果你的框架里屏幕贴图绑定的是其他槽位，请修改 binding 的值 (通常是 0)
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
    // 1. 镜头畸变 (Lens Distortion)
    // 注意这里改成了 push.speedFactor
    // ==========================================================
    float distortionStrength = 0.2 * push.speedFactor; 
    vec2 distortedUV = center + dir * (1.0 - distortionStrength * dist * dist);

    // ==========================================================
    // 2. 色差 (Chromatic Aberration)
    // ==============================================================
    float caStrength = 0.015 * push.speedFactor; 
    vec2 uvR = distortedUV + dir * caStrength;
    vec2 uvG = distortedUV; 
    vec2 uvB = distortedUV - dir * caStrength;

    // ==========================================================
    // 3. 径向动态模糊 (Radial Motion Blur)
    // ==========================================================
    int samples = 12; 
    float blurStrength = 0.08 * push.speedFactor; 
    
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