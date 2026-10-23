#version 450
#extension GL_GOOGLE_include_directive : require
#include "taa_common.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in uvec4 joints;
layout(location = 2) in vec4 weights;
mat4 skin(uint base) {
    return weights.x * boneMatrices[base + joints.x] + weights.y * boneMatrices[base + joints.y] +
        weights.z * boneMatrices[base + joints.z] + weights.w * boneMatrices[base + joints.w];
}
void main() {
    TemporalObject object = objects[gl_InstanceIndex];
    projectTemporal(position, skin(object.bones.x), skin(object.bones.y));
}
