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
	mat4 lightVP; // p2_1.5
} uScene;

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

void main()
{
	vec4 color = texture( uTexColor, v2fTexCoord );
	if( color.a < 0.5 )
		discard;

	// material properties
	vec3 baseColor = color.rgb;
	float roughness = texture(uTexRoughness, v2fTexCoord).r;
	float metalness = texture(uTexMetalness, v2fTexCoord).r;

	// geometric vectors
	vec3 N = normalize(v2fNormal);
	vec3 V = normalize(uScene.cameraPos.xyz - v2fPos); // correct camera pos from uniform
	
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

	// light radiance (no falloff)
	vec3 Li = LIGHT_COLOR * LIGHT_INTENSITY; 
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
	
	// Alpha from roughness? Usually alpha = roughness^2 for perceptually linear roughness
	float alpha = roughness * roughness;
	if( alpha < 0.001 ) alpha = 0.001; // clamp to avoid degenerate
	
	float D = D_Beckmann(alpha, NdotH);
	float G = G_CookTorrance(NdotL, NdotV, NdotH, VdotH);
	
	vec3 num = D * G * F;
	float den = 4.0 * NdotL * NdotV;
	
	vec3 Lspecular = num / max(den, 0.0001);
	
	vec3 Lo = (Ldiffuse + Lspecular) * Li * NdotL;
	
	vec3 finalColor = Lambient + Lo;
	
	oColor = vec4(finalColor, 1.0);
}
