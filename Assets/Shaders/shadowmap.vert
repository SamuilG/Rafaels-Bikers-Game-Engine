#version 450

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec3 iPos;
layout( location = 1 ) in vec2 iTexCoord;

layout( location = 0 ) out vec2 v2fTexCoord;

// Push constant is exactly mat4 - same layout as default.vert
// The CPU pre-multiplies: lightVP[cascadeIndex] * batchWorldMatrix
layout( push_constant ) uniform PC {
	mat4 lightModel; // = lightVP[cascade] * model
} uPush;

void main()
{
	v2fTexCoord = iTexCoord;
	gl_Position = uPush.lightModel * vec4(iPos, 1.0);
}
