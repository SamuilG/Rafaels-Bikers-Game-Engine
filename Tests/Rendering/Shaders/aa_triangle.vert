#version 450
layout(location = 0) in vec3 position;
layout(push_constant) uniform Parameters { mat4 mvp; vec4 options; } pc;
void main() { gl_Position = pc.mvp * vec4(position, 1.0); }
