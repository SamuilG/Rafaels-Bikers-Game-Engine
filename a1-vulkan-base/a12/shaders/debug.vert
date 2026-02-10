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

layout( location = 0 ) out vec2 v2fTexCoord;
layout( location = 1 ) out vec3 v2fNormal; // Just to consume input

void main()
{
	v2fTexCoord = iTexCoord;
	v2fNormal = iNormal;
	gl_Position = uScene.projCam * vec4( iPosition, 1.f );
}
