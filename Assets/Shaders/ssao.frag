#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 inUV;
layout(location = 0) out float oAoColor;

// 只需要深度和法线！无视 C++ 传来的噪点图和采样核！
layout(set = 0, binding = 0) uniform sampler2D uDepthMap;
layout(set = 0, binding = 1) uniform sampler2D uNormalMap;

// 匹配你 default.frag 里的 uScene
layout(set = 0, binding = 3) uniform SceneUniforms {
    mat4 view;
    mat4 proj;
} uScene;

// GPU 内置伪随机发生器
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 reconstructViewPos(vec2 uv, mat4 projInv) {
    float depth = texture(uDepthMap, uv).r;
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = projInv * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main() {
    vec4 normalRaw = texture(uNormalMap, inUV);
    // 过滤背景
    if (length(normalRaw.xyz) < 0.1) {
        oAoColor = 1.0; 
        return; 
    }
    vec3 normal = normalize(normalRaw.xyz);

    mat4 projInv = inverse(uScene.proj);
    vec3 fragPos = reconstructViewPos(inUV, projInv);

    // ==========================================
    // 物理参数调优 (专为大场景定制)
    // ==========================================
    float radius = 5.0;  // 采样半径
    float bias = 0.2;    // 防止地表自阴影的偏移
    float power = 2.5;   // 阴影对比度

    // 用 Shader 内部算出的噪点，绝对安全
    vec3 randomVec = normalize(vec3(
        hash(inUV) * 2.0 - 1.0,
        hash(inUV + 1.23) * 2.0 - 1.0,
        0.0
    ));

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal)); 
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    int kernelSize = 32; // 32 根射线的黄金螺旋，画质极高

    for(int i = 0; i < kernelSize; ++i) {
        // ==========================================
        // 核心技术：斐波那契半球分布 (Fibonacci Hemisphere)
        // 绝对均匀，完美抛弃 C++ 传进来的 UBO！
        // ==========================================
        float fi = float(i) + 0.5;
        float cosTheta = 1.0 - (fi / float(kernelSize)); 
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        float phi = 2.39996323 * fi; // 黄金比例角度
        
        vec3 sampleDir = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        
        // 靠近中心密集采样
        float scale = fi / float(kernelSize);
        scale = mix(0.1, 1.0, scale * scale);
        sampleDir *= scale;

        vec3 samplePos = TBN * sampleDir;
        samplePos = fragPos + samplePos * radius; 
        
        // 投影到屏幕
        vec4 offset = vec4(samplePos, 1.0);
        offset = uScene.proj * offset; 
        offset.xyz /= offset.w;         
        offset.xyz = offset.xyz * 0.5 + 0.5; 
        
        if(offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) continue;

        float sampleDepth = texture(uDepthMap, offset.xy).r;
        vec4 sampleClipPos = vec4(offset.xy * 2.0 - 1.0, sampleDepth, 1.0);
        vec4 sampleViewPos = projInv * sampleClipPos;
        float actualZ = sampleViewPos.z / sampleViewPos.w;

        float zDistance = abs(fragPos.z - actualZ);
        float rangeCheck = smoothstep(0.0, 1.0, radius / (zDistance + 0.001));

        occlusion += (actualZ >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(kernelSize));
    oAoColor = pow(occlusion, power);
}