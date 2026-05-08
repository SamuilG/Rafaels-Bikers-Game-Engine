#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 inUV;
layout(location = 0) out float oAoColor;

layout(set = 0, binding = 0) uniform sampler2D uDepthMap;
layout(set = 0, binding = 1) uniform sampler2D uNormalMap;

layout(set = 0, binding = 3) uniform SceneUniforms {
    mat4 view;
    mat4 proj;
} uScene;

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
    
    // ==========================================
    // 【核心修复 1】：空间统一
    // G-Buffer 存的通常是世界空间法线，必须将其转换到观察空间 (View Space)
    // 这样才能和 reconstructViewPos 算出来的 View Space 坐标匹配！
    // ==========================================
    vec3 normalWorld = normalize(normalRaw.xyz);
    // 乘以 view 矩阵的左上角 3x3 部分，剥离位移，只保留旋转
    vec3 normal = normalize(mat3(uScene.view) * normalWorld);

    mat4 projInv = inverse(uScene.proj);
    vec3 fragPos = reconstructViewPos(inUV, projInv);

    // ==========================================
    // 物理参数调优
    // ==========================================
    float radius = 9.5;  // 缩小半径，只捕捉角落的细节遮挡
    
    // 【核心修复 2】：Bias 调优
    // 1.0 的 bias 在 1单位=1米 的游戏里意味着“忽略1米以内的厚度”，太大了！
    // 修改为 0.05 左右，刚好能避免平地上的自我遮挡噪点
    float bias = 0.05;    

    float power = 7.0; // 加深死角的黑度

    vec3 randomVec = normalize(vec3(
        hash(inUV) * 2.0 - 1.0,
        hash(inUV + 1.23) * 2.0 - 1.0,
        0.0
    ));

    // 使用转换到观察空间后的法线构建 TBN
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal)); 
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    int kernelSize = 64; 
    for(int i = 0; i < kernelSize; ++i) {
        float fi = float(i) + 0.5;
        float cosTheta = 1.0 - (fi / float(kernelSize)); 
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        float phi = 2.39996323 * fi; 
        
        vec3 sampleDir = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        
        float scale = fi / float(kernelSize);
        scale = mix(0.1, 1.0, scale * scale);
        sampleDir *= scale;

        vec3 samplePos = TBN * sampleDir;
        samplePos = fragPos + samplePos * radius; 
        
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
        
        float rangeCheck = smoothstep(0.0, 1.0, 1.0 - clamp(zDistance / radius, 0.0, 1.0));
        occlusion += (actualZ >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(kernelSize));
    oAoColor = pow(occlusion, power);
}