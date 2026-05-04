#version 460
#extension GL_GOOGLE_include_directive : require

// ==========================================
// 输入与输出
// ==========================================
layout(location = 0) in vec2 inUV; // 全屏三角形生成的 UV

layout(location = 0) out float oAoColor; // 输出单个 float，写入 R8_UNORM 贴图

// ==========================================
// 描述符绑定 (Descriptor Bindings)
// ==========================================

// Binding 0: 场景深度缓冲 (Depth Buffer)
layout(set = 0, binding = 0) uniform sampler2D uDepthMap;

// Binding 1: 场景相机空间法线 (G-Buffer Normal)
// 我们假设 XYZ 存法线，A 存粗糙度 (G-Buffer 格式为 R16G16B16A16_SFLOAT)
layout(set = 0, binding = 1) uniform sampler2D uNormalMap;

// Binding 2: 4x4 旋转噪声贴图 (Noise Texture)
// 必须设置为重复采样 (REPEAT)
layout(set = 0, binding = 2) uniform sampler2D uNoiseMap;

// Binding 3: 场景 UBO (用于相机矩阵)
layout(set = 0, binding = 3) uniform SceneUniforms {
    mat4 view;
    mat4 proj;
    mat4 projInv; // 【关键】我们需要逆投影矩阵来重建相机空间位置
} uScene;

// Binding 4: SSAO 采样半球 UBO (64 个采样点)
layout(set = 0, binding = 4) uniform SsaoKernelUniforms {
    vec4 samples[64];
} uKernel;

// ==========================================
// Push Constants (SSAO 调优参数)
// ==========================================
layout(push_constant) uniform SsaoParameters {
    float radius; // 采样半径 (世界单位)
    float bias;   // 深度偏移 (防止“粉尘”噪点)
    float power;  // AO 强度指数 (用于增强阴影对比度)
} uParams;

// ==========================================
// 辅助函数: 从深度重建相机空间位置
// ==========================================
vec3 reconstructViewPos(vec2 uv) {
    float depth = texture(uDepthMap, uv).r;
    // Vulkan 深度是 [0, 1]，我们需要将其转换回投影空间的 NDCs z [-1, 1] (如果使用标准 Proj 矩阵)
    // 或者直接保持 0-1 (如果使用 Reverse-Z)。这里假设标准 Vulkan 投影。
    // 但更简便且稳健的方法是利用 projInv：
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = uScene.projInv * clipPos;
    return viewPos.xyz / viewPos.w; // 透视除法
}

// ==========================================
// 主函数
// ==========================================
void main() {
    // 1. 获取当前像素的相机空间数据
    vec3 fragPos = reconstructViewPos(inUV);
    vec4 normalRaw = texture(uNormalMap, inUV);
    if (normalRaw.a == 0.0) { // 简单判断，如果没有几何体（法线为空），则不计算
        oAoColor = 1.0;
        return;
    }
    vec3 normal = normalize(normalRaw.xyz);

    // 2. 获取随机旋转向量 (Noise)
    // 噪声贴图是 4x4 的，我们需要将其平铺在整个屏幕上
    vec2 noiseScale = textureSize(uDepthMap, 0) / 4.0;
    vec3 randomVec = normalize(texture(uNoiseMap, inUV * noiseScale).xyz);

    // 3. 构建 TBN 矩阵 (Tangent -> View)
    // 这允许我们将采样半球旋转到与当前表面法线对齐
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal)); // Gram-Schmidt 正交化
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // 4. 计算环境光遮蔽 (Ambient Occlusion)
    float occlusion = 0.0;
    int kernelSize = 64; // 使用 64 个采样点

    for(int i = 0; i < kernelSize; ++i) {
        // a) 将采样点从切线空间转换到相机空间，并应用随机旋转
        vec3 samplePos = TBN * uKernel.samples[i].xyz;
        
        // b) 将采样点移动到当前像素位置，并应用半径缩放
        samplePos = fragPos + samplePos * uParams.radius; 
        
        // c) 将相机空间采样点投影回屏幕坐标 (UV)，以读取深度
        vec4 offset = vec4(samplePos, 1.0);
        offset = uScene.proj * offset; // 投影转换
        offset.xyz /= offset.w;         // 透视除法得到 NDCs (-1 到 1)
        offset.xyz = offset.xyz * 0.5 + 0.5; // 转换到 UV 空间 (0 到 1)
        
        // d) 获取采样点所处位置的“实际”几何体深度
        // 我们需要它的相机空间 Z 值来做比较
        float sampleDepth = texture(uDepthMap, offset.xy).r;
        
        // 同样使用逆投影重建，保证比较是在同一空间（相机空间）
        // 这一步可以优化，但在 Init() 刚跑通时，保持逻辑清晰最重要。
        vec4 sampleClipPos = vec4(offset.xy * 2.0 - 1.0, sampleDepth, 1.0);
        vec4 sampleViewPos = uScene.projInv * sampleClipPos;
        float actualZ = sampleViewPos.z / sampleViewPos.w;

        // e) 范围检查 (Range Check)
        // 这是为了防止一个离当前像素非常远的几何体（比如天空盒或背景墙）错误地产生遮挡
        // actualZ 是背景几何体的 Z，samplePos.z 是采样点的 Z
        float rangeCheck = smoothstep(0.0, 1.0, uParams.radius / abs(fragPos.z - actualZ));

        // f) 比较深度与遮蔽测试
        // samplePos.z（采样点）应该比 actualZ（背景几何体）更靠前（Z 值更大）才算产生遮蔽。
        // 添加 bias 来防止平面上的噪点。
        // 注意：这里假设相机空间 Z 轴正方向从相机指向场景 (标准 Vulkan 惯例)。
        occlusion += (actualZ >= samplePos.z + uParams.bias ? 1.0 : 0.0) * rangeCheck;
    }

    // 5. 归一化并反转结果
    // 输出的结果是“遮蔽度”，我们希望它代表阴影强度 (0=全阴影, 1=无阴影)
    occlusion = 1.0 - (occlusion / kernelSize);
    
    // 应用 Power 指数增强对比度
    oAoColor = pow(occlusion, uParams.power);
}