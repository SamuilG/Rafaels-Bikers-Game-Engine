#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vWorldNormal;

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oBrightColor;
layout(location = 2) out vec4 oNormal;

layout(set = 1, binding = 0) uniform sampler2D uPortalViewTexture;

void main() {
    vec3 portalView = texture(uPortalViewTexture, vTexCoord).rgb;

    float vignette = smoothstep(0.0, 0.08, vTexCoord.x) *
        smoothstep(0.0, 0.08, vTexCoord.y) *
        smoothstep(0.0, 0.08, 1.0 - vTexCoord.x) *
        smoothstep(0.0, 0.08, 1.0 - vTexCoord.y);

    vec3 rim = vec3(0.25, 0.75, 1.0) * (1.0 - vignette) * 1.25;
    vec3 color = portalView * mix(0.82, 1.0, vignette) + rim;

    oColor = vec4(color, 1.0);
    oBrightColor = vec4(max(color - vec3(0.92), vec3(0.0)), 1.0);
    oNormal = vec4(normalize(vWorldNormal), 0.08);
}
