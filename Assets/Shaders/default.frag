#version 450

#extension GL_EXT_scalar_block_layout : require

//TODO AO

// ==========================================
// 1. 结构体定义 (必须与 C++ 严格对齐)
// ==========================================
layout(push_constant) uniform PushConstants {
    mat4 transform;        // 64 字节
    vec4 baseColorFactor;  // 16 字节
    vec4 emissiveFactor;   // 16 字节
    float metallicFactor;  // 4 字节
    float roughnessFactor; // 4 字节
    float alphaCutoff;     // 4 字节
    float _pad;            // 4 字节补齐
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

// 【无需 C++ 传入切线的法线映射】
vec3 getNormalFromMap()
{
    vec3 sampled = texture(uTexNormal, v2fTexCoord).xyz;
    
    // 1. 如果采样出来太暗（说明被sRGB误伤了），这里可以做一个简单的线性补偿
    // sampled = pow(sampled, vec3(2.2)); // 如果感觉还是太平，可以尝试取消这行注释

    // 2. 将 [0, 1] 映射到 [-1, 1]
    vec3 tangentNormal = sampled * 2.0 - 1.0;

    // ==========================================
    // 【关键增强】：手动放大 XY 轴的偏移量
    // 调整 scale 值，1.0 是原始，2.0 是双倍凹凸，4.0 是极强凹凸
    float normalScale = 2.5; 
    tangentNormal.xy *= normalScale;
    tangentNormal = normalize(tangentNormal); 
    // ==========================================

    // 下面的 TBN 计算保持不变
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
{// // --- 1. 材质属性采样 ---
    vec4 texColor = texture(uTexColor, v2fTexCoord);
    vec3 baseColor = (texColor * pc.baseColorFactor).rgb;
 
    float finalAlpha = texColor.a * pc.baseColorFactor.a;

    // 无论是否遮挡，只要是 Mask 材质，就执行裁剪
    if (finalAlpha < pc.alphaCutoff) {
        discard; 
    }
    // 【新增】：采样自发光贴图
    // glTF 标准下，emissiveFactor 应该乘上贴图颜色
   // 强制将 vec4 转换为 vec3 再进行乘法
    vec3 emissive = texture(uTexEmissive, v2fTexCoord).rgb * pc.emissiveFactor.rgb;
   
    // 【关键微调】：给自发光一个“强度倍率”
    // 在 PBR HDR 管线中，自发光强度通常需要 > 1.0 才能触发 Bloom 发光效果
    // 建议先乘个 5.0 看看效果，后续可以在 pc (Push Constants) 中传进来
    float emissiveIntensity = 5.0; 
    emissive *= emissiveIntensity;


    // 【关键修复：读取 glTF 标准的 ORM 贴图】
    vec4 pbrSample = texture(uTexRoughness, v2fTexCoord);
    vec4 aoSample = texture(uTexAO, v2fTexCoord);

    // 1. 设置默认安全回退值（当地板/物体没有任何贴图时生效）
    // 默认大部分未贴图物体都是粗糙的 (0.8)，且非金属 (0.0)
    float roughness = 0.8 * pc.roughnessFactor; 
    float metalness = 0.0 * pc.metallicFactor;  
    float ao = 1.0;

    // 2. 智能检测：如果采出来的不是纯白也不是纯黑，说明是真正有效的材质贴图！
    bool isPbrValid = !( (pbrSample.r > 0.98 && pbrSample.g > 0.98 && pbrSample.b > 0.98) || 
                         (pbrSample.r < 0.02 && pbrSample.g < 0.02 && pbrSample.b < 0.02) );

 if (isPbrValid) {
        // 【核心破案】：应对 Blender 导出器忽略 Invert 节点的 Bug！
        // 它把黑色的图直接传了进来，我们在 Shader 里强行替它反相 (1.0 - x)
        // 并加一个 max 保护，防止 pc.roughnessFactor 为 0 导致变成绝对镜面
        roughness = (1.0 - pbrSample.g) * max(pc.roughnessFactor, 0.05); 
        metalness = pbrSample.b * pc.metallicFactor;  
    }

    // 同理，检测 AO 贴图是否有效
    bool isAoValid = !( (aoSample.r > 0.98 && aoSample.g > 0.98 && aoSample.b > 0.98) || 
                        (aoSample.r < 0.02 && aoSample.g < 0.02 && aoSample.b < 0.02) );

    if (isAoValid) {
        // AO 永远储存在 ORM 图的 R 通道！
        ao = aoSample.r;
    }
    // --- 2. 几何向量计算 ---
 //   vec3 N = normalize(v2fNormal);
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
        // ==========================================
        // 核心 BRDF 计算开始
        // ==========================================
        
        // 1. 基础菲涅尔 (Fresnel) - 代表光被镜面反射的比例 (kS)
        vec3 F0 = mix(vec3(0.04), baseColor, metalness);
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
        vec3 kS = F; 

        // 2. 严格的能量守恒 (Energy Conservation)
        // 射入的光 = 被反射的光(kS) + 被吸收/漫反射的光(kD)
        vec3 kD = vec3(1.0) - kS; 
        kD *= 1.0 - metalness; // 纯金属没有内部漫反射，强行归零

        // 3. Disney Diffuse 漫反射模型 (替代老旧的 baseColor / PI)
        // 考虑了材质粗糙度，能在粗糙物体的边缘产生真实的微表面逆反射
        float LdotH = max(dot(L, H), 0.0);
        float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
        float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotL, 5.0);
        float viewScatter  = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotV, 5.0);
        vec3 disneyDiffuse = (baseColor / PI) * lightScatter * viewScatter;

        // 将能量比例 kD 赋予漫反射
        vec3 Ldiffuse = kD * disneyDiffuse;

        // 4. 高光计算 (暂时保留你原有的 Beckmann)
        float alpha = max(roughness * roughness, 0.001);
        float D = D_Beckmann(alpha, NdotH);
        float G = G_CookTorrance(NdotL, NdotV, NdotH, VdotH);
        
        // 分母加上 0.0001 防止视线与法线垂直时除以 0 导致像素爆炸爆白
        vec3 Lspecular = (D * G * F) / max(4.0 * NdotL * NdotV, 0.0001);

        // 5. 最终能量合并：漫反射 + 镜面高光
        totalLo += (Ldiffuse + Lspecular) * Li * NdotL;
    }

   // --- 5. 环境光与最终颜色合成 ---
    
    // 【 (Hemisphere Ambient Light)】
    // 模拟来自天空的漫反射光 (偏蓝) 和来自地面的反弹光 (偏暗棕/灰)
    vec3 skyColor = vec3(0.35, 0.45, 0.65) * 0.3; // 天光颜色及强度 (太暗就调大 0.6)
    vec3 groundColor = vec3(0.05, 0.04, 0.03);    // 地光通常很暗，带点泥土色

    // 利用表面法线 N 的 Y 分量 (-1 到 1) 映射到 (0 到 1) 
    // 表面朝上 (N.y=1)，hemiWeight=1，完全受天光照亮
    // 表面朝下 (N.y=-1)，hemiWeight=0，完全受地光照亮
    // 侧面 (N.y=0)，hemiWeight=0.5，混合天光和地光
    float hemiWeight = 0.5 * N.y + 0.5;
    vec3 ambientIrradiance = mix(groundColor, skyColor, hemiWeight);

    // 计算最终环境光 (如果你上一回合应用了 Disney Diffuse，可以乘上 kD 确保物理守恒)
    // vec3 Lambient = ambientIrradiance * baseColor * kD; 
    // 如果没有 kD，直接这样写：
    vec3 Lambient = ambientIrradiance * baseColor;

    //vec3 finalColor = Lambient + totalLo;
    vec3 finalColor = Lambient + totalLo + emissive;
 

// 调试模式覆盖
    if( uScene.renderMode == 6 ) finalColor = vec3(shadow); 
    
    // ========================================================
    // --- 6. Bloom 亮度提取 (必须在 HDR 空间进行) ---
    // ========================================================
    float threshold = 1.0; // PBR 物理光照下，超过 1.0 的才是真正的泛光
    vec3 bloomContrib = max(finalColor - vec3(threshold), vec3(0.0));
    oBrightColor = vec4(bloomContrib, 1.0);

    // ========================================================
    // --- 7. ACES 电影级色调映射 (Tonemapping) ---
    // ========================================================
    // 将极其耀眼的物理光照 (HDR) 平滑地压缩到屏幕能显示的 0-1 (SDR) 范围内。
    vec3 x = finalColor;
    vec3 mappedColor = clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0, 1.0);
    
    // 提取真正的透明度
    finalAlpha = texColor.a * pc.baseColorFactor.a;

    // 将输出颜色预乘 alpha（配合混合因子 ONE / ONE_MINUS_SRC_ALPHA）
    mappedColor *= finalAlpha; // PREMULTIPLY

    // 输出最终颜色到 Attachment 0 (正常场景)
    oColor = vec4(mappedColor, finalAlpha);
}