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
	uint renderMode;
} uScene;

layout( push_constant ) uniform PushConstants {
	mat4 model; // modMat for object positioning
} uPush;

layout( location = 0 ) out vec2 v2fTexCoord;
layout( location = 1 ) out vec3 v2fNormal; // Just to consume input

void main()
{
	v2fTexCoord = iTexCoord;
	// normal to world space
	v2fNormal = normalize(mat3(uPush.model) * iNormal);
	
	// position to world space
	vec4 worldPos = uPush.model * vec4( iPosition, 1.f );
	gl_Position = uScene.projCam * worldPos;
}
