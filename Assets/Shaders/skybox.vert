#version 450
layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 outUVW;

// 【完美对齐】：这里的顺序已经和你的 C++ 结构体 100% 对齐！
layout(set = 0, binding = 0) uniform SceneUniforms {
    mat4 camera;      // 第 1 个 64 字节
    mat4 projection;  // 第 2 个 64 字节
    mat4 projCam;     // 第 3 个 64 字节
    // (因为天空盒只用到这几个，后面的 light、cameraPos 就可以不写了，只要头部对齐即可)
} ubo;

void main() {
    // 1. 翻转 Y 轴，纠正 Vulkan 倒立的贴图坐标
    outUVW = vec3(inPos.x, -inPos.y, inPos.z); 
    
    // 2. 提取相机的纯旋转矩阵（把 4x4 降级成 3x3 再升回 4x4，完美抹除位移 XYZ）
    // 这样天空盒就会永远跟着你走！
    mat4 rotView = mat4(mat3(ubo.camera)); 
    
    // 3. 把 1x1 的盒子放大 50 倍（突破近裁剪面），并乘上投影和旋转矩阵
    vec4 clipPos = ubo.projection * rotView * vec4(inPos * 50.0, 1.0);
    
    // 4. 强制透视除法后的 Z 值为 1.0（永远卡在屏幕最深处）
    gl_Position = clipPos.xyww; 
}