#pragma once
#include "../Core/System.h"
#include "WindowContext.hpp"
#include "../Rhi/vulkan_window.hpp"
#include "../Rhi/vulkan_context.hpp"

namespace engine {
	class WindowSystem final : public System {
	public:
		explicit WindowSystem(WindowContext& context) : m_Context(context) {}

		void Init() override{
			m_Window = vkRHI::make_vulkan_window();
			m_Context.Width = m_Window..swapchainExtent.width;
			m_Context.Height = m_Window.swapchainExtent.height;
		};

		void Update() override {
			glfwPollEvents();
			if (glfwWindowShouldClose(mWindow.window))
				m_Context.ShouldQuit = true;
		};
		void Shutdown() override {
			//RAII will handle cleanup
		};

		//for renderer to access the window and swapchain info
		lut::VulkanWindow& GetVulkanWindow() { return mWindow; }
		lut::VulkanWindow const& GetVulkanWindow() const { return mWindow; }

		void NotifyResized() { m_Context.Resized = true; }


	private:
		vkRHI::VulkanWindow m_Window;
		WindowContext& m_Context;

	};
}

