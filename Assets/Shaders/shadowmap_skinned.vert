#version 450

#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3  iPosition;
layout(location = 1) in vec2  iTexCoord;
layout(location = 2) in vec3  iNormal;    // bound for binding compatibility; unused
layout(location = 3) in uvec4 iJoints;
layout(location = 4) in vec4  iWeights;

// Bone matrices SSBO (set = 2, binding = 0) - same as skinned.vert
layout(set = 2, binding = 0) readonly buffer BoneCB {
    mat4 bones[];
} boneCB;

// Push constants:
//   lightModel   = lightVP[cascade] * modelTransform  (precomputed on CPU, 64 bytes)
//   boneBaseIndex = starting slot in boneCB for this entity
layout(push_constant) uniform PC {
    mat4  lightModel;
    uint  boneBaseIndex;
} pc;

layout(location = 0) out vec2 v2fTexCoord;

void main()
{
    mat4 skinMat =
        iWeights.x * boneCB.bones[pc.boneBaseIndex + iJoints.x] +
        iWeights.y * boneCB.bones[pc.boneBaseIndex + iJoints.y] +
        iWeights.z * boneCB.bones[pc.boneBaseIndex + iJoints.z] +
        iWeights.w * boneCB.bones[pc.boneBaseIndex + iJoints.w];

    v2fTexCoord = iTexCoord;
    gl_Position = pc.lightModel * skinMat * vec4(iPosition, 1.0);
}
