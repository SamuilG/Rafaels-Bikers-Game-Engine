#version 450

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec2 v2fTexCoord;


// Alpha masking
layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( set = 1, binding = 1 ) uniform sampler2D uTexRoughness;
layout( set = 1, binding = 2 ) uniform sampler2D uTexMetalness;


void main()
{

	float alpha = texture(uTexColor, v2fTexCoord).a;
	if (alpha < 0.5)
	{
		discard;
	}
	
	// no color output
	// depth automatically

}
