#version 450

layout(location = 0) in vec2 v2fTexCoord;

// 绑定提取出来的亮度贴图
layout(set = 0, binding = 0) uniform sampler2D uBrightTexture;

layout(location = 0) out vec4 oColor;

// 通过你之前创建的 Pipeline Layout 传入 Push Constant
layout(push_constant) uniform PushConstants {
    int horizontal; // 1 为横向模糊，0 为纵向模糊
} pc;

// 高斯核权重 (5x5 卷积核的一维展开)
// 这些权重的总和接近 1.0，保证能量守恒
const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {             
    // 获取单个像素的大小
    vec2 tex_offset = 1.0 / textureSize(uBrightTexture, 0); 
    
    // 当前像素的颜色 * 中心权重
    vec3 result = texture(uBrightTexture, v2fTexCoord).rgb * weight[0]; 

    if(pc.horizontal == 1) {
        // 横向模糊：只在 X 轴方向偏移采样
        for(int i = 1; i < 5; ++i) {
            result += texture(uBrightTexture, v2fTexCoord + vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
            result += texture(uBrightTexture, v2fTexCoord - vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
        }
    } else {
        // 纵向模糊：只在 Y 轴方向偏移采样
        for(int i = 1; i < 5; ++i) {
            result += texture(uBrightTexture, v2fTexCoord + vec2(0.0, tex_offset.y * i)).rgb * weight[i];
            result += texture(uBrightTexture, v2fTexCoord - vec2(0.0, tex_offset.y * i)).rgb * weight[i];
        }
    }
    
    oColor = vec4(result, 1.0);
}