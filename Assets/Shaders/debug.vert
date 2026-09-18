#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 iPosition;
layout(location = 1) in vec2 iTexCoord;
layout(location = 2) in vec3 iNormal;

layout(scalar, set = 0, binding = 0) uniform DebugScene
{
    mat4 camera;
    mat4 projection;
    mat4 projCam;
} uScene;

layout(push_constant) uniform DebugTransform
{
    mat4 transform;
} pc;

layout(location = 0) out vec2 v2fTexCoord;
layout(location = 1) out vec3 v2fNormal;
layout(location = 2) out vec3 v2fPos;

void main()
{
    vec4 worldPos = pc.transform * vec4(iPosition, 1.0);
    v2fTexCoord = iTexCoord;
    v2fNormal = normalize(mat3(pc.transform) * iNormal);
    v2fPos = worldPos.xyz;
    gl_Position = uScene.projCam * worldPos;
}