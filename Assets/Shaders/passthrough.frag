#version 450

layout(location = 0) in vec2 v2fTexCoord;
layout(set = 0, binding = 0) uniform sampler2D uTexInput;

layout(location = 0) out vec4 oColor;

void main()
{
    // Display diagnostic colors from the floating-point scene target directly.
    // Exposure, tone mapping and mosaic must not change the diagnostic values.
    
    vec4 color = texture(uTexInput, v2fTexCoord);
    oColor = vec4(color.rgb, 1.0);
}
