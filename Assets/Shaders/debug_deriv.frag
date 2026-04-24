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
	// Partial Derivatives of Depth
	float dx = dFdx( gl_FragCoord.z );
	float dy = dFdy( gl_FragCoord.z );
	
	// Scale up significantly because depth derivatives are tiny
	float s = 1000.0; 
	oColor = vec4( abs(dx)*s, abs(dy)*s, 0.0, 1.0 );
}
