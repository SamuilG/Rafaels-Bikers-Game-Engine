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

layout(push_constant) uniform PushConstants {
    float stepSize;     
    float maxSteps;     
    float thickness;    
    float maxDistance;  
} pc;

vec3 GetViewPos(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = inverse(uScene.projection) * ndc;
    return viewPos.xyz / viewPos.w;
}

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uSceneColor, 0));
    float depth = texture(uDepthMap, uv).r;

    // 天空盒不需要反射
    if (depth >= 1.0) { oSsrColor = vec4(0.0); return; }

    vec4 normalData = texture(uNormalMap, uv);
    vec3 normalWorld = normalData.xyz;
    float roughness = normalData.a;

    // 粗糙度拦截
    if (roughness > 0.45 || length(normalWorld) < 0.1) { oSsrColor = vec4(0.0); return; }

    vec3 viewPos = GetViewPos(uv, depth);
    
    // =========================================================
    // 【保留优化】：硬性距离剔除，挽救你的 FPS！
    // =========================================================
    float pixelDist = length(viewPos);
    float safeMaxDist = max(pc.maxDistance, 50.0); 
    if (pixelDist > safeMaxDist) { oSsrColor = vec4(0.0); return; }

    vec3 normalView = normalize(mat3(uScene.view) * normalWorld);
    vec3 viewDir = normalize(viewPos);
    vec3 reflectDir = normalize(reflect(viewDir, normalView));

    // 脑后透视拦截
    if (viewPos.z + reflectDir.z * 0.1 > 0.0) { oSsrColor = vec4(0.0); return; }

    vec2 hitUV = vec2(-1.0);
    float hitResult = 0.0;

    // 起点推进
    vec3 currentPos = viewPos + reflectDir * 0.1;

    // =========================================================
    // 【保留优化】：远距离步长缩放，降低循环次数
    // =========================================================
    float distFactor = smoothstep(10.0, safeMaxDist, pixelDist);
    float dynamicStepSize = mix(pc.stepSize, pc.stepSize * 1.5, distFactor);
    int dynamicMaxSteps = int(mix(pc.maxSteps, pc.maxSteps * 0.6, distFactor));

    // 为老版本的 Thickness 加一个安全下限，防止穿透
    float safeThickness = max(pc.thickness, 0.5);

    for (int i = 0; i < dynamicMaxSteps; ++i) {
        currentPos += reflectDir * dynamicStepSize;

        if (currentPos.z > 0.0) break;

        vec4 projPos = uScene.projection * vec4(currentPos, 1.0);
        if (projPos.w <= 0.0) break; 
        
        projPos /= projPos.w;
        vec2 sampleUV = projPos.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) break; 

        float sampleDepth = texture(uDepthMap, sampleUV).r;
        if(sampleDepth >= 1.0) continue;

        vec3 sampleViewPos = GetViewPos(sampleUV, sampleDepth);
        float depthDiff = currentPos.z - sampleViewPos.z;

        // =========================================================
        // 【核心回归】：完全恢复你老版本的严苛碰撞逻辑！
        // =========================================================
        if (depthDiff < 0.0 && depthDiff > -safeThickness) {
            hitUV = sampleUV;
            hitResult = 1.0;
            break;
        }
    }

   if (hitResult > 0.5) {
        // =========================================================
        // 1. 强力屏幕边缘淡出 (专治拉伸形变)
        // =========================================================
        // 把 UV 转换到 -1 到 1 的 NDC 坐标
        vec2 screenNDC = hitUV * 2.0 - 1.0;
        // 找到距离屏幕边缘最近的轴
        float maxNDC = max(abs(screenNDC.x), abs(screenNDC.y));
        // 在屏幕边缘 20% 的区域内，让反射平滑消失，斩断拉伸的像素！
        float edgeFactor = 1.0 - smoothstep(0.8, 1.0, maxNDC);
        
        // =========================================================
        // 2. 菲涅尔视角衰减 (解决你说的“调整生效视角”)
        // =========================================================
        // 计算视线和法线的夹角。越垂直直视表面，NdotV 越接近 1；越斜视，越接近 0。
        float NdotV = max(dot(normalView, -viewDir), 0.0);
        
        // 菲涅尔公式：斜着看时反射极强 (1.0)，垂直直视时反射变弱 (这里设为最低保留 20% 反射)
        // 这样可以避免直视时看到 SSR 穿帮的空洞。
        float fresnelFade = mix(0.2, 1.0, pow(1.0 - NdotV, 3.0));
        
        // =========================================================
        // 3. 距离淡出 (保持原样)
        // =========================================================
        float distFade = 1.0 - smoothstep(safeMaxDist * 0.8, safeMaxDist, pixelDist);
        
        vec3 refColor = texture(uSceneColor, hitUV).rgb;
        float roughnessFade = clamp(1.0 - roughness * 2.0, 0.0, 1.0);
        
        // 终极 Alpha 混合：把边缘裁剪和视角衰减全部乘进去
        float alpha = edgeFactor * fresnelFade * distFade * roughnessFade;
        
        oSsrColor = vec4(refColor, alpha);
    } else {
        oSsrColor = vec4(0.0);
    }
}