#version 450

#extension GL_EXT_scalar_block_layout : require

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
    float _pad;            // Offset 108 -> 总大小 112 字节
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
    vec4 params;    // x: cosInner, y: cosOuter, z: pad, w: pad
};

layout( scalar, set = 0, binding = 0 ) uniform UScene
{
    mat4 camera;      // Offset: 0
    mat4 projection;  // Offset: 64
    mat4 projCam;     // Offset: 128
    vec4 cameraPos;   // Offset: 192
    GpuLight lights[16]; // Offset: 208, Size: 16 * 48 = 768. End: 976
    
    vec4 lightPos;    // Offset: 976
    vec4 lightColor;  // Offset: 992
    
    uint lightCount;  
    uint renderMode;
    uint iblEnabled; 
    uint _pad1;

    mat4 lightVP[4];      
    vec4 cascadeSplits;   
} uScene;

layout( set = 0, binding = 1 ) uniform sampler2DArrayShadow uShadowMap;

// ==========================================
// 【关键新增】：绑定场景天空盒立方体纹理
// ==========================================
layout( set = 0, binding = 2 ) uniform samplerCube uSkyboxTexture;

const float PI = 3.14159265359;

vec3 LIGHT_POS = uScene.lightPos.xyz; 
vec3 LIGHT_COLOR = uScene.lightColor.rgb;

float D_Beckmann(float alpha, float NdotH)
{
	if( NdotH <= 0.0 ) return 0.0;
	float alpha2 = alpha * alpha;
	float NdotH2 = NdotH * NdotH;
	float num = exp( (NdotH2 - 1.0) / (alpha2 * NdotH2) );
	float den = PI * alpha2 * NdotH2 * NdotH2;
	return num / den;
}

float G1(float NdotV, float NdotH, float VdotH)
{
	if( VdotH <= 0.0 ) return 0.0;
	return (2.0 * NdotH * NdotV) / VdotH;
}

float G_CookTorrance(float NdotL, float NdotV, float NdotH, float VdotH)
{
	float g1 = G1(NdotV, NdotH, VdotH);
	float g2 = G1(NdotL, NdotH, VdotH); 
	return min(1.0, min(g1, g2));
}

const vec2 poissonDisk[16] = vec2[]( 
   vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ), 
   vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ), 
   vec2( -0.91588581, 0.45771432 ), vec2( -0.81544232, -0.87912464 ), 
   vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ), 
   vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ), 
   vec2( -0.26496911, -0.41893023 ), vec2( 0.79197514, 0.19090188 ), 
   vec2( -0.24188840, 0.99706507 ), vec2( -0.81409955, 0.91437590 ), 
   vec2( 0.19984126, 0.78641367 ), vec2( 0.14383161, -0.14100790 ) 
);

float calculate_shadow()
{
	vec4 viewPos = uScene.camera * vec4(v2fPos, 1.0);
	float fragDepth = abs(viewPos.z);

	int cascadeIdx = 3; 
	if (fragDepth < uScene.cascadeSplits.x) cascadeIdx = 0;
	else if (fragDepth < uScene.cascadeSplits.y) cascadeIdx = 1;
	else if (fragDepth < uScene.cascadeSplits.z) cascadeIdx = 2;

	vec4 lightSpacePos = uScene.lightVP[cascadeIdx] * vec4(v2fPos, 1.0);
	vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
	projCoords.xy = projCoords.xy * 0.5 + 0.5;

	if (projCoords.z > 1.0) return 1.0;

    float bias = 0.00005; 
    float currentDepth = projCoords.z - bias; 

	vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
	float shadow = 0.0;
	float filterSize = 2.5; 

	for(int i = 0; i < 16; ++i)
	{
		vec2 offset = poissonDisk[i] * texelSize * filterSize;
		shadow += texture(uShadowMap, vec4(projCoords.xy + offset, float(cascadeIdx), currentDepth));
	}
	return shadow / 16.0;
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
    // --- 1. 材质属性采样 ---
    vec4 texColor = texture(uTexColor, v2fTexCoord);
    vec3 baseColor = (texColor * pc.baseColorFactor).rgb;
 
    float finalAlpha = texColor.a * pc.baseColorFactor.a;

    if (finalAlpha < pc.alphaCutoff) {
        discard; 
    }

    vec3 emissive = texture(uTexEmissive, v2fTexCoord).rgb * pc.emissiveFactor.rgb;
    float emissiveIntensity = 5.0; 
    emissive *= emissiveIntensity;

    // --- ORM 贴图解析 ---
    vec4 pbrSample = texture(uTexRoughness, v2fTexCoord);
    vec4 aoSample  = texture(uTexAO, v2fTexCoord);

    float roughness = pbrSample.g * pc.roughnessFactor; 
    float metalness = pbrSample.b * pc.metallicFactor;  
    float ao        = aoSample.r;

    roughness = max(roughness, 0.02);

    // --- 2. 几何向量计算 ---
    vec3 N = getNormalFromMap();
    vec3 V = normalize(uScene.cameraPos.xyz - v2fPos);
    
    // --- 3. 阴影计算 ---
    float shadow = calculate_shadow(); 
    
    // --- 4. 多光源光照累加 ---
    vec3 totalLo = vec3(0.0);

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
        vec3 F0 = mix(vec3(0.04), baseColor, metalness);
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
        vec3 kS = F; 

        vec3 kD = (vec3(1.0) - kS) * (1.0 - metalness); 

        float LdotH = max(dot(L, H), 0.0);
        float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
        float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotL, 5.0);
        float viewScatter  = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotV, 5.0);
        vec3 disneyDiffuse = (baseColor / PI) * lightScatter * viewScatter;

        float alphaBRDF = max(roughness * roughness, 0.001);
        float D = D_Beckmann(alphaBRDF, NdotH);
        float G = G_CookTorrance(NdotL, NdotV, NdotH, VdotH);
        
        vec3 Lspecular = (D * G * F) / max(4.0 * NdotL * NdotV, 0.0001);

        totalLo += (kD * disneyDiffuse + Lspecular) * Li * NdotL;
    }
