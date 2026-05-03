#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) out vec4 oSsrColor;

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uDepthMap;
layout(set = 0, binding = 2) uniform sampler2D uNormalMap;

layout(scalar, set = 0, binding = 3) uniform UScene {
    mat4 view;
    mat4 projection;
    mat4 projCam;
    vec4 cameraPos;
} uScene;

// 固定的步进参数（你可以后续再改回 PushConstants 控制）
const float STEP_SIZE = 0.2;   // 每次射线前进的距离
const float MAX_STEPS = 100.0; // 最多走100步
const float THICKNESS = 0.5;   // 物体厚度容差，防止射线穿透薄墙

vec3 GetViewPos(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = inverse(uScene.projection) * ndc;
    return viewPos.xyz / viewPos.w;
}

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uSceneColor, 0));
    float depth = texture(uDepthMap, uv).r;

    // 天空盒不需要反射自己
    if (depth >= 1.0) {
        oSsrColor = vec4(0.0);
        return;
    }

    vec4 normalData = texture(uNormalMap, uv);
    vec3 normalWorld = normalData.xyz;
    float roughness = normalData.a;

    // 粗糙度大于 0.5 的材质不配拥有 SSR，节省性能
    if (roughness > 0.5 || length(normalWorld) < 0.1) {
        oSsrColor = vec4(0.0);
        return;
    }

    // 准备射线
    vec3 viewPos = GetViewPos(uv, depth);
    vec3 normalView = normalize(mat3(uScene.view) * normalWorld);
    vec3 viewDir = normalize(viewPos);
    vec3 reflectDir = normalize(reflect(viewDir, normalView));

    // 往前推一点点作为起点，防止浮点精度导致立刻撞到自己
    vec3 currentPos = viewPos + reflectDir * 0.1;
    vec2 hitUV = vec2(-1.0);
    float hitResult = 0.0;

    // 射线步进循环
    for (int i = 0; i < int(MAX_STEPS); ++i) {
        currentPos += reflectDir * STEP_SIZE;

        vec4 projPos = uScene.projection * vec4(currentPos, 1.0);
        projPos /= projPos.w;
        vec2 sampleUV = projPos.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            break; // 飞出屏幕
        }

        float sampleDepth = texture(uDepthMap, sampleUV).r;
        if(sampleDepth >= 1.0) continue; // 略过天空盒

        vec3 sampleViewPos = GetViewPos(sampleUV, sampleDepth);
        float depthDiff = currentPos.z - sampleViewPos.z;

        // 如果射线的 Z 值小于屏幕物体的 Z 值（Vulkan 观察空间 Z 为负，越小越远）
        // 并且没有超过厚度阈值，就算作“撞到了”
        if (depthDiff < 0.0 && depthDiff > -THICKNESS) {
            hitUV = sampleUV;
            hitResult = 1.0;
            break;
        }
    }

    if (hitResult > 0.5) {
        // 边缘淡出：越靠近屏幕边缘，反射越透明，防止硬切边
        vec2 dCoords = smoothstep(vec2(0.1), vec2(0.0), hitUV) + smoothstep(vec2(0.9), vec2(1.0), hitUV);
        float edgeFactor = clamp(1.0 - (dCoords.x + dCoords.y), 0.0, 1.0);
        
        // 粗糙度淡出：越粗糙的地方，SSR 反射越弱
        float roughnessFade = clamp(1.0 - roughness * 2.0, 0.0, 1.0);

        vec3 refColor = texture(uSceneColor, hitUV).rgb;
        oSsrColor = vec4(refColor, edgeFactor * roughnessFade);
    } else {
        oSsrColor = vec4(0.0);
    }
}