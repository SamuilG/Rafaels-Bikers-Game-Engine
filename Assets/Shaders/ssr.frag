#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec2 v2fTexCoord;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uColorTex; 
layout(set = 0, binding = 1) uniform sampler2D uDepthTex; 

layout(scalar, set = 0, binding = 2) uniform UScene {
    mat4 camera;      // 视图矩阵
    mat4 projection;  // 投影矩阵
    mat4 projCam;     // 投影 * 视图矩阵
} uScene;

const int   MAX_STEPS = 120;      
const float STRIDE = 0.8;         
const float THICKNESS = 1.2;      
const float MAX_DISTANCE = 80.0;  

// 将深度（0..1）重建回视空间位置
vec3 reconstructViewPos(vec2 uv, float depth) {
    // 注意：在这个工程中 NDC.z 是 0..1（见项目中多处 invProjView 使用 near=0, far=1）
    // 所以不要把 depth 映射到 -1..1。直接把 depth 作为 NDC.z 进行逆投影。
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = inverse(uScene.projection) * ndc;
    return viewPos.xyz / viewPos.w;
}

void main() {
    vec4 sceneColor = texture(uColorTex, v2fTexCoord);
    float depth = texture(uDepthTex, v2fTexCoord).r;

    if (depth >= 0.9999) {
        oColor = sceneColor;
        return;
    }

    vec3 viewPos = reconstructViewPos(v2fTexCoord, depth);
    vec3 viewDir = normalize(-viewPos); // 视图空间中从表面指向相机的向量

    // 绝对向上的平面法线（world space）映射到视图空间
    vec3 worldUp = vec3(0.0, 1.0, 0.0);
    vec3 viewNormal = normalize(mat3(uScene.camera) * worldUp);

    vec3 reflectDir = normalize(reflect(-viewDir, viewNormal));

    float normalDotView = clamp(dot(viewNormal, viewDir), -1.0, 1.0);
    float fresnel = pow(1.0 - abs(normalDotView), 3.0);

    vec3 currentRayPos = viewPos + viewNormal * 0.1;
    bool hit = false;
    vec2 hitUV = vec2(0.0);

    for (int i = 0; i < MAX_STEPS; i++) {
        currentRayPos += reflectDir * STRIDE;

        if (length(currentRayPos - viewPos) > MAX_DISTANCE) break;

        vec4 clipPos = uScene.projection * vec4(currentRayPos, 1.0);
        vec3 ndcPos = clipPos.xyz / clipPos.w;
        vec2 sampleUV = ndcPos.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float sampleDepth = texture(uDepthTex, sampleUV).r;
        if (sampleDepth >= 0.9999) continue; 

        vec3 sampleViewPos = reconstructViewPos(sampleUV, sampleDepth);

        // 视空间中 z 通常为负，取负值变为正深度
        float rayDepthPos = -currentRayPos.z;
        float sceneDepthPos = -sampleViewPos.z;

        float depthDiff = rayDepthPos - sceneDepthPos;

        if (depthDiff > 0.0 && depthDiff < THICKNESS) {
            if (distance(sampleUV, v2fTexCoord) > 0.015) {
                hit = true;
                hitUV = sampleUV;
                break;
            }
        }
    }

    if (hit) {
        vec3 reflectionColor = texture(uColorTex, hitUV).rgb;

        vec2 edgeNear = smoothstep(vec2(0.0), vec2(0.12), hitUV);
        vec2 edgeFar  = 1.0 - smoothstep(vec2(0.88), vec2(1.0), hitUV);
        vec2 dCoords = edgeNear * edgeFar;
        float screenEdgeFactor = dCoords.x * dCoords.y;

        float baseIntensity = 0.5;
        float reflectIntensity = baseIntensity + (1.0 - baseIntensity) * fresnel;
        reflectIntensity *= screenEdgeFactor;

        oColor = vec4(mix(sceneColor.rgb, reflectionColor, reflectIntensity), sceneColor.a);
    } else {
        oColor = sceneColor;
    }
}