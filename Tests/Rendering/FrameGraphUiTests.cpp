#include "Runtime/UI/EngineUi.hpp"
#include "Runtime/Renderer/FrameGraph.hpp"
#include "Runtime/UserState/EditorState.hpp"
#include <imgui_internal.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>

using Graph = engine::FrameGraph;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Graph Fixture() {
    Graph graph; graph.Begin("Scene viewport (UI fixture)", 42);
    const Graph::TextureDesc desc{1280, 720, 1, 1, 8, "RGBA16F"};
    std::vector<Graph::Id> targets;
    for (const char* name : {"Scene color", "Bright color", "Scene depth", "SSR reflection", "Bloom vertical", "Composite color", "Viewport output", "Swapchain"}) {
        auto targetDesc = desc;
        if (std::string(name) == "Scene depth") { targetDesc.format = "D32F"; targetDesc.bytesPerTexel = 4; }
        if (std::string(name) == "Viewport output" || std::string(name) == "Swapchain") { targetDesc.format = "BGRA8 sRGB"; targetDesc.bytesPerTexel = 4; }
        targetDesc.external = std::string(name) == "Swapchain";
        targets.push_back(graph.RegisterTexture(targets.size() + 1, name, targetDesc));
    }
    auto use = [&](Graph::Id pass, int texture, Graph::Access access) { graph.UseTexture(pass, targets[texture], access); };
    auto pass = graph.AddPass("Forward MRT", "Graphics MRT");
    for (int i : {0, 1}) use(pass, i, Graph::Access::Color);
    use(pass, 2, Graph::Access::Depth);
    pass = graph.AddPass("SSR", "Post process"); use(pass, 0, Graph::Access::Sample); use(pass, 2, Graph::Access::Sample); use(pass, 3, Graph::Access::Color);
    pass = graph.AddPass("Bloom blur", "Post process"); use(pass, 1, Graph::Access::Sample); use(pass, 4, Graph::Access::Color);
    pass = graph.AddPass("Composite / tone mapping", "Post process");
    for (int i : {0, 3, 4}) use(pass, i, Graph::Access::Sample); use(pass, 5, Graph::Access::Color);
    pass = graph.AddPass("Particles / debug overlays", "Overlay"); use(pass, 5, Graph::Access::LoadColor); use(pass, 2, Graph::Access::DepthRead);
    pass = graph.AddPass("Camera effects / viewport output", "Post process"); use(pass, 5, Graph::Access::Sample); use(pass, 6, Graph::Access::Color);
    pass = graph.AddPass("Editor / runtime UI", "UI"); use(pass, 6, Graph::Access::Sample); use(pass, 7, Graph::Access::Color);
    pass = graph.AddPass("Present", "Present"); use(pass, 7, Graph::Access::Present);
    graph.Compile(); return graph;
}

