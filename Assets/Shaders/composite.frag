#version 450

layout(location = 0) in vec2 v2fTexCoord;

layout(set = 0, binding = 0) uniform sampler2D uSceneTexture; // Original scene color
layout(set = 0, binding = 1) uniform sampler2D uBloomBlur;    // Blurred bloom color

layout(set = 0, binding = 2) uniform UMosaic {
    int mosaicOn;
    float mosaicSize;
} uMosaic;

layout(location = 0) out vec4 oColor;

// Dynamic Bloom Params
layout(push_constant) uniform BloomParams {
    float exposure;
    float bloomStrength;
} params;

void main() {
    vec2 uv = v2fTexCoord;

    // Mosaic logic
    if (uMosaic.mosaicOn == 1) {
        ivec2 coord = ivec2(gl_FragCoord.xy);
        coord.x -= coord.x % 5;
        coord.y -= coord.y % 3;
        vec2 texSize = vec2(textureSize(uSceneTexture, 0));
        uv = vec2(coord) / texSize;
    }

    vec3 sceneColor = texture(uSceneTexture, uv).rgb;      
    vec3 bloomColor = texture(uBloomBlur, uv).rgb;

    // Additive mix
    vec3 result = sceneColor + bloomColor * params.bloomStrength; 

    // Final color
    oColor = vec4(result, 1.0);
}