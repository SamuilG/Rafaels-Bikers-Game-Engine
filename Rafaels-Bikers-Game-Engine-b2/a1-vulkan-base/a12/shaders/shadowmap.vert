#version 450

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec3 iPos;
layout( location = 1 ) in vec2 iTexCoord;
// no normal input

layout( location = 0 ) out vec2 v2fTexCoord;

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
	mat4 lightVP; // p2_1.5 add light matrix
} uScene;


layout( push_constant ) uniform PushConstants {
	mat4 model; 
} uPush;

void main()
{
	v2fTexCoord = iTexCoord;
	
	// world pos of the vertex
	// multiplied by model matrix (local to world space)
	vec4 worldPos = uPush.model * vec4(iPos, 1.0);

	// world pos into light's clip space
	// (render from the light's perspective)
	gl_Position = uScene.lightVP * worldPos;
}
