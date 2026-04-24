#version 450

layout(location = 0) in vec2 v2fTexCoord;
layout(set = 0, binding = 0) uniform sampler2D uTexInput;

// to mos or not to mos aaaa
layout(set = 0, binding = 1) uniform UMosaic {
    int mosaicOn;
    float mosaicSize; // 5x3 works yey
} uMosaic;

layout(location = 0) out vec4 oColor;

void main()
{
    vec2 uv = v2fTexCoord;

    // toggle
    // 5x3 grid
    if (uMosaic.mosaicOn == 1)
    {
        // getpixel coordinates
        ivec2 coord = ivec2(gl_FragCoord.xy);
        
        // be 5x3 grid
        // block size is x=5, y=3
        coord.x -= coord.x % 5;
        coord.y -= coord.y % 3;
        
        // onvert to uv [0-1]
        // texture size to divide the pixel coords
        vec2 texSize = vec2(textureSize(uTexInput, 0));
        uv = vec2(coord) / texSize;
    }

    vec3 color = texture(uTexInput, uv).rgb;

    // reinhard tone mapping
    // color = color / ( 1.0 + color );
    color = color / (color + vec3(1.0));
    
    // gamma is implicit in sRGB swapchain
    // color = pow( color, vex3(1.0/2.2) );

    oColor = vec4(color, 1.0);
}
