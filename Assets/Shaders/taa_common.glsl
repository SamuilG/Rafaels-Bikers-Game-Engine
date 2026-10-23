#ifndef STEER_TAA_COMMON
#define STEER_TAA_COMMON
layout(std140, set = 0, binding = 0) uniform TemporalCamera {
    mat4 currentViewProjection;
    mat4 previousViewProjection;
    mat4 inverseProjection;
    vec4 parameters;
    mat4 inverseViewProjection;
    vec4 currentCameraPosition;
    vec4 previousCameraPosition;
} camera;
struct TemporalObject {
    mat4 currentModel;
    mat4 previousModel;
    uvec4 bones;
};
layout(std430, set = 0, binding = 1) readonly buffer Objects { TemporalObject objects[]; };
layout(std430, set = 0, binding = 2) readonly buffer Bones { mat4 boneMatrices[]; };
layout(location = 0) out vec4 previousClip;
layout(location = 1) flat out uint historyValid;
void projectTemporal(vec3 position, mat4 currentSkin, mat4 previousSkin) {
    TemporalObject object = objects[gl_InstanceIndex];
    gl_Position = camera.currentViewProjection * object.currentModel * currentSkin * vec4(position, 1.0);
    previousClip = camera.previousViewProjection * object.previousModel * previousSkin * vec4(position, 1.0);
    historyValid = object.bones.w;
}
#endif
