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

vec3 mip_color( float lod )
{
	if( lod < 0.0 ) return vec3(1.0, 1.0, 1.0); 
	int level = int(lod);
	float fract = lod - float(level);
	
	vec3 colors[6];
	colors[0] = vec3(1.0, 0.0, 0.0); // Red - Level 0
	colors[1] = vec3(0.0, 1.0, 0.0); // Green
	colors[2] = vec3(0.0, 0.0, 1.0); // Blue
	colors[3] = vec3(1.0, 1.0, 0.0); // Yellow
	colors[4] = vec3(0.0, 1.0, 1.0); // Cyan
	colors[5] = vec3(1.0, 0.0, 1.0); // Magenta

	vec3 c0 = colors[level % 6];
	vec3 c1 = colors[(level + 1) % 6];
	return mix(c0, c1, fract);
}

void main()
{
	float lod = textureQueryLod( uTexColor, v2fTexCoord ).x;
	oColor = vec4( mip_color(lod), 1.0 );
}
