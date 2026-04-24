#pragma execution_character_set("utf-8")
#include "SwitchLanguage.hpp"

namespace engine {

    Language Translator::CurrentLanguage = Language::English;

    std::unordered_map<std::string, std::string> Translator::ChineseDict = {
        {"Engine Control Panel", "游戏引擎调试面板"},
        {"[ Performance ]", "[ 性能与渲染 ]"},
        {"FPS: %.3f ms", "帧耗时: %.3f ms"},
        {"View Mode", "渲染模式"},
        {"Particle System", "启用粒子系统"},
        {"Enable Mosaic Post-Process", "马赛克后处理"},
        {"[ generator ]", "[ 物理实体生成器 ]"},
        {"Height", "生成高度 (Y轴)"},
        {"select", "选择模型"},
        {"Spawn!!!!", "从天而降!"},
        {"Switch Language", "切换语言"},
        //粒子系统编辑器
		//particle editor
        {"[ Particle Editor ]", "[ 粒子系统实时编辑器 ]"},
        {"Select Particle", "选择粒子组"},
        {"Emitter Settings", "发射器设置"},
        {"Physics & Movement", "物理与运动"},
        {"Appearance & Color", "外观与颜色"},
        {"Life Cycle", "生命周期"},
        {"Emitter Shape", "发射器形态"},
        {"Emitter Pos", "发射器位置 (XYZ)"},
        {"Cone Spread", "锥形散射角度"},
        {"Sphere Radius", "球形发射半径"},
        {"Box Area", "盒形发射范围 (XYZ)"},
        {"Show Debug Wireframe", "显示调试线框"},
        {"Gravity", "重力影响 (XYZ)"},
        {"Speed Min", "最小初速度"},
        {"Speed Max", "最大初速度"},
        {"Rotation Min", "最小初始旋转 (度)"},
        {"Rotation Max", "最大初始旋转 (度)"},
        {"Size Min", "最小尺寸"},
        {"Size Max", "最大尺寸"},
        {"Start Size Scale", "出生时尺寸缩放"},
        {"End Size Scale", "死亡时尺寸缩放"},
        {"Start Color", "出生颜色 (RGBA)"},
        {"End Color", "消散颜色 (RGBA)"},
        {"Atlas Cols", "贴图列数 (Cols)"},
        {"Atlas Rows", "贴图行数 (Rows)"},
        {"Animate Atlas", "播放序列帧动画"},
        {"Life Min", "最短寿命 (秒)"},
        {"Life Max", "最长寿命 (秒)"},
        {"Add Group", "新增粒子组"},
        {"Delete Group", "删除当前组"},
        {"Visible", "显示 / 隐藏"},
        {"Name", "重命名 (Name)"},
        {"Use Texture", "使用贴图"},
        {"Select Texture", "选择贴图文件"},
        {"Max Particles", "最大粒子数量"},
        {"Apply Changes", "应用更改 (重置)"},
		//主菜单
        {"MAIN MENU", "主菜单"},
        {"Start Game", "开始游戏"},
        {"Exit Game", "退出游戏"},
        {"Setting", "设置"},
    };

} // namespace engine