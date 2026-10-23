#version 450
#extension GL_GOOGLE_include_directive : require
#include "taa_common.glsl"
layout(location = 0) in vec3 position;
void main() { projectTemporal(position, mat4(1.0), mat4(1.0)); }
