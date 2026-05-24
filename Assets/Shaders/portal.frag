#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in float vSurfaceAspect;

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oBrightColor;
layout(location = 2) out vec4 oNormal;

layout(set = 1, binding = 0) uniform sampler2D uPortalViewTexture;

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 portalParams;
} pc;

float Hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float Noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = Hash21(i);
    float b = Hash21(i + vec2(1.0, 0.0));
    float c = Hash21(i + vec2(0.0, 1.0));
    float d = Hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float Fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i) {
        value += Noise(p) * amplitude;
        p = p * 2.03 + vec2(7.1, 3.7);
        amplitude *= 0.5;
    }
    return value;
}

void main() {
    float time = pc.portalParams.x;
    float phase = pc.portalParams.y;
    const float portalRadius = 1.12;
    const float portalDownOffset = 0.16;
    vec2 portalUv = vTexCoord * 2.0 - 1.0;
    portalUv.y -= portalDownOffset;
    portalUv.x *= max(vSurfaceAspect, 0.0001);

    float radius = length(portalUv);
    float angle = atan(portalUv.y, portalUv.x);
    float edgeNoise = Fbm(vec2(angle * 2.0 + phase + time * 0.32, radius * 7.0 - time * 0.24));
    float fineNoise = Fbm(portalUv * 11.0 + vec2(time * 0.48 + phase, -time * 0.36));
    float noisyOuterRadius = portalRadius + (edgeNoise - 0.5) * 0.10 + sin(angle * 14.0 + time * 1.3 + phase) * 0.012;
    float edgeBand = smoothstep(noisyOuterRadius - 0.34, noisyOuterRadius, radius);
    float softEdgeAlpha = 1.0 - smoothstep(noisyOuterRadius - 0.22, noisyOuterRadius, radius);
    float dissolveAlpha = mix(1.0, smoothstep(0.18, 0.74, fineNoise), edgeBand);
    float alpha = softEdgeAlpha * dissolveAlpha;

    if (alpha <= 0.025) {
        discard;
    }

    vec2 viewSize = vec2(textureSize(uPortalViewTexture, 0));
    vec2 projectedUv = gl_FragCoord.xy / max(viewSize, vec2(1.0));
    projectedUv = clamp(projectedUv, vec2(0.0), vec2(1.0));
    vec3 portalView = texture(uPortalViewTexture, projectedUv).rgb;

    float innerVignette = 1.0 - smoothstep(0.40, portalRadius, radius);
    vec3 color = portalView * mix(0.86, 1.03, innerVignette);

    oColor = vec4(color, alpha);
    oBrightColor = vec4(0.0, 0.0, 0.0, alpha);
    oNormal = vec4(normalize(vWorldNormal), 0.05 * alpha);
}
