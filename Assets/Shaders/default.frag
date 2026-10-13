#version 450

#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require
#include "pbr_common.glsl"
#include "tonemap.glsl"
#include "environment.glsl"

// ==========================================
// 1. 结构体定义 (必须与 C++ 严格对齐)
// ==========================================
layout(push_constant) uniform PushConstants {
    mat4 transform;        // Offset 0
    vec4 baseColorFactor;  // Offset 64
    vec4 emissiveFactor;   // Offset 80
    float metallicFactor;  // Offset 96
    float roughnessFactor; // Offset 100
    float alphaCutoff;     // Offset 104
    float _pad;            // Offset 108
    vec4 clipPlane;        // Offset 112
} pc;
layout( location = 0 ) out vec4 oColor;       // 正常场景颜色
layout( location = 1 ) out vec4 oBrightColor; // 提取的亮度颜
layout( location = 2 ) out vec4 oNormal;

layout( location = 0 ) in vec2 v2fTexCoord;
layout( location = 1 ) in vec3 v2fNormal;
layout( location = 2 ) in vec3 v2fPos;

layout( location = 3 ) in vec4 v2fLightProjPos;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( set = 1, binding = 1 ) uniform sampler2D uTexRoughness;
layout( set = 1, binding = 2 ) uniform sampler2D uTexMetalness;
layout( set = 1, binding = 3 ) uniform sampler2D uTexEmissive;
layout( set = 1, binding = 4 ) uniform sampler2D uTexNormal;
layout( set = 1, binding = 5 ) uniform sampler2D uTexAO;

struct GpuLight {
    vec4 position;  // xyz: position/direction, w: type (0:Dir, 1:Point, 2:Spot)
    vec4 color;     // rgb: color, a: intensity
    vec4 direction; // xyz: direction, w: range
    vec4 params;    // x: cosInner, y: cosOuter, z: specularMultiplier, w: pad
};

layout( scalar, set = 0, binding = 0 ) uniform UScene
{
    mat4 camera;      // Offset: 0
    mat4 projection;  // Offset: 64
    mat4 projCam;     // Offset: 128
    vec4 cameraPos;   // Offset: 192
    GpuLight lights[16]; // Offset: 208, Size: 16 * 64 = 1024. End: 1232

    vec4 lightPos;    // Offset: 1232
    vec4 lightColor;  // Offset: 1248

    uint lightCount;
    uint renderMode;
    uint iblEnabled;
    uint _pad1;

    mat4 lightVP[4];
    vec4 cascadeSplits;
    vec4 portalClipPlane;
    vec4 iblParams;       // global / diffuse / specular strength, rotation in radians
    vec4 environmentTint; // RGB multiplier and saturation
} uScene;

layout( set = 0, binding = 1 ) uniform sampler2DArrayShadow uShadowMap;

// ==========================================
// Split-sum IBL resources, matching scene descriptor bindings 2–4.
// ==========================================
layout(set = 0, binding = 2) uniform samplerCube uPrefilteredEnvironment;
layout(set = 0, binding = 3) uniform samplerCube uDiffuseEnvironment;
layout(set = 0, binding = 4) uniform sampler2D uBrdfLut;
layout(constant_id = 0) const bool kLdrOutput = false;

const float PI = 3.14159265359;

vec3 LIGHT_POS = uScene.lightPos.xyz;
vec3 LIGHT_COLOR = uScene.lightColor.rgb;


#include "shadow_sampling.glsl"

// Ordered 4x4 dither used by the camera occlusion fade.  The object stays in
// the opaque, depth-writing pipeline; only individual fragments are removed.
float ditherThreshold4x4(vec2 fragCoord)
{
    ivec2 cell = ivec2(mod(floor(fragCoord), 4.0));
    int index = cell.x + cell.y * 4;
    const float bayer[16] = float[](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0
    );
    return (bayer[index] + 0.5) / 16.0;
}

