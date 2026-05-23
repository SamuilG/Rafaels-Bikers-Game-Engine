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
} pc;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vWorldNormal;

void main() {
    vTexCoord = inTexCoord;
    vWorldNormal = normalize(mat3(pc.transform) * inNormal);

    vec4 worldPos = pc.transform * vec4(inPosition, 1.0);
    gl_Position = uScene.projCam * worldPos;
}
