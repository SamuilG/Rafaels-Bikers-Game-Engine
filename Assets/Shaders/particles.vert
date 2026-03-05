#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in float inSize;
layout(location=2) in vec4 inColor;
layout(location=3) in vec4 inUVRect;
layout(location=4) in float inRotation;

layout(set=0, binding=0) uniform SceneUniform { mat4 camera; mat4 projection; mat4 projCam; } uScene;
layout(push_constant) uniform PushConstants { int useTexture;int debugMode;int _pad[2]; mat4 transform; } pc;

layout(location=0) out vec4 vColor;
layout(location=1) out flat int vUseTexture;
layout(location=2) out vec4 vUVRect; 
layout(location=3) out float vRotation; 
layout(location=4) out flat int vDebugMode;

void main() {
    gl_Position = uScene.projCam * vec4(inPos, 1.0);
    float distance = gl_Position.w;//粒子到摄像机的距离 (深度)
   
    float cameraScale = 2.0; 
    
    // 如果粒子在摄像机前面 (distance > 0)
    if (distance > 0.001) {
        // 大小缩放
        gl_PointSize = inSize * (cameraScale / distance);
    } else {
        gl_PointSize = 0.0; // 摄像机背后直接隐藏
    }
    vColor = inColor;
    vUseTexture = pc.useTexture; 
    vUVRect = inUVRect; 
    vRotation = inRotation; 
    vDebugMode = pc.debugMode;
}