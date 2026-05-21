#version 450

layout(location = 0) in vec2 v2fTexCoord;
layout(location = 0) out float oBlurredSSAO; // 输出单通道的平滑 SSAO

layout(set = 0, binding = 0) uniform sampler2D uSsaoTex;   // 原始带噪点的 SSAO
layout(set = 0, binding = 1) uniform sampler2D uDepthTex;  // 深度图 (用于边缘保护)

void main() 
{
    vec2 texelSize = 1.0 / vec2(textureSize(uSsaoTex, 0));
    float centerDepth = texture(uDepthTex, v2fTexCoord).r;
    
    float result = 0.0;
    float weightSum = 0.0;
    
    // 假设你在 SSAO 生成阶段使用的是 4x4 的 Noise Texture，
    // 这里我们就使用 4x4 的循环，能完美抵消噪点的周期性！
    for (int x = -2; x < 2; ++x) 
    {
        for (int y = -2; y < 2; ++y) 
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float sampleDepth = texture(uDepthTex, v2fTexCoord + offset).r;
            float sampleSsao  = texture(uSsaoTex, v2fTexCoord + offset).r;
            
            // =========================================================
            // 【核心：边缘保护】
            // 比较采样点深度与中心点深度。如果差异极小，说明在同一个平面上，给予最大权重；
            // 如果差异很大（比如单车边缘和背景地板），说明跨越了物体边界，权重趋近于 0！
            // =========================================================
            float depthDiff = abs(centerDepth - sampleDepth);
            
            // 这里的 0.005 是深度容忍阈值，你可能需要根据你工程中 Near/Far Plane 的设置微调它
            // 如果你使用的是 Linear Depth，这个值大约在 0.1 到 0.5 之间
            float weight = depthDiff < 0.005 ? 1.0 : 0.0; 
            
            result += sampleSsao * weight;
            weightSum += weight;
        }
    }
    
    // 归一化，如果四周全是悬崖（weightSum == 0），就保留原本的 SSAO 值
    oBlurredSSAO = weightSum > 0.0 ? (result / weightSum) : texture(uSsaoTex, v2fTexCoord).r;
}