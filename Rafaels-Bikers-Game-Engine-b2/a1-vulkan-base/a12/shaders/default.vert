#version 450

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec3 iPosition;
layout( location = 1 ) in vec2 iTexCoord;
layout( location = 2 ) in vec3 iNormal;

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

layout( location = 0 ) out vec2 v2fTexCoord;
layout( location = 1 ) out vec3 v2fNormal;
layout( location = 2 ) out vec3 v2fPos;
layout( location = 3 ) out vec4 v2fLightProjPos; // p_1.5

//import modelmatrix in glb
layout( push_constant ) uniform PushConstants {
	mat4 model; 
} uPush;

void main()
{
	v2fTexCoord = iTexCoord;
	
	v2fNormal = normalize(mat3(uPush.model) * iNormal);
	// Pass original normal
	// object space = world space for static

	vec4 worldPos = uPush.model * vec4(iPosition, 1.f);
	v2fPos = worldPos.xyz;

	gl_Position = uScene.projCam * worldPos;

	// p_1.5 position in light space
	v2fLightProjPos = uScene.lightVP * worldPos;
}
