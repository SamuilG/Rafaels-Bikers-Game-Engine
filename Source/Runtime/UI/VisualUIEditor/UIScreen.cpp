#include "UIScreen.hpp"

namespace engine {

    UIScreen::UIScreen(std::string screenName, const glm::vec2& canvasSize)
        : mName(std::move(screenName)) {
        // UIScreen 创建时默认就带一个 Canvas，避免编辑器和运行时到处判空。
        CreateRootCanvas("Canvas", canvasSize);
    }

    UIElement& UIScreen::CreateRootCanvas(std::string canvasName, const glm::vec2& canvasSize) {
        // Canvas 代表整张屏幕的根矩形，因此自身不参与交互命中。
        mReferenceResolution = canvasSize;
        auto canvas = std::make_unique<UIElement>(UIElementType::Canvas, std::move(canvasName));
        canvas->interactable = false;
        canvas->transform.anchorMin = glm::vec2(0.0f);
        canvas->transform.anchorMax = glm::vec2(0.0f);
        canvas->transform.pivot = glm::vec2(0.0f);
        canvas->transform.position = glm::vec2(0.0f);
        canvas->transform.size = canvasSize;
        mRootCanvas = std::move(canvas);
        return *mRootCanvas;
    }

    // 在根画布下递归查找指定 ID 的元素，未找到返回 nullptr
    UIElement* UIScreen::FindById(UIElementId searchId) {
        return mRootCanvas ? mRootCanvas->FindById(searchId) : nullptr;
    }

    const UIElement* UIScreen::FindById(UIElementId searchId) const {
        return mRootCanvas ? mRootCanvas->FindById(searchId) : nullptr;
    }

    UIElement* UIScreen::FindByName(std::string_view searchName) {
        return mRootCanvas ? mRootCanvas->FindByName(searchName) : nullptr;
    }

    const UIElement* UIScreen::FindByName(std::string_view searchName) const {
        return mRootCanvas ? mRootCanvas->FindByName(searchName) : nullptr;
    }

    // 添加属性绑定到列表末尾，返回新绑定的引用
    UIPropertyBinding& UIScreen::AddBinding(UIPropertyBinding binding) {
        mBindings.push_back(std::move(binding));
        return mBindings.back();
    }

    // 按索引移除属性绑定，越界时返回 false
    bool UIScreen::RemoveBinding(std::size_t index) {
        if (index >= mBindings.size()) {
            return false;
        }

        mBindings.erase(mBindings.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    // 添加动画片段到列表末尾，返回新片段的引用
    UIAnimationClip& UIScreen::AddAnimationClip(UIAnimationClip clip) {
        mAnimationClips.push_back(std::move(clip));
        return mAnimationClips.back();
    }

    // 按索引移除动画片段，越界时返回 false
    bool UIScreen::RemoveAnimationClip(std::size_t index) {
        if (index >= mAnimationClips.size()) {
            return false;
        }

        mAnimationClips.erase(mAnimationClips.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

} // namespace engine