vec3 getNormalFromMap()
{
    vec3 sampled = texture(uTexNormal, v2fTexCoord).xyz;
    vec3 tangentNormal = sampled * 2.0 - 1.0;

    float normalScale = 2.5;
    tangentNormal.xy *= normalScale;
    tangentNormal = normalize(tangentNormal);

    vec3 q1  = dFdx(v2fPos);
    vec3 q2  = dFdy(v2fPos);
    vec2 st1 = dFdx(v2fTexCoord);
    vec2 st2 = dFdy(v2fTexCoord);

    vec3 N   = normalize(v2fNormal);
    vec3 T   = normalize(q1 * st2.t - q2 * st1.t);
    vec3 B   = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

void main()
{
    if (dot(uScene.portalClipPlane.xyz, uScene.portalClipPlane.xyz) > 0.0001 &&
        dot(vec4(v2fPos, 1.0), uScene.portalClipPlane) < 0.0) {
        discard;
    }
    if (dot(pc.clipPlane.xyz, pc.clipPlane.xyz) > 0.0001 &&
        dot(vec4(v2fPos, 1.0), pc.clipPlane) < 0.0) {
        discard;
    }

    // --- 1. 材质属性采样 ---
    vec4 texColor = texture(uTexColor, v2fTexCoord);
    vec3 baseColor = (texColor * pc.baseColorFactor).rgb;

    float finalAlpha = texColor.a * pc.baseColorFactor.a;

    if (finalAlpha < pc.alphaCutoff) {
        discard;
    }

    // emissiveFactor.a is unused by the material model and carries the
    // per-instance dither coverage for both static and skinned meshes.
    float ditherFade = clamp(pc.emissiveFactor.a, 0.0, 1.0);
    if (ditherFade < 0.9999 && ditherFade <= ditherThreshold4x4(gl_FragCoord.xy)) {
        discard;
    }

    vec3 emissive = texture(uTexEmissive, v2fTexCoord).rgb * pc.emissiveFactor.rgb;
    float emissiveIntensity = 5.0;
    emissive *= emissiveIntensity;

    // --- ORM 贴图解析 ---
    vec4 pbrSample = texture(uTexRoughness, v2fTexCoord);
    vec4 aoSample  = texture(uTexAO, v2fTexCoord);

    float roughness = clamp(pbrSample.g * pc.roughnessFactor, 0.02, 1.0);
    float metalness = clamp(pbrSample.b * pc.metallicFactor, 0.0, 1.0);
    float ao        = aoSample.r;


    // --- 2. 几何向量计算 ---
    vec3 N = getNormalFromMap();
    vec3 V = normalize(uScene.cameraPos.xyz - v2fPos);
    // =================================================================
    // 【核心修复】：高光抗锯齿 (Specular Anti-Aliasing)
    // 动态侦测法线变化率，抹平闪烁的高光点！
    // =================================================================
    vec3 dNdX = dFdx(N);
    vec3 dNdY = dFdy(N);
    // 计算法线的方差（变化有多剧烈）
    float normalVariance = (dot(dNdX, dNdX) + dot(dNdY, dNdY));

    // 根据方差动态补偿一层“几何粗糙度”
    // 这里的 0.35 是控制抗锯齿强度的系数，你可以根据喜好在 0.2 ~ 0.5 之间微调
    float geomRoughness = normalVariance * 0.05;

    // 最终粗糙度：取材质本身粗糙度和几何补偿粗糙度的最大值
    roughness = clamp(max(roughness, geomRoughness), 0.02, 1.0);


    // --- 3. 阴影计算 ---
    float shadow = calculate_shadow();

    // --- 4. 多光源光照累加 ---
    vec3 totalLo = vec3(0.0);
    vec3 F0 = mix(vec3(0.04), baseColor, metalness);
    float NoV = clamp(dot(N, V), 0.0001, 1.0);
    vec2 dfg = textureLod(uBrdfLut, vec2(NoV, roughness), 0.0).rg;
    vec3 energyCompensation = pbrEnergyCompensation(F0, dfg);

    for (uint i = 0; i < uScene.lightCount; ++i)
    {
        GpuLight light = uScene.lights[i];
        vec3 L_dir;
        float attenuation = 1.0;

        if (light.position.w == 0.0)
        {
            L_dir = light.position.xyz;
        }
        else if (light.position.w == 1.0)
        {
            L_dir = light.position.xyz - v2fPos;
            float dist = length(L_dir);
            attenuation = clamp(1.0 - dist / light.direction.w, 0.0, 1.0);
            attenuation *= attenuation;
        }
        else
        {
            L_dir = light.position.xyz - v2fPos;
            float dist = length(L_dir);
            float distAtten = clamp(1.0 - dist / light.direction.w, 0.0, 1.0);
            distAtten *= distAtten;

            vec3 L_spot = normalize(L_dir);
            vec3 SpotDir = normalize(light.direction.xyz);

            float theta = dot(L_spot, -SpotDir);
            float epsilon = light.params.x - light.params.y;
            float spotAtten = clamp((theta - light.params.y) / epsilon, 0.0, 1.0);

            float spotShadow = 1.0;
            vec4 lightSpacePos = uScene.lightVP[3] * vec4(v2fPos, 1.0);
            vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
            projCoords.xy = projCoords.xy * 0.5 + 0.5;

            if (lightSpacePos.w > 0.0 &&
                projCoords.z >= 0.0 && projCoords.z <= 1.0 &&
                projCoords.x >= 0.0 && projCoords.x <= 1.0 &&
                projCoords.y >= 0.0 && projCoords.y <= 1.0)
            {
                float s_bias = 0.002;
                spotShadow = texture(uShadowMap, vec4(projCoords.xy, 3.0, projCoords.z - s_bias));
            }

            attenuation = distAtten * spotAtten * spotShadow;
        }

        vec3 L = normalize(L_dir);
        vec3 H = normalize(L + V);

        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0001);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        float currentShadow = (light.position.w == 0.0) ? shadow : 1.0;
        vec3 Li = light.color.rgb * light.color.a * attenuation * currentShadow;

        // --- 物理 BRDF ---

        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
        vec3 kS = F;

        vec3 kD = (vec3(1.0) - kS) * (1.0 - metalness);

        float LdotH = max(dot(L, H), 0.0);
        float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
        float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotL, 5.0);
        float viewScatter  = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotV, 5.0);
        vec3 disneyDiffuse = (baseColor / PI) * lightScatter * viewScatter;

        float alphaBRDF = pbrAlpha(roughness);
        float D = D_GGX(alphaBRDF, NdotH);
        float visibility = V_SmithGGXCorrelated(alphaBRDF, NdotV, NdotL);
        vec3 Lspecular = D * visibility * F * energyCompensation;
        // =========================================================
        // 【核心修改】：应用从 C++ 传来的高光系数
        // =========================================================
        float specMultiplier = light.params.z;
        Lspecular *= specMultiplier; // 瞬间抹除或减弱特定光源的高光！

        // =========================================================
        // 【新增防闪烁 1】：防止微平面高光除以极小值导致数值爆炸
        // =========================================================
     //   Lspecular = min(Lspecular, vec3(20.0));
        totalLo += (kD * disneyDiffuse + Lspecular) * Li * NdotL;
    }
