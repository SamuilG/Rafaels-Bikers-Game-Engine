#version 450

layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 outUVW;

layout(set = 0, binding = 0) uniform SceneUniforms {
    mat4 camera;      // 第一个：View 矩阵
    mat4 projection;  // 第二个：Proj 矩阵
    mat4 projCam;     // 第三个：VP 矩阵
} ubo;

void main() {
    outUVW = inPos; 
    
    // 提取纯旋转矩阵（丢弃相机的 xyz 位移）
    mat4 rotView = mat4(mat3(ubo.camera)); 
    
    // 把立方体放大，突破近裁剪面
    vec4 clipPos = ubo.projection * rotView * vec4(inPos * 1000.0, 1.0);
    
    // 强制让 Z = W，透视除法后 Z 永远是 1.0 (卡在屏幕最远端)
    gl_Position = clipPos.xyww; 
}