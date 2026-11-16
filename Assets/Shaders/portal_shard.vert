#version 450

#extension GL_EXT_scalar_block_layout : require

layout(scalar, set = 0, binding = 0) uniform UScene {
    mat4 camera;
    mat4 projection;
} uScene;

layout(push_constant) uniform Shard {
    mat4 transform;
} pc;

layout(location = 0) out vec3 viewPosition;
layout(location = 1) flat out vec3 faceNormal;

// Four outward-wound triangular faces with independent flat normals.
const vec3 corners[4] = vec3[](
    vec3(0.0, 0.90, 0.0),
    vec3(0.0, -0.38, 0.58),
    vec3(0.50, -0.38, -0.29),
    vec3(-0.50, -0.38, -0.29)
);
const ivec3 faces[4] = ivec3[](
    ivec3(0, 1, 2), ivec3(0, 2, 3), ivec3(0, 3, 1), ivec3(1, 3, 2)
);

void main() {
    ivec3 face = faces[gl_VertexIndex / 3];
    mat4 modelView = uScene.camera * pc.transform;
    mat3 basis = mat3(modelView);
    if (determinant(basis) < 0.0) face = face.xzy;
    viewPosition = (modelView * vec4(corners[face[gl_VertexIndex % 3]], 1.0)).xyz;
    faceNormal = normalize(cross(basis * (corners[face.y] - corners[face.x]),
        basis * (corners[face.z] - corners[face.x])));
    gl_Position = uScene.projection * vec4(viewPosition, 1.0);
}
