#pragma once

#include <imgui.h>
#include <ImGuizmo.h>
#include <cmath>

// The editor's paper-and-blueprint palette. Runtime/game UI keeps its own style.
namespace engine::editor_theme
{
    inline constexpr unsigned kPaper = 0xF0EEE1;
    inline constexpr unsigned kViewport = 0xE5E3D8;
    inline constexpr unsigned kBorder = 0x2C3131;
    inline constexpr unsigned kAccent = 0x4C7B9B;
    inline constexpr unsigned kHover = 0xE2DEC9;
    inline constexpr unsigned kText = 0x1F2421;
    inline constexpr unsigned kMuted = 0x6B6D6B;
    inline constexpr unsigned kAxisX = 0xD9534F;
    inline constexpr unsigned kAxisY = 0x5CB85C;
    inline constexpr unsigned kAxisZ = 0x428BCA;

    // Status text needs darker variants than the axis colors on a paper surface.
    inline constexpr unsigned kError = 0xA93732;
    inline constexpr unsigned kWarning = 0x825A1F;
    inline constexpr unsigned kSuccess = 0x39703D;

    namespace detail
    {
        // Headless previews use sRGB values directly. Vulkan enables conversion
        // only for the editor when its render target performs sRGB encoding.
        inline bool linearOutput = false;

        inline ImVec4 SrgbColor(unsigned rgb, float alpha)
        {
            constexpr float channelScale = 1.0f / 255.0f;
            return ImVec4(((rgb >> 16) & 0xFF) * channelScale,
                ((rgb >> 8) & 0xFF) * channelScale, (rgb & 0xFF) * channelScale, alpha);
        }
    }

    inline void SetLinearOutput(bool enabled)
    {
        detail::linearOutput = enabled;
    }

    inline bool UsesLinearOutput()
    {
        return detail::linearOutput;
    }

    inline float ToLinear(float srgb)
    {
        return srgb <= 0.04045f ? srgb / 12.92f
            : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }

    inline ImVec4 ToLinear(const ImVec4& srgb)
    {
        return ImVec4(ToLinear(srgb.x), ToLinear(srgb.y), ToLinear(srgb.z), srgb.w);
    }

    inline ImVec4 Color(unsigned rgb, float alpha = 1.0f)
    {
        const ImVec4 srgb = detail::SrgbColor(rgb, alpha);
        return detail::linearOutput ? ToLinear(srgb) : srgb;
    }

    inline ImU32 ColorU32(unsigned rgb, float alpha = 1.0f)
    {
        return ImGui::ColorConvertFloat4ToU32(Color(rgb, alpha));
    }

    inline ImVec4 Mix(unsigned first, unsigned second, float secondWeight, float alpha = 1.0f)
    {
        const ImVec4 a = detail::SrgbColor(first, alpha);
        const ImVec4 b = detail::SrgbColor(second, alpha);
        const ImVec4 srgb(a.x + (b.x - a.x) * secondWeight,
            a.y + (b.y - a.y) * secondWeight,
            a.z + (b.z - a.z) * secondWeight, alpha);
        return detail::linearOutput ? ToLinear(srgb) : srgb;
    }

    inline void Apply()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        // Keep the application's font, spacing and DPI settings. Only visual
        // treatment changes here; calling Apply repeatedly cannot compound scale.
        style.Alpha = 1.0f;
        // Linear framebuffer blending needs more ink to keep disabled labels readable.
        style.DisabledAlpha = UsesLinearOutput() ? 0.75f : 0.60f;
        style.WindowRounding = 3.0f;
        style.ChildRounding = 2.0f;
        style.PopupRounding = 3.0f;
        style.FrameRounding = 2.0f;
        style.ScrollbarRounding = 2.0f;
        style.GrabRounding = 2.0f;
        style.TabRounding = 2.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 1.0f;
        style.TabBarBorderSize = 1.0f;
        style.TabBarOverlineSize = 2.0f;
        style.SeparatorSize = 1.0f;
        style.SeparatorTextBorderSize = 1.0f;
        style.DragDropTargetBorderSize = 2.0f;
        style.DragDropTargetRounding = 2.0f;

        ImVec4* colors = style.Colors;
        const ImVec4 paper = Color(kPaper);
        const ImVec4 hover = Color(kHover);
        const ImVec4 border = Color(kBorder);
        const ImVec4 accent = Color(kAccent);
        // ImGui has one shared text color for normal/selected rows. A tinted
        // fill keeps dark text readable; solid brand blue marks their outlines.
        const ImVec4 selected = Mix(kPaper, kAccent, 0.24f);
        const ImVec4 active = Mix(kPaper, kAccent, 0.34f);
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
            colors[i] = paper;

