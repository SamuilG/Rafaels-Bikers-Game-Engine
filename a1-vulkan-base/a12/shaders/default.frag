#version 450

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec2 v2fTexCoord;
layout( location = 1 ) in vec3 v2fNormal;
layout( location = 2 ) in vec3 v2fPos;
layout( location = 3 ) in vec4 v2fLightProjPos;

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
	mat4 lightVP;
} uScene;

layout( set = 0, binding = 1 ) uniform sampler2DShadow uShadowMap;

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

// p2_1.5 PCF
float calculate_shadow()
{
	vec3 projCoords = v2fLightProjPos.xyz / v2fLightProjPos.w;
	// projCoords are in [-1, 1], transform to [0, 1]
	projCoords.xy = projCoords.xy * 0.5 + 0.5;

	// check bounds
	if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
		projCoords.y < 0.0 || projCoords.y > 1.0 || 
		projCoords.z < 0.0 || projCoords.z > 1.0) 
	{
		return 1.0;
	}

	// PCF
	float shadow = 0.0;
	vec2 texelSize = 1.0 / textureSize(uShadowMap, 0);
	
	// 3x3 PCF
	for(int x = -1; x <= 1; ++x)
	{
		for(int y = -1; y <= 1; ++y)
		{

			// sampler2DShadow automatic comparison
			shadow += texture(uShadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, projCoords.z)); 
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
	
	// light vector
	vec3 L_dir = LIGHT_POS - v2fPos;
	float dist = length(L_dir);
	vec3 L = normalize(L_dir);
	vec3 H = normalize(L + V);

	// dot products
	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 0.0001); // avoid div by zero
	float NdotH = max(dot(N, H), 0.0);
	float VdotH = max(dot(V, H), 0.0);
	float LdotH = max(dot(L, H), 0.0); // same as VdotH
	const float LIGHT_INTENSITY = 1.2; // no falloff

	float shadow = calculate_shadow();

	// light radiance (no falloff)
	vec3 Li = LIGHT_COLOR * LIGHT_INTENSITY * shadow; 
	vec3 Lambient = vec3(0.02) * baseColor; // weak ambient

	// PBR
	// Diffuse (Lambertian)
	// Dielectrics have diffuse, metals do not (multiplied by 1-M)
	
	// Fresnel (Schlick)
	// F0 = mix(0.04, baseColor, metalness)
	vec3 F0 = mix(vec3(0.04), baseColor, metalness);
	vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
	
	vec3 Ldiffuse = (baseColor / PI) * (vec3(1.0) - F) * (1.0 - metalness);

	// Specular (Cook-Torrance)
	// Lspecular = (D * G * F) / (4 * NdotL * NdotV)
	
	// Alpha from roughness
	float alpha = roughness * roughness;
	if( alpha < 0.001 ) alpha = 0.001; 
	
	float D = D_Beckmann(alpha, NdotH);
	float G = G_CookTorrance(NdotL, NdotV, NdotH, VdotH);
	
	vec3 num = D * G * F;
	float den = 4.0 * NdotL * NdotV;
	
	vec3 Lspecular = num / max(den, 0.0001);
	
	vec3 Lo = (Ldiffuse + Lspecular) * Li * NdotL;
	
	vec3 color = Lambient + Lo;

	// debug modes
	if( uScene.renderMode == 6 )
	{
		// shadow map debug
		// 1.0 = lit (white), 0.0 = shadow (black)
		color = vec3(shadow); 
	}
	
	oColor = vec4(color, 1.0);
}
