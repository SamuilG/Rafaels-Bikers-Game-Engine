#pragma once
#include "FrameGraph.hpp"
#include <volk/volk.h>
#include <type_traits>
#include <string_view>

namespace engine {
// Small Vulkan adapter. A null graph makes every hook a no-op.
class FrameGraphCapture {
public:
    using Access = FrameGraph::Access;
    explicit FrameGraphCapture(FrameGraph* graph) : mGraph(graph) {}
    explicit operator bool() const { return mGraph != nullptr; }
    template<class Handle> static uint64_t Key(Handle handle) {
        if constexpr (std::is_pointer_v<Handle>) return uint64_t(reinterpret_cast<uintptr_t>(handle));
        else return uint64_t(handle);
    }
    void Image(VkImage image, std::string_view name, VkExtent2D extent,
        VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT, uint32_t samples = 1,
        bool imported = false, bool external = false, uint32_t layer = 0) {
        if (!mGraph || !image) return;
        const char* text = "Unknown"; uint32_t bytes = 0;
        switch (format) {
        case VK_FORMAT_R16G16B16A16_SFLOAT: text = "RGBA16F"; bytes = 8; break;
        case VK_FORMAT_D32_SFLOAT: text = "D32F"; bytes = 4; break;
        case VK_FORMAT_R8_UNORM: text = "R8 UNORM"; bytes = 1; break;
        case VK_FORMAT_B8G8R8A8_SRGB: text = "BGRA8 sRGB"; bytes = 4; break;
        case VK_FORMAT_B8G8R8A8_UNORM: text = "BGRA8 UNORM"; bytes = 4; break;
        case VK_FORMAT_R8G8B8A8_SRGB: text = "RGBA8 sRGB"; bytes = 4; break;
        case VK_FORMAT_R8G8B8A8_UNORM: text = "RGBA8 UNORM"; bytes = 4; break;
        default: break;
        }
        mGraph->RegisterTexture(Key(image), std::string(name), {extent.width, extent.height, samples, 1, bytes, text, imported, external}, layer);
    }
    void Pass(std::string_view name, std::string_view category = "Graphics") {
        if (mGraph) mPass = mGraph->AddPass(std::string(name), std::string(category));
    }
    void Use(VkImage image, Access access, uint32_t layer = 0) {
        if (!mGraph || !image) return;
        const auto texture = mGraph->FindTexture(Key(image), layer);
        if (texture == FrameGraph::Invalid) throw std::logic_error("Frame graph RT was not registered");
        mGraph->UseTexture(mPass, texture, access);
    }
private:
    FrameGraph* mGraph;
    FrameGraph::Id mPass = FrameGraph::Invalid;
};
} // namespace engine
