#version 450

#extension GL_EXT_scalar_block_layout : require

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    vec2 pad;
} pc;

layout( location = 0 ) out vec4 oColor;       // 正常场景颜色
layout( location = 1 ) out vec4 oBrightColor; // 提取的亮度颜

layout( location = 0 ) in vec2 v2fTexCoord;
layout( location = 1 ) in vec3 v2fNormal;
layout( location = 2 ) in vec3 v2fPos;
layout( location = 3 ) in vec4 v2fLightProjPos;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( set = 1, binding = 1 ) uniform sampler2D uTexRoughness;
layout( set = 1, binding = 2 ) uniform sampler2D uTexMetalness;
layout( set = 1, binding = 3 ) uniform sampler2D uTexEmissive;
struct GpuLight {
    vec4 position;  // xyz: position/direction, w: type (0:Dir, 1:Point, 2:Spot)
    vec4 color;     // rgb: color, a: intensity
    vec4 direction; // 【新增】xyz: direction, w: range
    vec4 params;    // 【新增】x: cosInner, y: cosOuter, z: pad, w: pad
};
layout( scalar, set = 0, binding = 0 ) uniform UScene
{
    mat4 camera;      // Offset: 0
    mat4 projection;  // Offset: 64
    mat4 projCam;     // Offset: 128
    vec4 cameraPos;   // Offset: 192
    GpuLight lights[16]; // Offset: 208, Size: 16 * 48 = 768. End: 976
    
    // 关键修复：976 是 16 的倍数 (976 / 16 = 61)
    // 所以紧跟在 lights 后面的 vec4 必须从 976 开始
    vec4 lightPos;    // Offset: 976
    vec4 lightColor;  // Offset: 992
    
    // 把 uint 放在最后，它们不需要 16 字节对齐
    uint lightCount;  
    uint renderMode;
    uint _pad0; 
    uint _pad1;

    mat4 lightVP[4];      
    vec4 cascadeSplits;   
} uScene;

layout( set = 0, binding = 1 ) uniform sampler2DArrayShadow uShadowMap;



const float PI = 3.14159265359;

// simple light source
vec3 LIGHT_POS = uScene.lightPos.xyz; // world space
vec3 LIGHT_COLOR = uScene.lightColor.rgb;

// beckmann distribution function (NDF)
float D_Beckmann(float alpha, float NdotH)
{
	if( NdotH <= 0.0 ) return 0.0;

	float alpha2 = alpha * alpha;
	float NdotH2 = NdotH * NdotH;
	
	// equation from pdf
	float num = exp( (NdotH2 - 1.0) / (alpha2 * NdotH2) );
	float den = PI * alpha2 * NdotH2 * NdotH2;
	
	return num / den;
}

// simple helper for cook-torrance masking
float G1(float NdotV, float NdotH, float VdotH)
{
	if( VdotH <= 0.0 ) return 0.0;
	return (2.0 * NdotH * NdotV) / VdotH;
}

// cook-torrance geometric shadowing/masking
float G_CookTorrance(float NdotL, float NdotV, float NdotH, float VdotH)
{
	float g1 = G1(NdotV, NdotH, VdotH);
	float g2 = G1(NdotL, NdotH, VdotH); // using L instead of V for second term assumption logic symmetric
	return min(1.0, min(g1, g2));
}

// p2_1.5 PCF with CSM cascade selection

