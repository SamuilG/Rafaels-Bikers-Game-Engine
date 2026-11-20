#version 450

layout(set = 1, binding = 0) uniform sampler2D uSceneColor;
layout(location = 0) in vec3 viewPosition;
layout(location = 1) flat in vec3 faceNormal;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform ShardMaterial {
    layout(offset = 64) vec4 optics; // opacity, IOR, strength, dispersion
    vec4 riderCone; // view-space direction and sine of the half-angle
    vec4 riderFade; // cosine of the half-angle and fade weight
} pc;

void main() {
    float opacity = pc.optics.x;
    if (pc.riderFade.y > 0.0) {
        float depth = dot(viewPosition, pc.riderCone.xyz);
        float offset = length(viewPosition - depth * pc.riderCone.xyz);
        float edgeDistance = offset * pc.riderFade.x - depth * pc.riderCone.w;
        opacity *= mix(1.0, smoothstep(0.0, 0.65, edgeDistance), pc.riderFade.y);
        if (opacity <= 0.0) discard;
    }
    vec2 imageSize = vec2(textureSize(uSceneColor, 0));
    vec2 pixel = 0.5 / imageSize;
    vec2 screenUv = gl_FragCoord.xy / imageSize;
    vec3 ray = normalize(viewPosition);
    vec3 transmitted = refract(ray, faceNormal, 1.0 / pc.optics.y);
    vec2 bend = (transmitted - ray).xy * vec2(imageSize.y / imageSize.x, -1.0) * pc.optics.z;
    vec3 scene = vec3(
        textureLod(uSceneColor, clamp(screenUv + bend * (1.0 - pc.optics.w), pixel, 1.0 - pixel), 0.0).r,
        textureLod(uSceneColor, clamp(screenUv + bend, pixel, 1.0 - pixel), 0.0).g,
        textureLod(uSceneColor, clamp(screenUv + bend * (1.0 + pc.optics.w), pixel, 1.0 - pixel), 0.0).b
    );

    outColor = vec4(scene, opacity);
}
