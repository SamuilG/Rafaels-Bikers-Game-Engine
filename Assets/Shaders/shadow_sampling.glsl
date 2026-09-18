#ifndef STEER_SHADOW_SAMPLING_GLSL
#define STEER_SHADOW_SAMPLING_GLSL

// Requires uScene.camera/lightVP/cascadeSplits, v2fPos and uShadowMap.
const vec2 poissonDisk[16] = vec2[](
   vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ),
   vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ),
   vec2( -0.91588581, 0.45771432 ), vec2( -0.81544232, -0.87912464 ),
   vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ),
   vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ),
   vec2( -0.26496911, -0.41893023 ), vec2( 0.79197514, 0.19090188 ),
   vec2( -0.24188840, 0.99706507 ), vec2( -0.81409955, 0.91437590 ),
   vec2( 0.19984126, 0.78641367 ), vec2( 0.14383161, -0.14100790 )
);

// =================================================================
// 【新增】：将原先的阴影采样逻辑抽取成独立函数，方便复用
// =================================================================
float SampleShadowCascade(int cascadeIdx)
{
	vec4 lightSpacePos = uScene.lightVP[cascadeIdx] * vec4(v2fPos, 1.0);
	vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
	projCoords.xy = projCoords.xy * 0.5 + 0.5;

	// 如果超出了该层级的投影范围，直接返回无阴影(1.0)
	if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
		return 1.0;

	float bias = 0.00005;
	float currentDepth = projCoords.z - bias;

	vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
	float shadow = 0.0;
	float filterSize = 2.5;

	for(int i = 0; i < 16; ++i)
	{
		vec2 offset = poissonDisk[i] * texelSize * filterSize;
		// 注意 texture() 搭配 sampler2DArrayShadow 返回的是 float(可见度 0.0~1.0)
		shadow += texture(uShadowMap, vec4(projCoords.xy + offset, float(cascadeIdx), currentDepth));
	}
	return shadow / 16.0;
}

// =================================================================
// 【修改】：支持 Cascade Blending (级联缝隙平滑过渡) 的阴影计算
// =================================================================
float calculate_shadow()
{
	vec4 viewPos = uScene.camera * vec4(v2fPos, 1.0);
	float fragDepth = abs(viewPos.z);

	if (fragDepth >= uScene.cascadeSplits.z) {
		return 1.0;
	}

	int cascadeIdx = 2;
	float nextSplit = uScene.cascadeSplits.z;

	// 1. 判定当前像素所属的主级联及该级联的远切面深度
	if (fragDepth < uScene.cascadeSplits.x) {
		cascadeIdx = 0;
		nextSplit = uScene.cascadeSplits.x;
	} else if (fragDepth < uScene.cascadeSplits.y) {
		cascadeIdx = 1;
		nextSplit = uScene.cascadeSplits.y;
	} else {
		cascadeIdx = 2;
		nextSplit = uScene.cascadeSplits.z;
	}

	// 2. 采样主 Cascade
	float shadow = SampleShadowCascade(cascadeIdx);

	// 3. 缝隙平滑混合逻辑
	if (cascadeIdx < 2) {
		// 设置过渡带宽度：使用距离当前 Cascade 边界最后 12% 的区域进行平滑过渡
		float blendBand = nextSplit * 0.12;
		float blendStart = nextSplit - blendBand;

		// 如果像素刚好由于相机移动落在了级联的交界区域内
		if (fragDepth > blendStart) {
			// 计算插值因子 (0.0 表示刚进入过渡带，1.0 表示彻底贴紧边界线)
			float blendFactor = clamp((fragDepth - blendStart) / blendBand, 0.0, 1.0);

			// 只在过渡带才去花性能采样下一级的纹理 (动态复用保护了整体性能)
			float nextShadow = SampleShadowCascade(cascadeIdx + 1);

			// 将当前精度与下一级精度进行丝滑混合
			shadow = mix(shadow, nextShadow, blendFactor);
		}
	}

	return shadow;
}

#endif