//poisson disk offsets for sampling around the current pixel in shadow map
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

	//samll bias to prevent shadow acne
    float bias = 0.00005; 
    
    float currentDepth = projCoords.z - bias; 

	
	vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
	float shadow = 0.0;
	
	// softer when bigger
    
	float filterSize = 2.5; 

	for(int i = 0; i < 16; ++i)
	{

		vec2 offset = poissonDisk[i] * texelSize * filterSize;
     
		shadow += texture(uShadowMap, vec4(projCoords.xy + offset, float(cascadeIdx), currentDepth));
	}
	
	return shadow / 16.0;
}
void main()
{

    // --- 1. 材质属性采样 ---
 

    // --- 【关键修改】：贴图颜色 乘以 基础颜色因子 ---
    // 如果没有贴图，uTexColor 返回我们刚改的纯白 (1,1,1,1)，乘出来就是纯色！
    vec4 texColor = texture(uTexColor, v2fTexCoord);
    vec3 baseColor = (texColor * pc.baseColorFactor).rgb;
    
    // 粗糙度和金属度也一样，结合贴图通道和因子
    float roughness = texture(uTexRoughness, v2fTexCoord).r * pc.roughnessFactor;
    float metalness = texture(uTexMetalness, v2fTexCoord).r * pc.metallicFactor;
    // --- 2. 几何向量计算 ---
    vec3 N = normalize(v2fNormal);
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

        if (light.position.w == 0.0) // 1. 定向光 (Directional)
        {
            L_dir = light.position.xyz; 
        }
        else if (light.position.w == 1.0) // 2. 点光源 (Point)
        {
            L_dir = light.position.xyz - v2fPos;
            float dist = length(L_dir);
            // 距离衰减 (Range 存在 direction.w 里)
            attenuation = clamp(1.0 - dist / light.direction.w, 0.0, 1.0);
            attenuation *= attenuation;
        }
       else // 3. 聚光灯/车头灯 (Spotlight - w == 2.0)
        {
            L_dir = light.position.xyz - v2fPos;
            float dist = length(L_dir);
            
            // a. 距离衰减
            float distAtten = clamp(1.0 - dist / light.direction.w, 0.0, 1.0);
            distAtten *= distAtten;

            // b. 聚光灯边缘衰减 (Spotlight Cone)
            vec3 L = normalize(L_dir); // 当前像素到光源的方向
            vec3 SpotDir = normalize(light.direction.xyz); // 聚光灯的物理朝向
            
            // 计算夹角的 cos 值 (反向点乘)
            float theta = dot(L, -SpotDir); 
            float epsilon = light.params.x - light.params.y; // cosInner - cosOuter
            float spotAtten = clamp((theta - light.params.y) / epsilon, 0.0, 1.0);

            // ==========================================
            // c. 【新增】车头灯阴影采样
            float spotShadow = 1.0;
            
            // 将当前像素转换到车灯的投影空间 (使用第 4 个矩阵)
            vec4 lightSpacePos = uScene.lightVP[3] * vec4(v2fPos, 1.0);
            
            // 透视除法
            vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
            projCoords.xy = projCoords.xy * 0.5 + 0.5; // 映射到 [0, 1] 纹理坐标
            
            // 确保像素在车灯的前方，并且在阴影视锥体内
            if (lightSpacePos.w > 0.0 && 
                projCoords.z >= 0.0 && projCoords.z <= 1.0 &&
                projCoords.x >= 0.0 && projCoords.x <= 1.0 &&
                projCoords.y >= 0.0 && projCoords.y <= 1.0) 
            {
                float bias = 0.002; // 防止阴影失真的偏移值
                // 去第 4 层 (数组索引为 3.0) 采样深度，并与当前深度比较
                spotShadow = texture(uShadowMap, vec4(projCoords.xy, 3.0, projCoords.z - bias));
            }

            // 综合所有衰减：距离 * 边缘平滑 * 阴影遮挡
            attenuation = distAtten * spotAtten * spotShadow;
            // ==========================================
        }

     
        vec3 L = normalize(L_dir);
        vec3 H = normalize(L + V);

        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0001);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

      
        float currentShadow = 1.0;
        // 只有当这个灯是平行光时，才给它应用级联阴影！
        if (light.position.w == 0.0) { 
            currentShadow = shadow; // shadow 是你在循环外用 calculate_shadow() 算好的
        }
        
        vec3 Li = light.color.rgb * light.color.a * attenuation * currentShadow;
        // BRDF 计算
        vec3 F0 = mix(vec3(0.04), baseColor, metalness);
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
        vec3 Ldiffuse = (baseColor / PI) * (vec3(1.0) - F) * (1.0 - metalness);

        float alpha = max(roughness * roughness, 0.001);
        float D = D_Beckmann(alpha, NdotH);
        float G = G_CookTorrance(NdotL, NdotV, NdotH, VdotH);
        vec3 Lspecular = (D * G * F) / max(4.0 * NdotL * NdotV, 0.0001);

        totalLo += (Ldiffuse + Lspecular) * Li * NdotL;
    }

    // --- 5. 环境光与最终颜色合成 ---
    vec3 Lambient = vec3(0.02) * baseColor;
    vec3 finalColor = Lambient + totalLo;

    // emissive
    vec3 emissiveColor = texture(uTexEmissive, v2fTexCoord).rgb;
    
    finalColor += emissiveColor * 5.0; 
 

    // 调试模式覆盖
    if( uScene.renderMode == 6 ) finalColor = vec3(shadow); 
    
    // 输出到 Attachment 0 (正常渲染图)
    oColor = vec4(finalColor, 1.0);


   // --- 6. Bloom 亮度提取 ---
    float threshold = 0.5; // 阈值可以根据你的光源强度调整
    
    // 只保留超过阈值的部分（这样光晕边缘会像羽化一样柔和）
    vec3 bloomContrib = max(finalColor - vec3(threshold), vec3(0.0));
    
    oBrightColor = vec4(bloomContrib, 1.0);

 

}
