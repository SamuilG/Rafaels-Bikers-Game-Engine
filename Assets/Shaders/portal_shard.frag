#version 450

layout(set = 1, binding = 0) uniform sampler2D uSceneColor;
layout(location = 0) in vec3 viewPosition;
layout(location = 1) flat in vec3 faceNormal;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform ShardMaterial {
    layout(offset = 64) vec4 optics; // opacity, IOR, strength, dispersion
} pc;

void main() {
    vec2 imageSize = vec2(textureSize(uSceneColor, 0));
    vec2 pixel = 0.5 / imageSize;
    vec2 screenUv = gl_FragCoord.xy / imageSize;
    vec3 ray = normalize(viewPosition);
    vec3 transmitted = refract(ray, faceNormal, 1.0 / pc.optics.y);
    vec2 bend = (transmitted - ray).xy * vec2(imageSize.y / imageSize.x, -1.0) * pc.optics.z;
    vec3 scene = vec3(
        texture(uSceneColor, clamp(screenUv + bend * (1.0 - pc.optics.w), pixel, 1.0 - pixel)).r,
        texture(uSceneColor, clamp(screenUv + bend, pixel, 1.0 - pixel)).g,
        texture(uSceneColor, clamp(screenUv + bend * (1.0 + pc.optics.w), pixel, 1.0 - pixel)).b
    );

    outColor = vec4(scene, pc.optics.x);
}
