#ifndef STEER_DEBUG_COMMON_GLSL
#define STEER_DEBUG_COMMON_GLSL

layout(location = 0) in vec2 v2fTexCoord;
layout(location = 2) in vec3 v2fPos;
layout(set = 1, binding = 0) uniform sampler2D uTexColor;

// Match glsl::SceneUniform while skipping camera/light data unused by diagnostics.
layout(scalar, set = 0, binding = 0) uniform DebugScene
{
    mat4 camera;
    mat4 projection;
    mat4 projCam;
    layout(offset = 1280) mat4 lightVP[4];
    layout(offset = 1536) vec4 cascadeSplits;
    layout(offset = 1552) vec4 portalClipPlane;
} uScene;

// Compatible with both ObjectPC and SkinnedPC (128 bytes).
layout(push_constant) uniform DebugObject
{
    mat4 transform;
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float alphaCutoff;
    uint boneBaseIndex;
    vec4 clipPlane;
} pc;

void ApplyDebugVisibility()
{
    float alpha = texture(uTexColor, v2fTexCoord).a * pc.baseColorFactor.a;
    if (alpha <= 0.0001 || (pc.alphaCutoff > 0.0 && alpha < pc.alphaCutoff))
        discard;
    vec4 worldPos = vec4(v2fPos, 1.0);
    if (dot(pc.clipPlane.xyz, pc.clipPlane.xyz) > 0.0001 && dot(worldPos, pc.clipPlane) < 0.0)
        discard;
    if (dot(uScene.portalClipPlane.xyz, uScene.portalClipPlane.xyz) > 0.0001
        && dot(worldPos, uScene.portalClipPlane) < 0.0)
        discard;
}

float DebugViewDepth()
{
    return max(-(uScene.camera * vec4(v2fPos, 1.0)).z, 0.00001);
}

vec2 DebugClipDistances()
{
    // GLM perspectiveRH_ZO: A=-far/(far-near), B=-far*near/(far-near).
    float a = uScene.projection[2][2];
    float b = uScene.projection[3][2];
    float nearPlane = max(abs(b / a), 0.00001);
    float farPlane = abs(a + 1.0) > 0.0000001 ? abs(b / (a + 1.0)) : 1000000.0;
    return vec2(nearPlane, max(farPlane, nearPlane * 1.001));
}

#endif
