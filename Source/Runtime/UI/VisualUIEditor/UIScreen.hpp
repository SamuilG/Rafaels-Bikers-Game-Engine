#pragma once

#include <memory>
#include <string>
#include <vector>

#include "UIAnimation.hpp"
#include "UIBinding.hpp"
#include "UIElement.hpp"

namespace engine {

    // UI 屏幕容器，管理一棵 UIElement 树、动画片段、数据绑定以及屏幕级设置
    // （可见性、渲染顺序、进场/退场动画等）
    class UIScreen {
    public:
        // 构造函数：指定屏幕名称和画布参考分辨率
        explicit UIScreen(std::string screenName = "UIScreen", const glm::vec2& canvasSize = glm::vec2(1920.0f, 1080.0f));

        // 创建根画布元素，作为整棵 UI 元素树的根节点
        UIElement& CreateRootCanvas(std::string canvasName = "Canvas", const glm::vec2& canvasSize = glm::vec2(1920.0f, 1080.0f));
        // 判断是否已存在根画布
        bool HasRootCanvas() const { return mRootCanvas != nullptr; }

        // 获取根画布指针（可变/只读）
        UIElement* GetRootCanvas() { return mRootCanvas.get(); }
        const UIElement* GetRootCanvas() const { return mRootCanvas.get(); }

        // 按元素 ID 在整棵 UI 树中递归查找元素
        UIElement* FindById(UIElementId searchId);
        const UIElement* FindById(UIElementId searchId) const;
        UIElement* FindByName(std::string_view searchName);
        const UIElement* FindByName(std::string_view searchName) const;

        // ---- 数据绑定管理 ----
        std::vector<UIPropertyBinding>& GetBindings() { return mBindings; }
        const std::vector<UIPropertyBinding>& GetBindings() const { return mBindings; }
        // 添加一条属性绑定，返回新绑定的引用
        UIPropertyBinding& AddBinding(UIPropertyBinding binding = {});
        // 按索引移除绑定，成功返回 true
        bool RemoveBinding(std::size_t index);

        // ---- 动画片段管理 ----
        std::vector<UIAnimationClip>& GetAnimationClips() { return mAnimationClips; }
        const std::vector<UIAnimationClip>& GetAnimationClips() const { return mAnimationClips; }
        // 添加一段动画片段，返回新片段的引用
        UIAnimationClip& AddAnimationClip(UIAnimationClip clip = {});
        // 按索引移除动画片段，成功返回 true
        bool RemoveAnimationClip(std::size_t index);

        // ---- 基本属性 ----
        // 屏幕名称
        const std::string& GetName() const { return mName; }
        void SetName(std::string screenName) { mName = std::move(screenName); }

        // 参考分辨率，用于 UI 适配缩放
        const glm::vec2& GetReferenceResolution() const { return mReferenceResolution; }
        void SetReferenceResolution(const glm::vec2& referenceResolution) { mReferenceResolution = referenceResolution; }

        // 主题资源路径
        const std::string& GetThemePath() const { return mThemePath; }
        void SetThemePath(std::string themePath) { mThemePath = std::move(themePath); }

        // ---- 显示与交互设置 ----
        // 屏幕是否可见
        bool IsVisible() const { return mVisible; }
        void SetVisible(bool visible) { mVisible = visible; }

        // 是否拦截底层输入事件
        bool BlocksInput() const { return mBlocksInput; }
        void SetBlocksInput(bool blocksInput) { mBlocksInput = blocksInput; }

        // 渲染排序值，值越大越靠前绘制
        int GetRenderOrder() const { return mRenderOrder; }
        void SetRenderOrder(int renderOrder) { mRenderOrder = renderOrder; }

        // 屏幕打开时是否暂停游戏逻辑
        bool PauseGameWhenOpen() const { return mPauseGameWhenOpen; }
        void SetPauseGameWhenOpen(bool pauseGameWhenOpen) { mPauseGameWhenOpen = pauseGameWhenOpen; }

        // 是否允许输入穿透到更低层级的屏幕
        bool AllowInputPassthrough() const { return mAllowInputPassthrough; }
        void SetAllowInputPassthrough(bool allowInputPassthrough) { mAllowInputPassthrough = allowInputPassthrough; }

        // ---- 进场/退场动画设置 ----
        // 进场动画名称
        const std::string& GetEnterAnimation() const { return mEnterAnimation; }
        void SetEnterAnimation(std::string animationName) { mEnterAnimation = std::move(animationName); }

        // 退场动画名称
        const std::string& GetExitAnimation() const { return mExitAnimation; }
        void SetExitAnimation(std::string animationName) { mExitAnimation = std::move(animationName); }

        // 退场动画播完后是否自动隐藏屏幕
        bool HideAfterExit() const { return mHideAfterExit; }
        void SetHideAfterExit(bool hideAfterExit) { mHideAfterExit = hideAfterExit; }

        // 过渡动画播放期间是否阻断输入
        bool BlockInputDuringTransition() const { return mBlockInputDuringTransition; }
        void SetBlockInputDuringTransition(bool blockInputDuringTransition) { mBlockInputDuringTransition = blockInputDuringTransition; }

        // 综合判断：是否阻断更低层级屏幕的输入（需同时拦截且不穿透）
        bool BlocksLowerLayerInput() const { return mBlocksInput && !mAllowInputPassthrough; }

    private:
        std::string mName;                                              // 屏幕名称
        glm::vec2 mReferenceResolution = glm::vec2(1920.0f, 1080.0f);  // 参考分辨率
        std::string mThemePath;                                         // 主题路径
        bool mVisible = true;                                           // 是否可见
        bool mBlocksInput = true;                                       // 是否拦截输入
        int mRenderOrder = 0;                                           // 渲染顺序
        bool mPauseGameWhenOpen = false;                                // 打开时暂停游戏
        bool mAllowInputPassthrough = false;                            // 允许输入穿透
        std::string mEnterAnimation;                                    // 进场动画名
        std::string mExitAnimation;                                     // 退场动画名
        bool mHideAfterExit = true;                                     // 退场后自动隐藏
        bool mBlockInputDuringTransition = true;                        // 过渡期间阻断输入
        std::vector<UIPropertyBinding> mBindings;                       // 属性绑定列表
        std::vector<UIAnimationClip> mAnimationClips;                   // 动画片段列表
        std::unique_ptr<UIElement> mRootCanvas;                         // 根画布元素
    };

} // namespace engine
