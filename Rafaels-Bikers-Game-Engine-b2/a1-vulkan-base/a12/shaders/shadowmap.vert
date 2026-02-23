#version 450

#extension GL_EXT_scalar_block_layout : require

layout( location = 0 ) in vec3 iPos;
layout( location = 1 ) in vec2 iTexCoord;

layout( location = 0 ) out vec2 v2fTexCoord;

// 更新后的 UScene 结构体，必须与 C++ 端的 glsl::SceneUniform 严格对齐
layout( scalar, set = 0, binding = 0 ) uniform UScene
{
	mat4 camera;
	mat4 projection;
	mat4 projCam;
	vec4 cameraPos;
	vec4 lightPos;
	vec4 lightColor;
	uint renderMode;
	uint _pad0;
	uint _pad1;
	uint _pad2;
	mat4 lightVP[4];      // 修改为数组，匹配 kCascadeCount = 4
	vec4 cascadeSplits;   // 虽然顶点着色器可能用不到，但为了对齐 UBO 布局必须加上
} uScene;

// 更新 Push Constants 结构体以接收级联索引
layout( push_constant ) uniform PushConstants {
	uint cascadeIndex;    // 当前正在渲染哪一层级联 (0, 1, 2, 3)
	mat4 model;           // 实例的模型变换矩阵
} uPush;

void main()
{
	v2fTexCoord = iTexCoord;
	
	// 1. 计算世界空间坐标
	vec4 worldPos = uPush.model * vec4(iPos, 1.0);

	// 2. 根据当前的级联索引选择对应的灯光 VP 矩阵
	// 使用 uPush.cascadeIndex 从数组中提取正确的变换矩阵
	gl_Position = uScene.lightVP[uPush.cascadeIndex] * worldPos;
}