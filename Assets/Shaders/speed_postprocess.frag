#version 450 core

// 1. 明确声明输入输出的位置 (Location)
layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 FragColor;

// 2. 明确声明贴图的描述符集和绑定槽位 (Binding)
layout(set = 0, binding = 0) uniform sampler2D screenTexture; 

// 3. 【核心修复 1】：同步 C++ 的结构体，加入 deathFactor
layout(push_constant) uniform PushConstants {
    float speedFactor;
    float deathFactor; 
} pc; // 统一改名为 pc (Push Constants)

void main()
{
    vec2 center = vec2(0.5, 0.5);
    vec2 uv = TexCoords;
    vec2 dir = uv - center;        // 从中心指向当前像素的向量
    float dist = length(dir);      // 距离中心的距离

    // ==========================================================
    // 创建径向掩码 (Vignette Mask)
    // ==========================================================
    float vignetteMask = smoothstep(0.01, 0.25, dist);

    // ==========================================================
    // 1. 镜头畸变 (Lens Distortion)
    // ==========================================================
    float distortionStrength = -0.8 * pc.speedFactor * vignetteMask; // 使用 pc.speedFactor
    vec2 distortedUV = center + dir * (1.0 - distortionStrength * dist * dist);

    // ==========================================================
    // 2. 色差 (Chromatic Aberration)
    // ==============================================================
    float caStrength = 0.04 * pc.speedFactor * vignetteMask; 
    vec2 uvR = distortedUV + dir * caStrength;
    vec2 uvG = distortedUV; 
    vec2 uvB = distortedUV - dir * caStrength;

    // ==========================================================
    // 3. 径向动态模糊 (Radial Motion Blur)
    // ==========================================================
    int samples = 12; 
    float blurStrength = 0.1 * pc.speedFactor * vignetteMask; 
    
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

    // 【核心修复 2】：把极速特效算出来的颜色提取出来，给死亡滤镜用
    vec3 sceneColor = finalColor.rgb;

    // =======================================================
    //  Wasted 死亡特效 (黑白滤镜)
    // =======================================================
    if (pc.deathFactor > 0.0) {
        // 1. 标准的物理亮度公式 (Luminance) 转换为灰度
        float gray = dot(sceneColor, vec3(0.299, 0.587, 0.114));
        vec3 wastedColor = vec3(gray);

        // 2. 增强对比度，让黑白更压抑、更惨烈
        wastedColor = smoothstep(0.1, 0.9, wastedColor);
        
        // 3. 根据 deathFactor 平滑混合 (如果后续加渐变，这里就能丝滑过渡)
        sceneColor = mix(sceneColor, wastedColor, pc.deathFactor);
    }

    // 【核心修复 3】：统一使用 FragColor 输出
    FragColor = vec4(sceneColor, 1.0);
}