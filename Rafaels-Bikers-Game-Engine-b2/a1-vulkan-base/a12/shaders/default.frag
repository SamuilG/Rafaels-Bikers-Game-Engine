#version 450

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec2 v2fTexCoord;
layout( location = 1 ) in vec3 v2fNormal;
layout( location = 2 ) in vec3 v2fPos;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( set = 1, binding = 1 ) uniform sampler2D uTexRoughness;
layout( set = 1, binding = 2 ) uniform sampler2D uTexMetalness;

layout( scalar, set = 0, binding = 0 ) uniform UScene
{
	mat4 camera;
	mat4 projection;
	mat4 projCam;
	vec4 cameraPos;
	vec4 lightPos;
	vec4 lightColor;
	uint renderMode;
	uint _pad0;
	uint _pad1;
	uint _pad2;
	mat4 lightVP[4];
	vec4 cascadeSplits;
} uScene;

// Use Texture Array for CSM
layout( set = 0, binding = 1 ) uniform sampler2DArrayShadow uShadowMap;

layout( location = 0 ) out vec4 oColor;

const float PI = 3.14159265359;

float calculate_shadow()
{
	// 1. Select Cascade based on view-space depth
	vec4 posView = uScene.camera * vec4(v2fPos, 1.0);
	float depth = abs(posView.z);

	uint cascadeIndex = 0;
	for(uint i = 0; i < 3; ++i) {
		if(depth > uScene.cascadeSplits[i]) {
			cascadeIndex = i + 1;
		}
	}

	// 2. Project to the selected light space
	vec4 shadowPos = uScene.lightVP[cascadeIndex] * vec4(v2fPos, 1.0);
	vec3 projCoords = shadowPos.xyz / shadowPos.w;

	// Transform NDC to UV
	projCoords.xy = projCoords.xy * 0.5 + 0.5;

	if (projCoords.z < 0.0 || projCoords.z > 1.0) return 1.0;

	// 3. PCF 3x3 on Texture Array
	float shadow = 0.0;
	vec2 texelSize = 1.0 / textureSize(uShadowMap, 0).xy;
	
	for(int x = -1; x <= 1; ++x)
	{
		for(int y = -1; y <= 1; ++y)
		{
			// texture coordinate for ArrayShadow is vec4(uv.x, uv.y, layer, compare_depth)
			shadow += texture(uShadowMap, vec4(
				projCoords.xy + vec2(x, y) * texelSize, 
				float(cascadeIndex), 
				projCoords.z
			)); 
		}
	}
	
	return shadow / 9.0;
}

// beckmann distribution function (NDF)
float D_Beckmann(float alpha, float NdotH)
{
	if( NdotH <= 0.0 ) return 0.0;
	float alpha2 = alpha * alpha;
	float NdotH2 = NdotH * NdotH;
	float num = exp( (NdotH2 - 1.0) / (alpha2 * NdotH2) );
	float den = PI * alpha2 * NdotH2 * NdotH2;
	return num / den;
}

// cook-torrance masking
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

void main()
{
	// 1. 材质属性采样 (保持不变)
	vec3 baseColor = texture(uTexColor, v2fTexCoord).rgb;
	float roughness = texture(uTexRoughness, v2fTexCoord).r;
	float metalness = texture(uTexMetalness, v2fTexCoord).r;

	// 2. 几何向量
	vec3 N = normalize(v2fNormal);
	vec3 V = normalize(uScene.cameraPos.xyz - v2fPos); 
	
	// --- 3. 恢复老版本光源逻辑 ---
	vec3 LIGHT_POS = uScene.lightPos.xyz; // 使用 Uniform 中的光源位置
	vec3 L_dir = LIGHT_POS - v2fPos;      // 计算从像素指向光源的向量
	float dist = length(L_dir);           // 如果需要距离衰减可以保留此项
	vec3 L = normalize(L_dir);            // 归一化光照向量
	vec3 H = normalize(L + V);

	// 4. PBR 点积计算
	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 0.0001); 
	float NdotH = max(dot(N, H), 0.0);
	float VdotH = max(dot(V, H), 0.0);

	// 5. 阴影计算 (级联逻辑保持不变)
	const float LIGHT_INTENSITY = 1.2;
	float shadow = calculate_shadow();

	// 6. 恢复完整的 PBR 计算
	vec3 Li = uScene.lightColor.rgb * LIGHT_INTENSITY * shadow; 
	vec3 Lambient = vec3(0.02) * baseColor;

	// Fresnel (Schlick)
	vec3 F0 = mix(vec3(0.04), baseColor, metalness);
	vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
	
	// 漫反射 (Lambertian)
	vec3 Ldiffuse = (baseColor / PI) * (vec3(1.0) - F) * (1.0 - metalness);

	// 高光 (Cook-Torrance)
	float alpha = roughness * roughness;
	if( alpha < 0.001 ) alpha = 0.001; 
	float D = D_Beckmann(alpha, NdotH);
	float G = G_CookTorrance(NdotL, NdotV, NdotH, VdotH);
	
	vec3 num = D * G * F;
	float den = 4.0 * NdotL * NdotV;
	vec3 Lspecular = num / max(den, 0.0001);
	
	// 最终颜色合成
	vec3 Lo = (Ldiffuse + Lspecular) * Li * NdotL;
	vec3 color = Lambient + Lo;

	// --- 调试模式 (保持不变) ---
	if(uScene.renderMode == 8) {
		float viewZ = abs((uScene.camera * vec4(v2fPos, 1.0)).z);
		if(viewZ < uScene.cascadeSplits.x) color *= vec3(1.0, 0.6, 0.6);
		else if(viewZ < uScene.cascadeSplits.y) color *= vec3(0.6, 1.0, 0.6);
		else if(viewZ < uScene.cascadeSplits.z) color *= vec3(0.6, 0.6, 1.0);
	}
	if( uScene.renderMode == 6 ) color = vec3(shadow); 
	
	oColor = vec4(color, 1.0);
}