// Rasterize actual production ImGui draw data for layout review. All textures
// in this standalone panel are the font atlas (including its white pixel).
void SaveImage(const std::filesystem::path& path) {
    constexpr int width = 1200, height = 800;
    unsigned char* atlas = nullptr; int aw = 0, ah = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);
    std::vector<unsigned char> pixels(width * height * 4, 22);
    for (int i = 3; i < int(pixels.size()); i += 4) pixels[i] = 255;
    const auto* data = ImGui::GetDrawData();
    Require(data && data->TotalVtxCount > 100, "Panel did not generate draw geometry");
    for (const auto* list : data->CmdLists) for (const auto& command : list->CmdBuffer) {
        if (command.UserCallback) continue;
        for (unsigned i = 0; i + 2 < command.ElemCount; i += 3) {
            std::array<ImDrawVert, 3> v;
            for (int k = 0; k < 3; ++k) v[k] = list->VtxBuffer[list->IdxBuffer[command.IdxOffset + i + k] + command.VtxOffset];
            const float area = (v[1].pos.y - v[2].pos.y) * (v[0].pos.x - v[2].pos.x) + (v[2].pos.x - v[1].pos.x) * (v[0].pos.y - v[2].pos.y);
            if (std::abs(area) < 0.00001f) continue;
            const int left = std::max({0, int(std::floor(std::min({v[0].pos.x, v[1].pos.x, v[2].pos.x}))), int(command.ClipRect.x)});
            const int top = std::max({0, int(std::floor(std::min({v[0].pos.y, v[1].pos.y, v[2].pos.y}))), int(command.ClipRect.y)});
            const int right = std::min({width, int(std::ceil(std::max({v[0].pos.x, v[1].pos.x, v[2].pos.x}))), int(command.ClipRect.z)});
            const int bottom = std::min({height, int(std::ceil(std::max({v[0].pos.y, v[1].pos.y, v[2].pos.y}))), int(command.ClipRect.w)});
            std::array<ImVec4, 3> colors;
            for (int k = 0; k < 3; ++k) colors[k] = ImGui::ColorConvertU32ToFloat4(v[k].col);
            for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float a = ((v[1].pos.y - v[2].pos.y) * (px - v[2].pos.x) + (v[2].pos.x - v[1].pos.x) * (py - v[2].pos.y)) / area;
                const float b = ((v[2].pos.y - v[0].pos.y) * (px - v[2].pos.x) + (v[0].pos.x - v[2].pos.x) * (py - v[2].pos.y)) / area;
                const float c = 1 - a - b;
                if (a < 0 || b < 0 || c < 0) continue;
                const float u = a * v[0].uv.x + b * v[1].uv.x + c * v[2].uv.x;
                const float t = a * v[0].uv.y + b * v[1].uv.y + c * v[2].uv.y;
                const auto* texel = atlas + 4 * (std::clamp(int(t * ah), 0, ah - 1) * aw + std::clamp(int(u * aw), 0, aw - 1));
                const float alpha = (a * colors[0].w + b * colors[1].w + c * colors[2].w) * texel[3] / 255.f;
                const float color[3] = {a * colors[0].x + b * colors[1].x + c * colors[2].x,
                    a * colors[0].y + b * colors[1].y + c * colors[2].y, a * colors[0].z + b * colors[1].z + c * colors[2].z};
                auto* pixel = pixels.data() + 4 * (y * width + x);
                for (int channel = 0; channel < 3; ++channel) pixel[channel] = static_cast<unsigned char>(std::clamp(
                    color[channel] * texel[channel] * alpha + pixel[channel] * (1 - alpha), 0.f, 255.f));
            }
        }
    }
    Require(stbi_write_png(path.string().c_str(), width, height, 4, pixels.data(), width * 4) != 0, "Cannot write panel layout image");
}

int main(int argc, char** argv) try {
    Require(argc == 2, "Specify output directory");
    const std::filesystem::path output = argv[1];
    ImGui::CreateContext(); auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = {1200, 800}; io.DeltaTime = 1.f / 60;
    unsigned char* atlas = nullptr; int w, h; io.Fonts->GetTexDataAsRGBA32(&atlas, &w, &h); io.Fonts->SetTexID(ImTextureID(1));
    engine::EditorState editor; editor.showFrameGraph = true;
    auto graph = Fixture();
    auto frame = [&]() {
        ImGui::NewFrame(); ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always); ImGui::SetNextWindowSize({1180, 780}, ImGuiCond_Always);
        engine::EngineUi::DrawFrameGraph(editor, graph); ImGui::Render();
        Require(ImGui::GetDrawData()->TotalVtxCount > 100, "Panel tab has no geometry");
    };
    frame(); frame();
    io.AddMousePosEvent(70, 205); io.AddMouseButtonEvent(0, true); frame();
    io.AddMouseButtonEvent(0, false); frame(); io.AddMousePosEvent(-100, -100); frame(); SaveImage(output / "graph.png");
    for (const char* name : {"RT lifetime", "Render targets"}) {
        bool found = false;
        auto& pool = ImGui::GetCurrentContext()->TabBars;
        for (int i = 0; i < pool.GetMapSize(); ++i) if (auto* bar = pool.TryGetMapData(i)) {
            for (auto& tab : bar->Tabs) if (std::string(ImGui::TabBarGetTabName(bar, &tab)) == name) {
                bar->NextSelectedTabId = tab.ID; found = true;
            }
        }
        Require(found, "Panel tab was not created"); frame(); frame();
        if (std::string(name) == "Render targets") {
            io.AddMousePosEvent(80, 186); io.AddMouseButtonEvent(0, true); frame(); io.AddMouseButtonEvent(0, false); frame();
        }
        SaveImage(output / (std::string(name) == "RT lifetime" ? "lifetime.png" : "resources.png"));
    }
    graph.Begin("Empty fixture", 43); frame();
    engine::EngineUi::ResetFrameGraphPanel(); frame();
    ImGui::DestroyContext();
    std::cout << "PASS production Graph / RT lifetime / Render targets panel layouts and empty/reset states\n";
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
