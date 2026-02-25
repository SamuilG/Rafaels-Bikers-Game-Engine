#version 460

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) out vec2 v2fTexCoord;

void main()
{
	// generate full-screen triangle from vertex index
	// top-left (-1, -1) to (3, -1) to (-1, 3) 
	// this covers the screen range (-1, 1) in X and Y
	
	vec2 pos[3] = vec2[](
		vec2(-1.0, -1.0),
		vec2( 3.0, -1.0),
		vec2(-1.0,  3.0)
	);

	gl_Position = vec4( pos[gl_VertexIndex], 0.0, 1.0 );
	
	// UV coordinates
	// (0,0) to (2,0) to (0,2)
	v2fTexCoord = 0.5 * gl_Position.xy + 0.5;
}
