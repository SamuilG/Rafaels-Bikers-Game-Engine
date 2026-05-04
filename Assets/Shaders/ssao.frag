#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 inUV;
layout(location = 0) out float oAoColor;

layout(set = 0, binding = 0) uniform sampler2D uDepthMap;
layout(set = 0, binding = 1) uniform sampler2D uNormalMap;
layout(set = 0, binding = 2) uniform sampler2D uNoiseMap;

// 【防弹设计1】：不再向 C++ 索要 projInv，防止结构体错位
layout(set = 0, binding = 3) uniform SceneUniforms {
    mat4 view;
    mat4 proj;
} uScene;

layout(set = 0, binding = 4) uniform SsaoKernelUniforms {
    vec4 samples[64];
} uKernel;

vec3 reconstructViewPos(vec2 uv, mat4 projInv) {
    float depth = texture(uDepthMap, uv).r;
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = projInv * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main() {
    vec4 normalRaw = texture(uNormalMap, inUV);
    // 【防弹设计2】：背景天空盒的法线 Alpha 通常为 0，遇到背景直接返回纯白，不画阴影
    if (length(normalRaw.xyz) < 0.1) {
        oAoColor = 1.0;
        return;
    }
    vec3 normal = normalize(normalRaw.xyz);
    // 【防弹设计3】：自己在 Shader 里算逆矩阵，零 C++ 依赖
    mat4 projInv = inverse(uScene.proj);
    vec3 fragPos = reconstructViewPos(inUV, projInv);

    // 【防弹设计4】：硬编码安全参数
  
    float radius = 10.0;  // 之前是 0.5
    float bias = 0.1;     // 之前是 0.025
    float power = 3.0;    // 加强对比度

    vec2 noiseScale = textureSize(uDepthMap, 0) / 4.0;
    vec3 randomVec = normalize(texture(uNoiseMap, inUV * noiseScale).xyz);

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal)); 
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    int kernelSize = 64; 

    for(int i = 0; i < kernelSize; ++i) {
        vec3 samplePos = TBN * uKernel.samples[i].xyz;
        samplePos = fragPos + samplePos * radius; 
        
        vec4 offset = vec4(samplePos, 1.0);
        offset = uScene.proj * offset; 
        offset.xyz /= offset.w;         
        offset.xyz = offset.xyz * 0.5 + 0.5; 
        
        float sampleDepth = texture(uDepthMap, offset.xy).r;
        vec4 sampleClipPos = vec4(offset.xy * 2.0 - 1.0, sampleDepth, 1.0);
        vec4 sampleViewPos = projInv * sampleClipPos;
        float actualZ = sampleViewPos.z / sampleViewPos.w;

        // 防止除以 0 的极小偏移量
        float zDistance = abs(fragPos.z - actualZ);
        float rangeCheck = smoothstep(0.0, 1.0, radius / (zDistance + 0.001));

        occlusion += (actualZ >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(kernelSize));
    oAoColor = pow(occlusion, power);
}