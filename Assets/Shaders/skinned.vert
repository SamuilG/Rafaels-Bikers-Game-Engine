#version 450

#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3  iPosition;
layout(location = 1) in vec2  iTexCoord;
layout(location = 2) in vec3  iNormal;
layout(location = 3) in uvec4 iJoints;    // up to 4 joint indices per vertex
layout(location = 4) in vec4  iWeights;   // corresponding blend weights

// GpuLight struct (4 vec4s = 64 bytes per light, matches CPU GpuLight)
struct GpuLight {
    vec4 position;
    vec4 color;
    vec4 direction;
    vec4 params;
};

// Scene UBO (must match the CPU SceneUniform layout exactly)
layout(scalar, set = 0, binding = 0) uniform UScene
{
    mat4      camera;
    mat4      projection;
    mat4      projCam;
    vec4      cameraPos;
    GpuLight  lights[16]; // 16 * 64 = 1024 bytes
    vec4      lightPos;
    vec4      lightColor;
    uint      lightCount;
    uint      renderMode;
    uint      _pad0;
    uint      _pad1;
    mat4      lightVP[4];
    vec4      cascadeSplits;
} uScene;

// Bone matrices SSBO (set = 2, binding = 0)
layout(set = 2, binding = 0) readonly buffer BoneCB {
    mat4 bones[];
} boneCB;

// Push constants (must match default.frag layout exactly)
layout(push_constant) uniform PushConstants {
    mat4  transform;         //  64 bytes - offset 0
    vec4  baseColorFactor;   //  16 bytes - offset 64
    vec4  emissiveFactor;    //  16 bytes - offset 80  (matches default.frag)
    float metallicFactor;    //   4 bytes - offset 96
    float roughnessFactor;   //   4 bytes - offset 100
    float alphaCutoff;       //   4 bytes - offset 104
    uint  boneBaseIndex;     //   4 bytes - offset 108
} pc;

layout(location = 0) out vec2 v2fTexCoord;
layout(location = 1) out vec3 v2fNormal;
layout(location = 2) out vec3 v2fPos;
layout(location = 3) out vec4 v2fLightProjPos;

void main()
{
    // Blend up to 4 bone influences
    mat4 skinMat =
        iWeights.x * boneCB.bones[pc.boneBaseIndex + iJoints.x] +
        iWeights.y * boneCB.bones[pc.boneBaseIndex + iJoints.y] +
        iWeights.z * boneCB.bones[pc.boneBaseIndex + iJoints.z] +
        iWeights.w * boneCB.bones[pc.boneBaseIndex + iJoints.w];

    // Entity root transform applied on top of skeletal deformation
    mat4 finalMat = pc.transform * skinMat;

    vec4 worldPos  = finalMat * vec4(iPosition, 1.0);
    v2fPos         = worldPos.xyz;
    v2fNormal      = normalize(mat3(finalMat) * iNormal);
    v2fTexCoord    = iTexCoord;
    gl_Position    = uScene.projCam * worldPos;
    v2fLightProjPos = uScene.lightVP[0] * worldPos;
}
