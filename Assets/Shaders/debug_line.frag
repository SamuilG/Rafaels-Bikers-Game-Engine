#version 450

layout( location = 0 ) in vec3 v2fColor;

layout( location = 0 ) out vec4 oColor;
layout( location = 1 ) out vec4 oBrightColor; 

void main()
{
    oColor = vec4(v2fColor, 1.0);
    
    
    oBrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}