#version 450

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec3 iPosition;
layout( location = 1 ) in vec2 iTexCoord;
layout( location = 2 ) in vec3 iNormal;

struct GpuLight {
    vec4 position;
    vec4 color;
    vec4 direction;
    vec4 params;
};

layout( scalar, set = 0, binding = 0 ) uniform UScene
{
	mat4 camera;
	mat4 projection;
	mat4 projCam;
	vec4 cameraPos;
	GpuLight lights[16];
	vec4 lightPos;
	vec4 lightColor;
	uint lightCount;
	uint renderMode;
	uint iblEnabled;
	uint _pad1;
	mat4 lightVP[4];      // CSM cascade light-view-proj matrices
	vec4 cascadeSplits;   // view-space split depths
	vec4 portalClipPlane;
} uScene;

layout( location = 0 ) out vec2 v2fTexCoord;
layout( location = 1 ) out vec3 v2fNormal;
layout( location = 2 ) out vec3 v2fPos;
layout( location = 3 ) out vec4 v2fLightProjPos; // p_1.5

//import modelmatrix in glb



layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float alphaCutoff;
    float _pad;
    vec4 clipPlane;
} pc;


void main()
{
	v2fTexCoord = iTexCoord;
	
	v2fNormal = normalize(mat3(pc.transform) * iNormal);
	// Pass original normal
	// object space = world space for static

	vec4 worldPos = pc.transform * vec4(iPosition, 1.f);
	v2fPos = worldPos.xyz;

	gl_Position = uScene.projCam * worldPos;

	// p_1.5 position in light space (cascade 0 used as reference; frag selects per-cascade)
	v2fLightProjPos = uScene.lightVP[0] * worldPos;
}
