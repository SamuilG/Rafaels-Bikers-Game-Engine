#version 450
layout(location = 0) in vec4 previousClip;
layout(location = 1) flat in uint historyValid;
layout(location = 0) out vec4 motion;
layout(set = 0, binding = 3) uniform sampler2D currentDepth;
void main() {
    ivec2 size = textureSize(currentDepth, 0);
    float depth = texelFetch(currentDepth, ivec2(gl_FragCoord.xy), 0).r;
    // Match the material pass's visible surface, including cutout/dither holes.
    // Account for a multisample depth resolve selecting a subpixel position on
    // a sloping triangle, rather than the motion pass's pixel center.
    float tolerance = max(0.000002, 2.0 * fwidth(gl_FragCoord.z));
    if (depth >= 1.0 || abs(gl_FragCoord.z - depth) > tolerance) discard;
    if (historyValid == 0u || previousClip.w <= 0.0) { motion = vec4(0.0); return; }
    vec2 currentUv = gl_FragCoord.xy / vec2(size);
    vec2 previousUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
    motion = vec4(previousUv - currentUv, min(previousClip.w, 65504.0), 1.0);
}
