#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) out vec4 oSsrColor; // RGB 为反射颜色，A 为反射强度(遮罩)

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uDepthMap;
layout(set = 0, binding = 2) uniform sampler2D uNormalMap;

// 必须和 C++ 的 SceneUniform 对齐，用于获取相机矩阵
layout(scalar, set = 0, binding = 3) uniform UScene {
    mat4 view;        // offset 0
    mat4 projection;  // offset 64
    mat4 projCam;     // offset 128
    vec4 cameraPos;   // offset 192
} uScene;

layout(push_constant) uniform PushConstants {
    float stepSize;     // 步长，例如 0.2
    float maxSteps;     // 最大步数，例如 100
    float thickness;    // 物体厚度阈值，例如 0.1
    float _pad;
} pc;

// 辅助函数：通过 UV 和 Depth 重建 View Space (观察空间) 的 3D 坐标
vec3 GetViewPos(vec2 uv, float depth) {
    // Vulkan NDC: x,y [-1, 1], z [0, 1]
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = inverse(uScene.projection) * ndc;
    return viewPos.xyz / viewPos.w;
}

void main() {
    // 全屏后处理直接根据 gl_FragCoord 算 UV
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uSceneColor, 0));

    float depth = texture(uDepthMap, uv).r;
    // 如果是天空盒(深度为 1.0) 或者无效区域，直接退出
    if (depth >= 1.0) {
        oSsrColor = vec4(0.0);
        return;
    }

    vec4 normalData = texture(uNormalMap, uv);
    vec3 normalWorld = normalData.xyz;
    float roughness = normalData.a;

    // SSR 性能优化：过于粗糙的表面不需要 SSR，或者没有法线数据的地方跳过
    if (roughness > 0.5 || length(normalWorld) < 0.1) {
        oSsrColor = vec4(0.0);
        return;
    }

    // 将 World Space 法线转换到 View Space
    vec3 normalView = normalize(mat3(uScene.view) * normalize(normalWorld));

    // 计算 View Space 中的视线和反射向量
    vec3 viewPos = GetViewPos(uv, depth);
    vec3 viewDir = normalize(viewPos);
    vec3 reflectDir = normalize(reflect(viewDir, normalView));

    // SSR 射线步进 (Raymarching)
    vec3 currentPos = viewPos;
    vec2 hitUV = vec2(0.0);
    float hitResult = 0.0;

    for (int i = 0; i < int(pc.maxSteps); ++i) {
        currentPos += reflectDir * pc.stepSize;

        // 将走到的 3D 点重新投影回屏幕空间，看它在屏幕哪个 UV
        vec4 projPos = uScene.projection * vec4(currentPos, 1.0);
        projPos /= projPos.w;
        vec2 sampleUV = projPos.xy * 0.5 + 0.5;

        // 如果射线飞出了屏幕边缘，停止
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            break;
        }

        // 采样该 UV 处的真实深度，并算出它的真实 ViewPos
        float sampleDepth = texture(uDepthMap, sampleUV).r;
        vec3 sampleViewPos = GetViewPos(sampleUV, sampleDepth);

        // 深度比较 (在 Vulkan 观察空间中，Z 轴看向负方向，Z 越小离相机越远)
        // currentPos.z < sampleViewPos.z 意味着射线钻到了物体的后面
        float depthDiff = currentPos.z - sampleViewPos.z;

        if (depthDiff < 0.0 && depthDiff > -pc.thickness) {
            hitUV = sampleUV;
            hitResult = 1.0;
            break; // 撞到了！
        }
    }

    if (hitResult > 0.5) {
        // 边缘淡出效果 (Edge Fade)，防止反射在屏幕边缘生硬切断
        vec2 dCoords = smoothstep(vec2(0.1), vec2(0.0), hitUV) + smoothstep(vec2(0.9), vec2(1.0), hitUV);
        float edgeFactor = clamp(1.0 - (dCoords.x + dCoords.y), 0.0, 1.0);

        // 粗糙度越高，SSR 反射越弱（这是一种简化的物理拟合）
        float roughnessFade = 1.0 - roughness * 2.0;

        vec3 refColor = texture(uSceneColor, hitUV).rgb;
        oSsrColor = vec4(refColor, edgeFactor * roughnessFade);
    } else {
        oSsrColor = vec4(0.0);
    }
}