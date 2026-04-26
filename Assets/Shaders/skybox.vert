#version 450
layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 outUVW;

// 【关键修复】：顺序必须和 C++ 引擎里的 glsl::SceneUniform 结构体头三个矩阵完全一致！
layout(set = 0, binding = 0) uniform SceneUniforms {
    mat4 camera;      // 第一个：View 矩阵
    mat4 projection;  // 第二个：Proj 矩阵
    mat4 projCam;     // 第三个：VP 矩阵
    // (后面的灯光数据用不到，可以不写)
} ubo;

void main() {
    // Vulkan 的 Y 轴是朝下的，而 Cubemap 标准通常 Y 朝上
    // 给 Y 加上负号，防止天空盒上下颠倒
    outUVW = vec3(inPos.x, -inPos.y, inPos.z); 
    
    // 提取纯旋转矩阵（完美丢弃相机的 xyz 位移！）
    mat4 rotView = mat4(mat3(ubo.camera)); 
    
    // 把立方体放大，并应用正确的投影和旋转
    vec4 clipPos = ubo.projection * rotView * vec4(inPos * 1000.0, 1.0);
    
    // 强制让 Z = W，透视除法后 Z 永远是 1.0 (卡在屏幕最远端)
    gl_Position = clipPos.xyww; 
}