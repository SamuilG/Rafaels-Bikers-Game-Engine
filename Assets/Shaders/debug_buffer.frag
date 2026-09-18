#version 450

layout(location = 0) in vec2 v2fTexCoord;
layout(set = 0, binding = 0) uniform sampler2D uBuffer;
layout(location = 0) out vec4 oColor;

// IDs match Runtime/Renderer/RenderUtilities/ViewMode.hpp.
layout(push_constant) uniform BufferView {
    int mode;
    int padding0;
    int padding1;
    int padding2;
} params;

void main() {
    vec4 value = texture(uBuffer, v2fTexCoord);
    vec3 result = vec3(0.0);
    if (params.mode == 6) {
        result = vec3(value.r); // R8 occlusion: white is unoccluded.
    } else if (params.mode == 7) {
        result = value.rgb * value.a; // Actual reflected radiance times hit confidence.
    } else if (params.mode == 8 && dot(value.xyz, value.xyz) > 0.0001) {
        result = normalize(value.xyz) * 0.5 + 0.5; // Signed world-space normal.
    }
    // Diagnostic values bypass exposure, bloom, mosaic and tone mapping.
    oColor = vec4(result, 1.0);
}
