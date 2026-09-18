#version 450

layout( location = 0 ) in vec3 v2fColor;

layout( location = 0 ) out vec4 oColor;

void main()
{
    oColor = vec4(v2fColor, 1.0);
}
