#version 450

#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 iPosition;
layout(location = 1) in vec2 iTexCoord;
layout(location = 2) in vec3 iNormal;
layout(location = 3) in uvec4 iBoneIndices;
layout(location = 4) in vec4 iBoneWeights;

layout(scalar, set = 0, binding = 0) uniform UScene
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

layout(set = 0, binding = 2) uniform USkeleton
{
    mat4 boneMatrices[256];
} uSkeleton;

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    uint boneMatrixOffset;
    uint useSkinning;
    vec2 pad;
} pc;

layout(location = 0) out vec2 v2fTexCoord;
layout(location = 1) out vec3 v2fNormal;
layout(location = 2) out vec3 v2fPos;
layout(location = 3) out vec4 v2fLightProjPos;

mat4 skinMatrix()
{
    if (pc.useSkinning == 0u) return mat4(1.0);
    return iBoneWeights.x * uSkeleton.boneMatrices[pc.boneMatrixOffset + iBoneIndices.x]
         + iBoneWeights.y * uSkeleton.boneMatrices[pc.boneMatrixOffset + iBoneIndices.y]
         + iBoneWeights.z * uSkeleton.boneMatrices[pc.boneMatrixOffset + iBoneIndices.z]
         + iBoneWeights.w * uSkeleton.boneMatrices[pc.boneMatrixOffset + iBoneIndices.w];
}

void main()
{
    mat4 skin = skinMatrix();
    vec4 skinnedPos = skin * vec4(iPosition, 1.0);
    vec3 skinnedNormal = mat3(skin) * iNormal;

    v2fTexCoord = iTexCoord;
    v2fNormal = normalize(mat3(pc.transform) * skinnedNormal);

    vec4 worldPos = pc.transform * skinnedPos;
    v2fPos = worldPos.xyz;
    gl_Position = uScene.projCam * worldPos;
    v2fLightProjPos = uScene.lightVP[0] * worldPos;
}