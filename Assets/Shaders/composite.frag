#version 450

// 1. 贴图绑定区 (Bindings)
layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uBloom;
layout(set = 0, binding = 2) uniform sampler2D uSsrMap;
// binding = 3 是你的 UBO (如果有的话)
layout(set = 0, binding = 4) uniform sampler2D uSsaoMap; // <--- 新增的 SSAO

// 2. 顶点输入与最终输出区 (Inputs & Outputs) - 必须在 main 之前！
layout(location = 0) in vec2 v2fTexCoord;
layout(location = 0) out vec4 oColor;

// 3. 主函数区
void main() {
    // 【临时测试代码】：放在这里，它就能认识 v2fTexCoord 和 oColor 了！
    float ssao = texture(uSsaoMap, v2fTexCoord).r;
    oColor = vec4(vec3(ssao), 1.0);
    return; // 强制截断，不执行后面的原来代码

    // ... 下面保留你原本的混合代码 ...
}