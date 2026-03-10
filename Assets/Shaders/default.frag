#version 450

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec2 v2fTexCoord;
layout( location = 1 ) in vec3 v2fNormal;
layout( location = 2 ) in vec3 v2fPos;
layout( location = 3 ) in vec4 v2fLightProjPos;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( set = 1, binding = 1 ) uniform sampler2D uTexRoughness;
layout( set = 1, binding = 2 ) uniform sampler2D uTexMetalness;
struct GpuLight {
    vec4 position;  // xyz: position/direction, w: type (0:Dir, 1:Point)
    vec4 color;     // rgb: color, a: intensity
    vec4 params;    // x: range
};
layout( scalar, set = 0, binding = 0 ) uniform UScene
{
    mat4 camera;
    mat4 projection;
    mat4 projCam;
    vec4 cameraPos;
    GpuLight lights[16]; // 新增光源数组
    uint lightCount;     // 当前有效光源数
    vec4 lightPos;       // 保留旧变量以兼容原有逻辑（或将其废弃）
    vec4 lightColor;
    uint renderMode;
    uint _pad0;          // 匹配 C++ 中的 float _pad0[3]
    uint _pad1;
    uint _pad2;
    mat4 lightVP[4];      
    vec4 cascadeSplits;   
} uScene;

layout( set = 0, binding = 1 ) uniform sampler2DArrayShadow uShadowMap;

layout( location = 0 ) out vec4 oColor;

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
float calculate_shadow()
{
	// Select cascade based on view-space depth of the fragment
	// v2fPos is world-space; project to view space to get depth
	vec4 viewPos = uScene.camera * vec4(v2fPos, 1.0);
	float fragDepth = abs(viewPos.z);

	int cascadeIdx = 3; // default: last (outermost) cascade
	if      (fragDepth < uScene.cascadeSplits.x) cascadeIdx = 0;
	else if (fragDepth < uScene.cascadeSplits.y) cascadeIdx = 1;
	else if (fragDepth < uScene.cascadeSplits.z) cascadeIdx = 2;

	// Project fragment into the chosen cascade's light space
	vec4 lightSpacePos = uScene.lightVP[cascadeIdx] * vec4(v2fPos, 1.0);
	vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
	// projCoords are in [-1, 1], transform xy to [0, 1]
	projCoords.xy = projCoords.xy * 0.5 + 0.5;

	// check bounds
	if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
		projCoords.y < 0.0 || projCoords.y > 1.0 || 
		projCoords.z < 0.0 || projCoords.z > 1.0) 
	{
		return 1.0;
	}

	// PCF over 3x3 texel neighbourhood
	// textureSize returns (width, height, layers); we only need xy
	vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
	float shadow = 0.0;
	
	// sampler2DArrayShadow: texture(sampler, vec4(u, v, layer, compareRef))
	for(int x = -1; x <= 1; ++x)
	{
		for(int y = -1; y <= 1; ++y)
		{
			vec2 uv = projCoords.xy + vec2(x, y) * texelSize;
			shadow += texture(uShadowMap, vec4(uv, float(cascadeIdx), projCoords.z)); 
		}
	}
	shadow /= 9.0;

	return shadow;
}

void main()
{
	// material properties
	vec3 baseColor = texture(uTexColor, v2fTexCoord).rgb;
	float roughness = texture(uTexRoughness, v2fTexCoord).r;
	float metalness = texture(uTexMetalness, v2fTexCoord).r;

	// geometric vectors
	vec3 N = normalize(v2fNormal);
    vec3 V = normalize(uScene.cameraPos.xyz - v2fPos); 
    
    float shadow = calculate_shadow(); // 假设阴影仍只关联主定向光 (lights[0])
    
    vec3 totalLo = vec3(0.0);

    for (uint i = 0; i < uScene.lightCount; ++i)
    {
        GpuLight light = uScene.lights[i];
        vec3 L_dir;
        float attenuation = 1.0;

        if (light.position.w == 0.0) // 定向光 (Directional Light)
        {
            L_dir = light.position.xyz; // 此时 xyz 通常存储的是反向方向
        }
        else // 点光源 (Point Light)
        {
            L_dir = light.position.xyz - v2fPos;
            float dist = length(L_dir);
            // 简单的距离衰减：(1 - d/range)^2
            attenuation = clamp(1.0 - dist / light.params.x, 0.0, 1.0);
            attenuation *= attenuation;
        }

        vec3 L = normalize(L_dir);
        vec3 H = normalize(L + V);

        // 点积计算
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0001);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        // 只对第一个光源应用阴影（通常是定向太阳光）
        float currentShadow = (i == 0) ? shadow : 1.0;
        
        // 入射光能量
        vec3 Li = light.color.rgb * light.color.a * attenuation * currentShadow;

        // PBR 计算 (BRDF)
        vec3 F0 = mix(vec3(0.04), baseColor, metalness);
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
        vec3 Ldiffuse = (baseColor / PI) * (vec3(1.0) - F) * (1.0 - metalness);

        float alpha = max(roughness * roughness, 0.001);
        float D = D_Beckmann(alpha, NdotH);
        float G = G_CookTorrance(NdotL, NdotV, NdotH, VdotH);
        vec3 Lspecular = (D * G * F) / max(4.0 * NdotL * NdotV, 0.0001);

        totalLo += (Ldiffuse + Lspecular) * Li * NdotL;
    }

    vec3 Lambient = vec3(0.02) * baseColor;
    vec3 color = Lambient + totalLo;

	// debug modes
	if( uScene.renderMode == 6 ) color = vec3(shadow); 
    
    oColor = vec4(color, 1.0);
}
