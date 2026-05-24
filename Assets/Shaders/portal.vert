#version 450

#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(scalar, set = 0, binding = 0) uniform UScene {
    mat4 camera;
    mat4 projection;
    mat4 projCam;
    vec4 cameraPos;
} uScene;

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 portalParams;
} pc;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out float vSurfaceAspect;

void main() {
    vTexCoord = inTexCoord;
    vWorldNormal = normalize(mat3(pc.transform) * inNormal);
    vec3 worldRight = mat3(pc.transform) * vec3(1.0, 0.0, 0.0);
    vec3 worldUp = mat3(pc.transform) * vec3(0.0, 1.0, 0.0);
    vSurfaceAspect = max(length(worldRight), 0.0001) / max(length(worldUp), 0.0001);

    vec4 worldPos = pc.transform * vec4(inPosition, 1.0);
    gl_Position = uScene.projCam * worldPos;
}
