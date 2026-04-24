#version 450

layout(location = 0) out vec4 oColor;

void main()
{
    // Output 1/20 per fragment
    // additive blending, 20 overlapping fragments will reach 1.0 (white)
    oColor = vec4(0.05, 0.05, 0.05, 1.0); 


    // Alpha is set to 1.0;
    // not written to target due to pipeline state
}
