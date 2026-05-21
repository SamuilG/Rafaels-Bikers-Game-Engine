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

// =========================================================
// 【补回缺失的函数】：伪随机哈希函数，用于生成抖动噪声
// =========================================================
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
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

    // =========================================================
    // 2. 远距离步长缩放
    // =========================================================
    float distFactor = smoothstep(10.0, safeMaxDist, pixelDist);
    float dynamicStepSize = mix(pc.stepSize, pc.stepSize * 1.5, distFactor);
    int dynamicMaxSteps = int(mix(pc.maxSteps, pc.maxSteps * 0.6, distFactor));
    
    //  【核心修复 1】：彻底移除 0.5 的强制厚度！完全信任外部传入的严格厚度
    float adaptiveThickness = pc.thickness;

   // =========================================================
    // 3. 移除随机起点抖动 (Jittering)，还你一个纯净的镜面！
    // =========================================================
    // 注释掉这两行：
    // float jitter = hash(gl_FragCoord.xy);
    // vec3 currentPos = viewPos + normalView * 0.05 + reflectDir * (0.05 + jitter * dynamicStepSize);
    
    // 替换为这行纯净平滑的起点：
    vec3 currentPos = viewPos + normalView * 0.05 + reflectDir * 0.05;
    
    vec3 lastPos = currentPos; // 记录上一步的位置，为二分查找做准备
    // =========================================================
    // 4. 开始 Ray Marching
    // =========================================================
    for (int i = 0; i < dynamicMaxSteps; ++i) {
        lastPos = currentPos;
        currentPos += reflectDir * dynamicStepSize;

        if (currentPos.z > 0.0) break; // 跑出屏幕背面

        vec4 projPos = uScene.projection * vec4(currentPos, 1.0);
        if (projPos.w <= 0.0) break; 
        
        projPos /= projPos.w;
        vec2 sampleUV = projPos.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) break; 

        float sampleDepth = texture(uDepthMap, sampleUV).r;
        if(sampleDepth >= 1.0) continue; // 击中天空，继续飞

        vec3 sampleViewPos = GetViewPos(sampleUV, sampleDepth);
        float depthDiff = currentPos.z - sampleViewPos.z;

        //  【核心修复 2】：严格厚度判定 + 触发二分查找
        if (depthDiff < 0.0 && depthDiff > -adaptiveThickness) {
            
            // ==========================================
            // 触发【二分查找】精确贴合表面，彻底消除断层！
            // ==========================================
            vec3 p1 = lastPos;     // 在表面外的上一个点
            vec3 p2 = currentPos;  // 钻进表面内的当前点
            vec3 midPoint;
            vec2 midUV;
            
            for(int j = 0; j < 5; ++j) {
                midPoint = mix(p1, p2, 0.5); // 取中点
                
                vec4 midProj = uScene.projection * vec4(midPoint, 1.0);
                midProj /= midProj.w;
                midUV = midProj.xy * 0.5 + 0.5;
                
                float d = texture(uDepthMap, midUV).r;
                vec3 vPos = GetViewPos(midUV, d);
                float dDiff = midPoint.z - vPos.z;
                
                if (dDiff < 0.0) {
                    p2 = midPoint; // 依然在物体内部，把内侧点往外拉
                } else {
                    p1 = midPoint; // 跑到物体外面了，把外侧点往内推
                }
            }
            
            // 二分查找结束，确认最终的精准击中点！
            hitUV = midUV;
            hitResult = 1.0;
            break;
        }
    }

   if (hitResult > 0.5) {
        // =========================================================
        // 1. 强力屏幕边缘淡出 (专治拉伸形变)
        // =========================================================
        vec2 screenNDC = hitUV * 2.0 - 1.0;
        float maxNDC = max(abs(screenNDC.x), abs(screenNDC.y));
        float edgeFactor = 1.0 - smoothstep(0.8, 1.0, maxNDC);
        
        // =========================================================
        // 2. 菲涅尔视角衰减 
        // =========================================================
        float NdotV = max(dot(normalView, -viewDir), 0.0);
        float fresnelFade = mix(0.2, 1.0, pow(1.0 - NdotV, 3.0));
        
        // =========================================================
        // 3. 距离淡出 
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