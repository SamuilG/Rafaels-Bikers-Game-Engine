#include "Runtime/UI/EditorTheme.hpp"
#include "Runtime/UI/EditorLayout.hpp"
#include <ImGuizmo/ImGuizmo.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace theme = engine::editor_theme;
namespace layout = engine::editor_layout;

namespace {
    void Require(bool condition, const std::string& message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message.c_str());
            std::exit(1);
        }
    }

    double Luminance(const ImVec4& color) {
        const auto linear = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * linear(color.x) + 0.7152 * linear(color.y) + 0.0722 * linear(color.z);
    }

    double Contrast(const ImVec4& foreground, const ImVec4& background) {
        const double a = Luminance(foreground), b = Luminance(background);
        return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
    }

    ImVec4 Composite(const ImVec4& color, const ImVec4& background, float alpha) {
        return ImVec4(background.x + (color.x - background.x) * alpha,
            background.y + (color.y - background.y) * alpha,
            background.z + (color.z - background.z) * alpha, 1.0f);
    }

    float EncodeSrgb(float linear) {
        return linear <= 0.0031308f ? 12.92f * linear
            : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }

    float DecodeSrgb(float encoded) {
        return encoded <= 0.04045f ? encoded / 12.92f
            : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }

    void RequireColor(const ImVec4& actual, unsigned expected, const char* label) {
        Require(ImGui::ColorConvertFloat4ToU32(actual) == ImGui::ColorConvertFloat4ToU32(theme::Color(expected)), label);
    }

    void CheckLinearOutput() {
        const auto near = [](float actual, double expected, const char* label) {
            Require(std::abs(actual - expected) < 0.00002, label);
        };
        // Model the sRGB render target's output encoding independently from
        // the production conversion, rather than call its inverse/helper.
        const auto encode = EncodeSrgb;
        Require(!theme::UsesLinearOutput(), "headless palette defaults to direct sRGB values");
        const ImGuiStyle originalStyle = ImGui::GetStyle();
        const ImGuizmo::Style originalGizmo = ImGuizmo::GetStyle();
        theme::SetLinearOutput(true);
        Require(theme::UsesLinearOutput(), "sRGB render target enables linear palette output");
        const ImVec4 gray = theme::Color(0x808080, 0.37f);
        near(gray.x, 0.2158605001, "128/255 uses standard sRGB EOTF exactly once");
        near(gray.w, 0.37, "color conversion preserves alpha");
        near(theme::Color(0x0A0A0A).x, 0.0030352698, "near-black colors use the EOTF linear segment");
        near(theme::Color(0).x, 0.0, "black remains black");
        near(theme::Color(0xFFFFFF).x, 1.0, "white remains white");
        const ImVec4 midpoint = theme::Mix(0, 0xFFFFFF, 0.5f, 0.28f);
        // Mixing decoded endpoints would produce 0.5, not this value; applying
        // EOTF twice would produce about 0.038. Both errors visibly shift tints.
        near(midpoint.x, 0.2140411405, "Mix blends authored sRGB values before one EOTF conversion");
        near(midpoint.w, 0.28, "Mix does not gamma-convert alpha");
        for (unsigned rgb : {theme::kPaper, theme::kViewport, theme::kBorder, theme::kAccent,
            theme::kHover, theme::kText, theme::kMuted, theme::kAxisX, theme::kAxisY, theme::kAxisZ}) {
            const ImVec4 linear = theme::Color(rgb);
            near(encode(linear.x), ((rgb >> 16) & 255) / 255.0, "render target recovers supplied red channel");
            near(encode(linear.y), ((rgb >> 8) & 255) / 255.0, "render target recovers supplied green channel");
            near(encode(linear.z), (rgb & 255) / 255.0, "render target recovers supplied blue channel");
        }
        theme::Apply();
        theme::Apply(); // Applying each editor frame must not compound gamma.
        for (int index = 0; index < ImGuiCol_COUNT; ++index) {
            const ImVec4& converted = ImGui::GetStyle().Colors[index];
            const ImVec4& original = originalStyle.Colors[index];
            near(encode(converted.x), original.x, "repeated Apply recovers authored red, including mixed states");
            near(encode(converted.y), original.y, "repeated Apply recovers authored green, including mixed states");
            near(encode(converted.z), original.z, "repeated Apply recovers authored blue, including mixed states");
            near(converted.w, original.w, "Apply preserves translucent color alpha");
        }
        for (int index = 0; index < ImGuizmo::COUNT; ++index) {
            const ImVec4& converted = ImGuizmo::GetStyle().Colors[index];
            const ImVec4& original = originalGizmo.Colors[index];
            near(encode(converted.x), original.x, "gizmo red receives the same one-time transfer");
            near(encode(converted.y), original.y, "gizmo green receives the same one-time transfer");
            near(encode(converted.z), original.z, "gizmo blue receives the same one-time transfer");
            near(converted.w, original.w, "gizmo conversion preserves alpha");
        }
        theme::SetLinearOutput(false);
        theme::Apply();
        Require(!theme::UsesLinearOutput(), "direct sRGB preview output is restored");
        for (int index = 0; index < ImGuiCol_COUNT; ++index) {
            const ImVec4& restored = ImGui::GetStyle().Colors[index];
            const ImVec4& original = originalStyle.Colors[index];
            near(restored.x, original.x, "switching back restores authored red");
            near(restored.y, original.y, "switching back restores authored green");
            near(restored.z, original.z, "switching back restores authored blue");
            near(restored.w, original.w, "switching back preserves alpha");
        }
        std::puts("PASS sRGB render-target transfer, numerical reference values, authored tint mixing, alpha and mode round-trip");
    }

    void CheckTheme() {
        const auto& style = ImGui::GetStyle();
        RequireColor(style.Colors[ImGuiCol_WindowBg], 0xF0EEE1, "panel background matches supplied ivory");
        RequireColor(style.Colors[ImGuiCol_Text], 0x1F2421, "body text matches supplied ink");
        RequireColor(style.Colors[ImGuiCol_TextDisabled], 0x6B6D6B, "secondary text matches supplied muted gray");
        RequireColor(style.Colors[ImGuiCol_Border], 0x2C3131, "borders match supplied iron gray");
        RequireColor(style.Colors[ImGuiCol_ButtonHovered], 0xE2DEC9, "button hover matches supplied soft gray");
        RequireColor(style.Colors[ImGuiCol_CheckMark], 0x4C7B9B, "checkmarks use brand blue");
        Require(style.FrameBorderSize >= 1.0f && style.WindowBorderSize >= 1.0f && style.PopupBorderSize >= 1.0f,
            "inputs, panels and popup menus retain visible borders");
        for (ImGuiCol background : {ImGuiCol_WindowBg, ImGuiCol_PopupBg, ImGuiCol_FrameBg,
            ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive,
            ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive,
            ImGuiCol_Tab, ImGuiCol_TabSelected, ImGuiCol_TabDimmedSelected}) {
            const double contrast = Contrast(style.Colors[ImGuiCol_Text], style.Colors[background]);
            Require(contrast >= 4.5, std::string("normal/interactive text contrast >= 4.5 on ") + ImGui::GetStyleColorName(background));
        }
        // The exact user-supplied muted gray has 4.48:1 contrast on ivory.
        // Preserve the requested palette without falsely claiming 4.5:1 AA
        // compliance for secondary labels; primary/interactive text uses 4.5.
        Require(Contrast(style.Colors[ImGuiCol_TextDisabled], style.Colors[ImGuiCol_WindowBg]) >= 4.4,
            "secondary labels retain the supplied palette's 4.48:1 contrast");
        const ImVec4 disabled = Composite(style.Colors[ImGuiCol_Text], style.Colors[ImGuiCol_WindowBg], style.DisabledAlpha);
        Require(style.DisabledAlpha > 0.0f && style.DisabledAlpha < 1.0f &&
            Contrast(disabled, style.Colors[ImGuiCol_WindowBg]) >= 3.0,
            "disabled controls are distinct while retaining readable text");
        const auto& gizmo = ImGuizmo::GetStyle();
        RequireColor(gizmo.Colors[ImGuizmo::DIRECTION_X], 0xD9534F, "X axis uses supplied red");
        RequireColor(gizmo.Colors[ImGuizmo::DIRECTION_Y], 0x5CB85C, "Y axis uses supplied green");
        RequireColor(gizmo.Colors[ImGuizmo::DIRECTION_Z], 0x428BCA, "Z axis uses supplied blue");
        std::puts("PASS palette, bordered controls, text contrast, disabled readability and ImGuizmo axis colors");
    }

    void CheckLinearDisabled() {
        Require(theme::UsesLinearOutput(), "preview uses production sRGB-framebuffer mode");
        const auto& style = ImGui::GetStyle();
        const ImVec4& paper = style.Colors[ImGuiCol_WindowBg];
        const ImVec4 blended = Composite(style.Colors[ImGuiCol_Text], paper, style.DisabledAlpha);
        const ImVec4 displayed(EncodeSrgb(blended.x), EncodeSrgb(blended.y), EncodeSrgb(blended.z), 1);
        const ImVec4 displayedPaper(EncodeSrgb(paper.x), EncodeSrgb(paper.y), EncodeSrgb(paper.z), 1);
        const double contrast = Contrast(displayed, displayedPaper);
        Require(contrast >= 3.0 && style.DisabledAlpha < 1.0f,
            "disabled text stays distinct and readable after actual linear framebuffer blending");
        std::printf("PASS production linear framebuffer disabled-text contrast %.2f:1\n", contrast);
    }

    ImVec2 hoverTarget;
    bool hoveredButton = false;

    void DrawSceneFixture() {
        if (ImGui::Begin(layout::kSceneViewport)) {
            ImGui::TextDisabled("THEME FIXTURE / schematic scene, not a live game capture");
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 end(origin.x + available.x, origin.y + available.y);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, end, theme::ColorU32(theme::kViewport));
            draw->PushClipRect(origin, end, true);
            const ImU32 minor = theme::ColorU32(theme::kBorder, 0.08f);
            const ImU32 major = theme::ColorU32(theme::kBorder, 0.16f);
            for (int index = 0; index < static_cast<int>(available.x / 24); ++index)
                draw->AddLine(ImVec2(origin.x + index * 24, origin.y), ImVec2(origin.x + index * 24, end.y), index % 4 ? minor : major);
            for (int index = 0; index < static_cast<int>(available.y / 24); ++index)
                draw->AddLine(ImVec2(origin.x, origin.y + index * 24), ImVec2(end.x, origin.y + index * 24), index % 4 ? minor : major);
            const ImVec2 center(origin.x + available.x * 0.5f, origin.y + available.y * 0.5f);
            const ImVec2 points[] = {
                {center.x - 95, center.y - 20}, {center.x, center.y - 80}, {center.x + 95, center.y - 20},
                {center.x, center.y + 40}, {center.x - 95, center.y + 75}, {center.x, center.y + 135}, {center.x + 95, center.y + 75}
            };
            draw->AddQuadFilled(points[0], points[1], points[2], points[3], theme::ColorU32(theme::kPaper));
            draw->AddQuadFilled(points[0], points[3], points[5], points[4], ImGui::GetColorU32(ImGuiCol_Header));
            draw->AddQuadFilled(points[3], points[2], points[6], points[5], theme::ColorU32(theme::kHover));
            for (const auto& edge : {std::pair{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 4}, {4, 5}, {5, 6}, {6, 2}, {3, 5}})
                draw->AddLine(points[edge.first], points[edge.second], theme::ColorU32(theme::kAccent), 2.0f);
            const ImVec2 axis(center.x, center.y + 40);
            draw->AddLine(axis, ImVec2(axis.x + 150, axis.y + 18), theme::ColorU32(theme::kAxisX), 3.0f);
            draw->AddLine(axis, ImVec2(axis.x, axis.y - 150), theme::ColorU32(theme::kAxisY), 3.0f);
            draw->AddLine(axis, ImVec2(axis.x - 125, axis.y + 75), theme::ColorU32(theme::kAxisZ), 3.0f);
            draw->AddText(ImVec2(axis.x + 156, axis.y + 10), theme::ColorU32(theme::kText), "X");
            draw->AddText(ImVec2(axis.x + 10, axis.y - 150), theme::ColorU32(theme::kText), "Y");
            draw->AddText(ImVec2(axis.x - 142, axis.y + 70), theme::ColorU32(theme::kText), "Z");
            draw->AddRect(origin, end, theme::ColorU32(theme::kBorder));
            draw->PopClipRect();
            ImGui::Dummy(available);
        }
        ImGui::End();
    }

    void DrawFixture() {
        if (ImGui::BeginMainMenuBar()) {
            ImGui::TextUnformatted("STEER ENGINE");
            ImGui::Separator();
            if (ImGui::BeginMenu("File")) { ImGui::MenuItem("Save Scene Snapshot", "Ctrl+S"); ImGui::EndMenu(); }
            if (ImGui::BeginMenu("View")) { ImGui::MenuItem("Reset Editor Layout"); ImGui::EndMenu(); }
            if (ImGui::BeginMenu("Tools")) { ImGui::MenuItem("Diagnostics"); ImGui::EndMenu(); }
            ImGui::EndMainMenuBar();
        }
        layout::DrawDockspace();
        if (ImGui::Begin(layout::kRenderSettings)) ImGui::TextUnformatted("Render configuration");
        ImGui::End();
        if (ImGui::Begin(layout::kSceneHierarchy)) {
            char filter[48] = "";
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##filter", "Search scene...", filter, sizeof(filter));
            ImGui::Spacing();
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            if (ImGui::TreeNode("Scene")) {
                for (const char* object : {"Main Camera", "Directional Light", "Grid", "Selected Mesh", "Particles"})
                    ImGui::Selectable(object, std::string(object) == "Selected Mesh");
                ImGui::TreePop();
            }
            ImGui::Separator();
            ImGui::TextDisabled("5 entities");
        }
        ImGui::End();
        DrawSceneFixture();
        if (ImGui::Begin(layout::kEntityInspector)) {
            ImGui::TextUnformatted("Selected Mesh");
            ImGui::TextDisabled("Entity / Static Mesh");
            ImGui::Separator();
            bool visible = true;
            ImGui::Checkbox("Visible", &visible);
            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const char* label : {"Position", "Rotation", "Scale"}) {
                    float values[] = {0.0f, 0.0f, 0.0f};
                    if (std::string(label) == "Scale") values[0] = values[1] = values[2] = 1.0f;
                    ImGui::TextUnformatted(label);
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat3((std::string("##") + label).c_str(), values, 0.01f, -10, 10, "%.2f");
                }
            }
            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
                int shader = 0;
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##shader", &shader, "Standard Lit\0Unlit\0");
                float roughness = 0.65f;
                ImGui::TextUnformatted("Roughness");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##roughness", &roughness, 0, 1, "%.2f");
                ImGui::Button("Apply", ImVec2(100, 0));
                ImGui::SameLine();
                ImGui::Button("Hover state", ImVec2(110, 0));
                hoveredButton = ImGui::IsItemHovered();
                const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                hoverTarget = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(theme::Color(theme::kWarning), "Controlled by animation");
            ImGui::BeginDisabled();
            bool driven = true;
            ImGui::Checkbox("Emitter follows target", &driven);
            ImGui::Button("Unavailable action");
            ImGui::EndDisabled();
            ImGui::TextDisabled("Read-only until playback stops.");
        }
        ImGui::End();
        if (ImGui::Begin(layout::kConsole)) ImGui::TextUnformatted("[Info] Theme fixture initialized");
        ImGui::End();
        if (ImGui::Begin(layout::kAssets)) {
            ImGui::TextDisabled("Assets  /  Models");
            ImGui::Separator();
            if (ImGui::BeginTable("assets", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                for (const char* column : {"Name", "Type", "Size", "Status"}) ImGui::TableSetupColumn(column);
                ImGui::TableHeadersRow();
                const char* rows[][4] = {{"Cube.mesh", "Static Mesh", "24 vertices", "Ready"}, {"Paper.mat", "Material", "PBR", "Ready"}, {"Grid.png", "Texture", "1024 x 1024", "Ready"}};
                for (const auto& row : rows) {
                    ImGui::TableNextRow();
                    for (int col = 0; col < 4; ++col) { ImGui::TableSetColumnIndex(col); ImGui::TextUnformatted(row[col]); }
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    // Rasterize real ImGui draw commands and their font atlas. The fixture is
    // intentionally independent of Vulkan so its output is reproducible in CI.
    void SavePreview(const char* path, int width, int height) {
        unsigned char* atlas;
        int atlasWidth, atlasHeight;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas, &atlasWidth, &atlasHeight);
        const bool linearOutput = theme::UsesLinearOutput();
        const ImVec4 paper = theme::Color(theme::kPaper);
        const auto encodeOutput = [linearOutput](float value) { return linearOutput ? EncodeSrgb(value) : value; };
        std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
        for (size_t index = 0; index < pixels.size(); index += 4) {
            pixels[index] = static_cast<unsigned char>(encodeOutput(paper.x) * 255 + 0.5f);
            pixels[index + 1] = static_cast<unsigned char>(encodeOutput(paper.y) * 255 + 0.5f);
            pixels[index + 2] = static_cast<unsigned char>(encodeOutput(paper.z) * 255 + 0.5f);
            pixels[index + 3] = 255;
        }
        const auto edge = [](const ImVec2& a, const ImVec2& b, float x, float y) {
            return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
        };
        const ImDrawData& data = *ImGui::GetDrawData();
        Require(data.TotalVtxCount > 1000, "fixture generated real widget draw data");
        size_t triangleCount = 0;
        for (const ImDrawList* list : data.CmdLists) {
            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.UserCallback) continue;
                const int clipMinX = std::max(0, static_cast<int>(std::ceil(command.ClipRect.x)));
                const int clipMinY = std::max(0, static_cast<int>(std::ceil(command.ClipRect.y)));
                const int clipMaxX = std::min(width, static_cast<int>(command.ClipRect.z));
                const int clipMaxY = std::min(height, static_cast<int>(command.ClipRect.w));
                for (unsigned index = 0; index + 2 < command.ElemCount; index += 3) {
                    const ImDrawVert& a = list->VtxBuffer[list->IdxBuffer[command.IdxOffset + index] + command.VtxOffset];
                    const ImDrawVert& b = list->VtxBuffer[list->IdxBuffer[command.IdxOffset + index + 1] + command.VtxOffset];
                    const ImDrawVert& c = list->VtxBuffer[list->IdxBuffer[command.IdxOffset + index + 2] + command.VtxOffset];
                    const float area = edge(b.pos, c.pos, a.pos.x, a.pos.y);
                    if (std::abs(area) < 0.0001f) continue;
                    ++triangleCount;
                    const ImVec4 colors[] = {ImGui::ColorConvertU32ToFloat4(a.col), ImGui::ColorConvertU32ToFloat4(b.col), ImGui::ColorConvertU32ToFloat4(c.col)};
                    const int minX = std::max(clipMinX, static_cast<int>(std::floor(std::min({a.pos.x, b.pos.x, c.pos.x}))));
                    const int minY = std::max(clipMinY, static_cast<int>(std::floor(std::min({a.pos.y, b.pos.y, c.pos.y}))));
                    const int maxX = std::min(clipMaxX, static_cast<int>(std::ceil(std::max({a.pos.x, b.pos.x, c.pos.x}))));
                    const int maxY = std::min(clipMaxY, static_cast<int>(std::ceil(std::max({a.pos.y, b.pos.y, c.pos.y}))));
                    for (int y = minY; y < maxY; ++y) for (int x = minX; x < maxX; ++x) {
                        const float wa = edge(b.pos, c.pos, x + 0.5f, y + 0.5f) / area;
                        const float wb = edge(c.pos, a.pos, x + 0.5f, y + 0.5f) / area;
                        const float wc = 1.0f - wa - wb;
                        if (wa < 0 || wb < 0 || wc < 0) continue;
                        const float u = (wa * a.uv.x + wb * b.uv.x + wc * c.uv.x) * atlasWidth - 0.5f;
                        const float v = (wa * a.uv.y + wb * b.uv.y + wc * c.uv.y) * atlasHeight - 0.5f;
                        const int sx = static_cast<int>(std::floor(u)), sy = static_cast<int>(std::floor(v));
                        const float tx = u - sx, ty = v - sy;
                        const auto sample = [&](int px, int py, int channel) {
                            return atlas[(std::clamp(py, 0, atlasHeight - 1) * atlasWidth + std::clamp(px, 0, atlasWidth - 1)) * 4 + channel] / 255.0f;
                        };
                        float texture[4];
                        for (int channel = 0; channel < 4; ++channel)
                            texture[channel] = (sample(sx, sy, channel) * (1 - tx) + sample(sx + 1, sy, channel) * tx) * (1 - ty) +
                                (sample(sx, sy + 1, channel) * (1 - tx) + sample(sx + 1, sy + 1, channel) * tx) * ty;
                        const float alpha = (wa * colors[0].w + wb * colors[1].w + wc * colors[2].w) * texture[3];
                        unsigned char* destination = &pixels[(static_cast<size_t>(y) * width + x) * 4];
                        for (int channel = 0; channel < 3; ++channel) {
                            const float color = (wa * (&colors[0].x)[channel] + wb * (&colors[1].x)[channel] + wc * (&colors[2].x)[channel]) * texture[channel];
                            // An sRGB attachment decodes the stored destination,
                            // blends linear RGB, then encodes the stored result.
                            // Source vertices are already linear via the theme;
                            // the atlas is UNORM and only modulates glyph alpha.
                            float background = destination[channel] / 255.0f;
                            if (linearOutput) background = DecodeSrgb(background);
                            const float blended = color * alpha + background * (1 - alpha);
                            destination[channel] = static_cast<unsigned char>(std::clamp(encodeOutput(blended), 0.0f, 1.0f) * 255 + 0.5f);
                        }
                    }
                }
            }
        }
        Require(stbi_write_png(path, width, height, 4, pixels.data(), width * 4) != 0, "wrote theme fixture preview");
        std::printf("PASS software-rendered %zu real ImGui triangles (%s framebuffer) to %s\n",
            triangleCount, linearOutput ? "linear blending / sRGB output" : "direct", path);
    }
}

