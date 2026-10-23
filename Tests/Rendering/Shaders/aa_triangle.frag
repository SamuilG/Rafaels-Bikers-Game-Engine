#version 450
layout(push_constant) uniform Parameters { mat4 mvp; vec4 options; } pc;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 bright;
layout(location = 2) out vec4 normal;
void main() {
    float c = pc.options.x > 0.5 ? (((int(gl_FragCoord.x) + int(pc.options.y)) % 2 == 0) ? 0.2 : 0.8) : 1.0;
    color = vec4(vec3(c), 1); bright = vec4(vec3(c * 2.0), 1); normal = vec4(0, 0, 1, 0.3);
}