        colors[ImGuiCol_Text] = Color(kText);
        colors[ImGuiCol_TextDisabled] = Color(kMuted);
        colors[ImGuiCol_WindowBg] = paper;
        colors[ImGuiCol_ChildBg] = paper;
        colors[ImGuiCol_PopupBg] = paper;
        colors[ImGuiCol_Border] = border;
        colors[ImGuiCol_BorderShadow] = Color(kBorder, 0.0f);
        colors[ImGuiCol_FrameBg] = paper;
        colors[ImGuiCol_FrameBgHovered] = hover;
        colors[ImGuiCol_FrameBgActive] = selected;
        colors[ImGuiCol_TitleBg] = Color(kViewport);
        colors[ImGuiCol_TitleBgActive] = selected;
        colors[ImGuiCol_TitleBgCollapsed] = Color(kViewport);
        colors[ImGuiCol_MenuBarBg] = paper;
        colors[ImGuiCol_ScrollbarBg] = Color(kViewport);
        colors[ImGuiCol_ScrollbarGrab] = Color(kMuted);
        colors[ImGuiCol_ScrollbarGrabHovered] = accent;
        colors[ImGuiCol_ScrollbarGrabActive] = Color(kBorder);
        colors[ImGuiCol_CheckMark] = accent;
        colors[ImGuiCol_SliderGrab] = accent;
        colors[ImGuiCol_SliderGrabActive] = Color(kBorder);
        colors[ImGuiCol_Button] = Color(kViewport);
        colors[ImGuiCol_ButtonHovered] = hover;
        colors[ImGuiCol_ButtonActive] = active;
        colors[ImGuiCol_Header] = selected;
        colors[ImGuiCol_HeaderHovered] = hover;
        colors[ImGuiCol_HeaderActive] = active;
        colors[ImGuiCol_Separator] = border;
        colors[ImGuiCol_SeparatorHovered] = accent;
        colors[ImGuiCol_SeparatorActive] = accent;
        colors[ImGuiCol_ResizeGrip] = Color(kBorder, 0.35f);
        colors[ImGuiCol_ResizeGripHovered] = accent;
        colors[ImGuiCol_ResizeGripActive] = accent;
        colors[ImGuiCol_InputTextCursor] = Color(kText);
        colors[ImGuiCol_TabHovered] = hover;
        colors[ImGuiCol_Tab] = Color(kViewport);
        colors[ImGuiCol_TabSelected] = selected;
        colors[ImGuiCol_TabSelectedOverline] = accent;
        colors[ImGuiCol_TabDimmed] = Color(kViewport);
        colors[ImGuiCol_TabDimmedSelected] = Mix(kPaper, kAccent, 0.14f);
        colors[ImGuiCol_TabDimmedSelectedOverline] = accent;
        colors[ImGuiCol_DockingPreview] = Color(kAccent, 0.35f);
        colors[ImGuiCol_DockingEmptyBg] = Color(kViewport);
        colors[ImGuiCol_PlotLines] = accent;
        colors[ImGuiCol_PlotLinesHovered] = Color(kText);
        colors[ImGuiCol_PlotHistogram] = accent;
        colors[ImGuiCol_PlotHistogramHovered] = Color(kBorder);
        colors[ImGuiCol_TableHeaderBg] = hover;
        colors[ImGuiCol_TableBorderStrong] = border;
        colors[ImGuiCol_TableBorderLight] = border;
        colors[ImGuiCol_TableRowBg] = paper;
        colors[ImGuiCol_TableRowBgAlt] = Mix(kPaper, kViewport, 0.55f);
        colors[ImGuiCol_TextLink] = Color(0x365F79);
        colors[ImGuiCol_TextSelectedBg] = selected;
        colors[ImGuiCol_TreeLines] = Color(kMuted);
        colors[ImGuiCol_DragDropTarget] = accent;
        colors[ImGuiCol_DragDropTargetBg] = Color(kAccent, 0.15f);
        colors[ImGuiCol_UnsavedMarker] = accent;
        colors[ImGuiCol_NavCursor] = accent;
        colors[ImGuiCol_NavWindowingHighlight] = accent;
        colors[ImGuiCol_NavWindowingDimBg] = Color(kBorder, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = Color(kBorder, 0.32f);

        ImGuizmo::Style& gizmo = ImGuizmo::GetStyle();
        gizmo.Colors[ImGuizmo::DIRECTION_X] = Color(kAxisX);
        gizmo.Colors[ImGuizmo::DIRECTION_Y] = Color(kAxisY);
        gizmo.Colors[ImGuizmo::DIRECTION_Z] = Color(kAxisZ);
        gizmo.Colors[ImGuizmo::PLANE_X] = Color(kAxisX, 0.30f);
        gizmo.Colors[ImGuizmo::PLANE_Y] = Color(kAxisY, 0.30f);
        gizmo.Colors[ImGuizmo::PLANE_Z] = Color(kAxisZ, 0.30f);
        gizmo.Colors[ImGuizmo::SELECTION] = accent;
        gizmo.Colors[ImGuizmo::INACTIVE] = Color(kMuted, 0.65f);
        gizmo.Colors[ImGuizmo::TRANSLATION_LINE] = Color(kText);
        gizmo.Colors[ImGuizmo::SCALE_LINE] = Color(kText);
        gizmo.Colors[ImGuizmo::ROTATION_USING_BORDER] = accent;
        gizmo.Colors[ImGuizmo::ROTATION_USING_FILL] = Color(kAccent, 0.18f);
        gizmo.Colors[ImGuizmo::HATCHED_AXIS_LINES] = Color(kBorder, 0.65f);
        gizmo.Colors[ImGuizmo::TEXT] = Color(kText);
        gizmo.Colors[ImGuizmo::TEXT_SHADOW] = Color(kPaper);
    }
}