// =================================================================
    // 5. Split-sum environment lighting and final HDR radiance.
    // =================================================================
    vec3 Lambient = vec3(0.0);
    if (uScene.iblEnabled == 1) {
        // The cosine-convolved cubemap stores irradiance / pi.
        vec3 ambientIrradiance = textureLod(uDiffuseEnvironment, environmentDirection(N, uScene.iblParams.w), 0.0).rgb;
        ambientIrradiance = environmentColor(ambientIrradiance, uScene.environmentTint);
        vec3 kS_env = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NoV, 5.0);
        vec3 kD_env = (vec3(1.0) - kS_env) * (1.0 - metalness);
        vec3 LambientDiffuse = ambientIrradiance * baseColor * kD_env;
        vec3 R = reflect(-V, N);
        float lod = roughness * float(textureQueryLevels(uPrefilteredEnvironment) - 1);
        vec3 prefiltered = textureLod(uPrefilteredEnvironment, environmentDirection(R, uScene.iblParams.w), lod).rgb;
        prefiltered = environmentColor(prefiltered, uScene.environmentTint);
        vec3 LambientSpecular = prefiltered * (F0 * dfg.x + vec3(dfg.y)) * energyCompensation;
        Lambient = (LambientDiffuse * max(uScene.iblParams.y, 0.0) +
            LambientSpecular * max(uScene.iblParams.z, 0.0)) * ao * max(uScene.iblParams.x, 0.0);
    }
    // 最终合并（如果 IBL 关闭，Lambient 就是 vec3(0.0)）
    // Bound storage to finite RGBA16F radiance before Bloom extraction.
    vec3 finalColor = clamp(Lambient + totalLo + emissive, vec3(0.0), vec3(65504.0));
    // --- 6. Bloom 亮度提取与防闪烁 (Firefly Filter) ---
    float threshold = 1.0;
    vec3 bloomContrib = max(finalColor - vec3(threshold), vec3(0.0));

    // =========================================================
    // 【新增防闪烁 2】：Bloom 亮度钳制 (Karis Luminance Clamp)
    // 斩断那些在远距离因为单像素高频法线而产生的爆炸性高光
    // =========================================================
    float maxBloom = 5.0; // 允许进入 Bloom 的最大亮度倍数（可微调 2.0 ~ 5.0）
    float lum = dot(bloomContrib, vec3(0.2126, 0.7152, 0.0722)); // 计算真实感知亮度
    if (lum > maxBloom) {
        // 如果亮度超标，按比例整体压暗，保持颜色色相不变
        bloomContrib *= (maxBloom / lum);
    }

    oBrightColor = vec4(bloomContrib, 1.0);
    // Scene and portal views remain linear HDR through the composite pass.
    vec3 outputColor = kLdrOutput ? linearToSrgb(toneMapAces(finalColor)) : finalColor;
    oColor = vec4(outputColor * finalAlpha, finalAlpha);
    oNormal = vec4(normalize(N), roughness);
}
