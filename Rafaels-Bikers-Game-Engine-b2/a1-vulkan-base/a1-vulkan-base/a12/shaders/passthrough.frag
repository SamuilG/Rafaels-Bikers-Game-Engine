#version 450

layout(location = 0) in vec2 v2fTexCoord;
layout(set = 0, binding = 0) uniform sampler2D uTexInput;

layout(location = 0) out vec4 oColor;

void main()
{
    // bro just passthrough
    // input texture (visImage) is already UNORM (clamped)
    // show heat without crushing the values
    
    vec4 color = texture(uTexInput, v2fTexCoord);
    oColor = vec4(color.rgb, 1.0);
}