int main(int argc, char** argv) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1600, 960);
    io.DeltaTime = 1.0f / 60;
    // Match the actual editor's font and 18 px size. The fixture needs Latin
    // glyphs only, avoiding an unrelated full CJK-atlas allocation in this test.
    Require(io.Fonts->AddFontFromFileTTF(argc > 2 ? argv[2] : "Assets/Fonts/simhei.ttf", 18.0f) != nullptr,
        "loaded the actual editor font for representative widget metrics");
    unsigned char* atlas;
    int atlasWidth, atlasHeight;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &atlasWidth, &atlasHeight);
    io.Fonts->SetTexID(1);
    theme::Apply();
    CheckLinearOutput();
    CheckTheme();
    theme::SetLinearOutput(true);
    theme::Apply();
    CheckLinearDisabled();
    for (int frame = 0; frame < 9; ++frame) {
        if (frame >= 6) io.AddMousePosEvent(hoverTarget.x, hoverTarget.y);
        ImGui::NewFrame();
        DrawFixture();
        ImGui::Render();
    }
    Require(hoveredButton, "real mouse input reaches the hovered-button state in the docked inspector");
    const ImGuiWindow* inspector = ImGui::FindWindowByName(layout::kEntityInspector);
    Require(inspector && inspector->DockNode && inspector->DockTabIsVisible, "inspector fixture is visible in real docking layout");
    const ImU32 hoverColor = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
    bool renderedHover = false;
    for (const ImDrawVert& vertex : inspector->DrawList->VtxBuffer) renderedHover |= vertex.col == hoverColor;
    Require(renderedHover, "hovered button emits the theme's hover color into actual render data");
    SavePreview(argc > 1 ? argv[1] : "preview.png", 1600, 960);
    theme::SetLinearOutput(false);
    theme::Apply();
    ImGui::DestroyContext();
    std::puts("PASS all editor theme tests (real ImGui widgets; schematic fixture, no live engine screenshot)");
}
