#version 450

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec2 v2fTexCoord;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;

layout( scalar, set = 0, binding = 0 ) uniform UScene
{
	mat4 camera;
	mat4 projection;
	mat4 projCam;
	uint renderMode;
} uScene;

layout( location = 0 ) out vec4 oColor;

void main()
{
	// Depth visualization
	float d = gl_FragCoord.z;
	// Power it to make it visible
	oColor = vec4( vec3(pow(d, 250.0)), 1.0 ); 
}
