#version 450

layout(location=0) in vec4 vColor;
layout(location=1) in flat int vUseTexture;
layout(location=2) in vec4 vUVRect;
layout(location=3) in float vRotation;
layout(location=4) in flat int vDebugMode;

layout(location=0) out vec4 outColor;
layout(set=1, binding=0) uniform sampler2D texSampler;

void main() {
    //将原点移到中心
    vec2 centeredCoord = gl_PointCoord - vec2(0.5, 0.5);
  
    centeredCoord *= 1.4142; 

    //旋转计算
    float s = sin(vRotation);
    float c = cos(vRotation);
    mat2 rotMat = mat2(c, -s, 
                       s,  c);
    vec2 rotatedCoord = rotMat * centeredCoord + vec2(0.5, 0.5);

    //判断是否超出范围
    if (rotatedCoord.x < 0.0 || rotatedCoord.x > 1.0 || 
        rotatedCoord.y < 0.0 || rotatedCoord.y > 1.0) 
    {
        discard;
    }

    if (vUseTexture == 1) {
        
        //debug线框
        float border = 0.03; 
        
        // 检测当前像素是否位于边缘区域
        bool isEdge = (rotatedCoord.x < border || rotatedCoord.x > 1.0 - border || 
                       rotatedCoord.y < border || rotatedCoord.y > 1.0 - border);
        
        if (isEdge && vDebugMode == 1) {
            // 画框
            outColor = vec4(1.0, 0.0, 0.0, 0.5);
        } else {
            // 内部正常采样真正的贴图
            vec2 finalUV = rotatedCoord * vUVRect.xy + vUVRect.zw; 
            vec4 texColor = texture(texSampler, finalUV);
            vec4 finalParticleColor = vColor * texColor;//調整叠加度
            if (texColor.a < 0.1) {
        discard; // 丟棄像素
    }
        outColor = vec4(finalParticleColor.rgb * finalParticleColor.a, finalParticleColor.a);
        }
        

    } else {
        // 圆球
        vec2 uv = rotatedCoord * 2.0 - 1.0; 
        float r2 = dot(uv, uv);
        if (r2 > 1.0) discard;
        float soft = smoothstep(1.0, 0.6, r2);
        outColor = vColor * soft;
    }
}