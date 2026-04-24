#version 450
#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec3 iPosition;
layout( location = 1 ) in vec3 iColor;

layout( scalar, set = 0, binding = 0 ) uniform UScene
{
    mat4 camera;
    mat4 projection;
    mat4 projCam;
    vec4 cameraPos;
    vec4 lightPos;
    vec4 lightColor;
    uint renderMode;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    mat4 lightVP[4];
    vec4 cascadeSplits;
} uScene;

layout( location = 0 ) out vec3 v2fColor;

void main()
{
    
    v2fColor = iColor;
    
    
    gl_Position = uScene.projCam * vec4(iPosition, 1.0);
}