// =================================================================
    // 5. 环境光与最终颜色合成 (物理 IBL 采样 + 熔断保护)
    // =================================================================
    vec3 Lambient = vec3(0.0); // 默认环境光为 0 (死黑)

    // 【新增】：只有在 iblEnabled == 1 时才计算天空盒反射
    if (uScene.iblEnabled == 1) {
        vec3 F0_env = mix(vec3(0.04), baseColor, metalness);
        
        // a. 环境漫反射
        vec3 skyColorEnv = vec3(0.35, 0.45, 0.65) * 0.3; 
        vec3 groundColorEnv = vec3(0.05, 0.04, 0.03);    
        float hemiWeight = 0.5 * N.y + 0.5;
        vec3 ambientIrradiance = mix(groundColorEnv, skyColorEnv, hemiWeight);

        vec3 kS_env = F0_env + (max(vec3(1.0 - roughness), F0_env) - F0_env) * pow(1.0 - max(dot(N, V), 0.0001), 5.0);
        vec3 kD_env = (vec3(1.0) - kS_env) * (1.0 - metalness);
        vec3 LambientDiffuse = ambientIrradiance * baseColor * kD_env;

        // b. IBL 镜面环境反射 (倒影)
        vec3 R = reflect(-V, N);
        float lod = roughness * 8.0; 
        
        // 采样真实的天空盒 (如果没生成 Mipmap，这里会返回锐利的高清图)
        vec3 iblSpecularColor = textureLod(uSkyboxTexture, R, lod).rgb;

        // 【熔断保护】
        if (dot(iblSpecularColor, iblSpecularColor) < 0.0001) {
            iblSpecularColor = mix(groundColorEnv * 2.0, skyColorEnv * 6.5, R.y * 0.5 + 0.5);
        }

        // =================================================================
        // 【核心修复】：解决“万物皆可抛光”的塑料感问题
        // =================================================================
        // 1. 粗糙度衰减：如果天空盒没有 Mipmap 无法变模糊，我们就在它变粗糙时直接将其变暗。
        // smoothstep(0.3, 0.8) 表示粗糙度大于 0.3 开始变弱，大于 0.8 时完全失去镜面反射。
        float roughnessFade = 1.0 - smoothstep(0.3, 0.8, roughness);

        // 2. 金属度底色压制：真实的非金属（如砖块、木头）环境反射非常微弱。
        // 我们强行把非金属（metalness = 0.0）的 IBL 反光压制到原来的 15%。
        float metalFade = mix(0.15, 1.0, metalness); 

        // 叠加环境高光，并套用双重物理衰减！
        vec3 LambientSpecular = iblSpecularColor * kS_env * roughnessFade * metalFade;

        // c. 合并并应用 AO 遮蔽
        Lambient = (LambientDiffuse + LambientSpecular) * ao;
    }

    // 最终合并（如果 IBL 关闭，Lambient 就是 vec3(0.0)）
    vec3 finalColor = Lambient + totalLo + emissive;    // --- 6. Bloom 亮度提取 ---
    float threshold = 1.0; 
    vec3 bloomContrib = max(finalColor - vec3(threshold), vec3(0.0));
    oBrightColor = vec4(bloomContrib, 1.0);

    // --- 7. Tonemapping ---
    vec3 x = finalColor;
    vec3 mappedColor = clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0, 1.0);
    
    finalAlpha = texColor.a * pc.baseColorFactor.a;
    mappedColor *= finalAlpha; // PREMULTIPLY

    oColor = vec4(mappedColor, finalAlpha);
    oNormal = vec4(N, roughness);
}