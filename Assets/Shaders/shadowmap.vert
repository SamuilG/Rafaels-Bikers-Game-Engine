#version 450

#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 iPos;
layout(location = 1) in vec2 iTexCoord;
layout(location = 3) in uvec4 iBoneIndices;
layout(location = 4) in vec4 iBoneWeights;

layout(location = 0) out vec2 v2fTexCoord;

layout(set = 0, binding = 2) uniform USkeleton
{
    mat4 boneMatrices[256];
} uSkeleton;

layout(push_constant) uniform PC {
    mat4 lightModel;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    uint boneMatrixOffset;
    uint useSkinning;
    vec2 pad;
} uPush;

mat4 skinMatrix()
{
    if (uPush.useSkinning == 0u) return mat4(1.0);
    return iBoneWeights.x * uSkeleton.boneMatrices[uPush.boneMatrixOffset + iBoneIndices.x]
         + iBoneWeights.y * uSkeleton.boneMatrices[uPush.boneMatrixOffset + iBoneIndices.y]
         + iBoneWeights.z * uSkeleton.boneMatrices[uPush.boneMatrixOffset + iBoneIndices.z]
         + iBoneWeights.w * uSkeleton.boneMatrices[uPush.boneMatrixOffset + iBoneIndices.w];
}

void main()
{
    v2fTexCoord = iTexCoord;
    gl_Position = uPush.lightModel * skinMatrix() * vec4(iPos, 1.0);
}