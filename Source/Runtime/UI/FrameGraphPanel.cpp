#include "EngineUi.hpp"
#include "../Renderer/FrameGraph.hpp"
#include "../UserState/EditorState.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace engine {
namespace {
using Graph = FrameGraph;
struct PanelState {
    ImGuiTextFilter filter;
    float zoom = 0.7f;
    ImVec2 pan{24, 24};
    int pass = -1, texture = -1;
    std::string selectionName;
    bool fit = false, focus = false, ordering = false, unused = false;
} state;
const ImU32 passColor = IM_COL32(72, 145, 241, 255);
const ImU32 rtColor = IM_COL32(235, 166, 65, 255);
const ImU32 rwColor = IM_COL32(174, 113, 227, 255);
bool Inside(ImVec2 p, ImVec2 min, ImVec2 max) { return p.x >= min.x && p.y >= min.y && p.x < max.x && p.y < max.y; }
ImVec2 Add(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
void SelectPass(const Graph& graph, int index) {
    state.pass = index; state.texture = -1; state.selectionName = graph.passes[index].name;
}
void SelectTexture(const Graph& graph, int index) {
    state.texture = index; state.pass = -1; state.selectionName = graph.textures[index].name;
}
bool ResourceMatches(const Graph::Texture& texture) { return state.filter.PassFilter(texture.name.c_str()); }
void Legend() {
    ImGui::TextColored(ImColor(passColor), "Read"); ImGui::SameLine();
    ImGui::TextColored(ImColor(rtColor), "Write / RT"); ImGui::SameLine();
    ImGui::TextColored(ImColor(rwColor), "Read + write");
}
void Details(const Graph& graph) {
    ImGui::Separator();
    if (state.pass >= 0) {
        const auto& pass = graph.passes[state.pass];
        ImGui::Text("#%d  %s", state.pass, pass.name.c_str());
        ImGui::TextDisabled("%s | %zu RT accesses", pass.category.c_str(), pass.uses.size());
        if (ImGui::BeginTable("accesses", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, {0, 112})) {
            ImGui::TableSetupColumn("RT"); ImGui::TableSetupColumn("Access"); ImGui::TableSetupColumn("Content versions"); ImGui::TableHeadersRow();
            for (const auto& use : pass.uses) {
                ImGui::PushID(int(use.texture)); ImGui::TableNextRow(); ImGui::TableNextColumn();
                if (ImGui::Selectable(graph.textures[use.texture].name.c_str())) SelectTexture(graph, int(use.texture));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(Graph::AccessName(use.access));
                ImGui::TableNextColumn();
                if (use.input != Graph::Invalid) { ImGui::Text("Read v%u", graph.versions[use.input].number); ImGui::SameLine(); }
                if (use.output != Graph::Invalid) ImGui::Text("Write v%u", graph.versions[use.output].number);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else if (state.texture >= 0) {
        const auto& texture = graph.textures[state.texture]; const auto& desc = texture.desc;
        ImGui::TextUnformatted(texture.name.c_str());
        ImGui::Text("%s | %u x %u | %ux MSAA | %.2f MiB estimated", desc.format.c_str(), desc.width, desc.height,
            desc.samples, double(desc.EstimatedBytes()) / 1048576.0);
        ImGui::Text("Image 0x%llx | layer %u | %s", static_cast<unsigned long long>(texture.image), texture.layer,
            desc.external ? "External image" : desc.importedContents ? "Persistent / previous-frame contents" : "Persistent engine RT");
        if (texture.firstUse != Graph::Invalid) ImGui::Text("Frame use interval: Pass #%u to #%u", texture.firstUse, texture.lastUse);
        else ImGui::TextDisabled("Registered, unused in this captured frame");
        ImGui::BeginChild("rt-users", {0, 75});
        for (size_t i = 0; i < graph.passes.size(); ++i) for (const auto& use : graph.passes[i].uses)
            if (use.texture == uint32_t(state.texture)) {
                ImGui::PushID(int(i));
                const auto label = "#" + std::to_string(i) + " " + graph.passes[i].name + " - " + Graph::AccessName(use.access);
                if (ImGui::Selectable(label.c_str())) SelectPass(graph, int(i));
                ImGui::PopID(); break;
            }
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("Select a Pass or RT to inspect accesses, versions and lifetime.");
    }
}
void DrawNodes(const Graph& graph) {
    struct Node { bool pass; uint32_t index; ImVec2 pos; };
    std::vector<Node> nodes;
    std::vector<size_t> passNodes(graph.passes.size()), versionNodes(graph.versions.size());
    uint32_t maxColumn = 0;
    for (const auto& pass : graph.passes) maxColumn = std::max(maxColumn, pass.level + 1);
    std::vector<uint32_t> rows(maxColumn + 1);
    for (uint32_t i = 0; i < graph.passes.size(); ++i) {
        const auto column = graph.passes[i].level;
        passNodes[i] = nodes.size(); nodes.push_back({true, i, {float(column) * 246, float(rows[column]++) * 92}});
    }
    for (uint32_t i = 0; i < graph.versions.size(); ++i) {
        const auto& version = graph.versions[i];
        const auto column = version.producer == Graph::Invalid ? 0u : graph.passes[version.producer].level + 1;
        versionNodes[i] = nodes.size(); nodes.push_back({false, i, {float(column) * 246, float(rows[column]++) * 92}});
    }
    float minX = std::numeric_limits<float>::max(), maxX = 0;
    for (const auto& node : nodes) minX = std::min(minX, node.pos.x);
    for (auto& node : nodes) { node.pos.x -= minX; maxX = std::max(maxX, node.pos.x + 216); }
    ImGui::BeginChild("node-canvas", {0, std::max(180.f, ImGui::GetContentRegionAvail().y - 190.f)}, true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const auto origin = ImGui::GetCursorScreenPos(); const auto available = ImGui::GetContentRegionAvail();
    const ImVec2 size{std::max(available.x, 1.f), std::max(available.y, 1.f)};
    ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered(); const auto& io = ImGui::GetIO();
    if (state.fit && !nodes.empty()) {
        const float width = maxX;
        const float height = float(*std::max_element(rows.begin(), rows.end())) * 92;
        state.zoom = std::clamp(std::min((size.x - 36) / width, (size.y - 36) / std::max(height, 1.f)), 0.06f, 1.f);
        state.pan = {18, 18}; state.fit = false;
    }
    if (state.focus) {
        for (const auto& node : nodes) if ((node.pass && int(node.index) == state.pass) ||
            (!node.pass && int(graph.versions[node.index].texture) == state.texture)) {
            state.zoom = 0.85f;
            state.pan = {size.x * 0.5f - (node.pos.x + 108) * state.zoom, size.y * 0.5f - (node.pos.y + 34) * state.zoom};
            break;
        }
        state.focus = false;
    }
    if (hovered && io.MouseWheel != 0) {
        const auto local = ImVec2{(io.MousePos.x - origin.x - state.pan.x) / state.zoom,
            (io.MousePos.y - origin.y - state.pan.y) / state.zoom};
        state.zoom = std::clamp(state.zoom * std::pow(1.15f, io.MouseWheel), 0.06f, 1.8f);
        state.pan = {io.MousePos.x - origin.x - local.x * state.zoom, io.MousePos.y - origin.y - local.y * state.zoom};
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0)) state.pan = Add(state.pan, io.MouseDelta);
    auto position = [&](const Node& node) { return Add(origin, Add(state.pan, {node.pos.x * state.zoom, node.pos.y * state.zoom})); };
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, Add(origin, size), true);
    auto connection = [&](size_t from, size_t to, ImU32 color) {
        auto a = Add(position(nodes[from]), {216 * state.zoom, 32 * state.zoom});
        const auto b = Add(position(nodes[to]), {0, 32 * state.zoom});
        const float bend = std::max(30.f, (b.x - a.x) * 0.5f);
        draw->AddBezierCubic(a, Add(a, {bend, 0}), Add(b, {-bend, 0}), b, color, std::max(1.f, 1.5f * state.zoom));
        draw->AddTriangleFilled(b, Add(b, {-6, -3}), Add(b, {-6, 3}), color);
    };
    if (state.ordering) for (const auto& dep : graph.dependencies)
        if (dep.hazard != Graph::Hazard::ReadAfterWrite)
            connection(passNodes[dep.before], passNodes[dep.after], IM_COL32(130, 140, 158, 90));
    for (uint32_t i = 0; i < graph.versions.size(); ++i) {
        const auto& version = graph.versions[i];
        const bool related = state.texture == int(version.texture) || state.pass == int(version.producer) ||
            (state.pass >= 0 && std::find(version.readers.begin(), version.readers.end(), uint32_t(state.pass)) != version.readers.end());
        const int alpha = state.pass < 0 && state.texture < 0 ? 155 : related ? 255 : 35;
        if (version.producer != Graph::Invalid) connection(passNodes[version.producer], versionNodes[i], IM_COL32(235, 166, 65, alpha));
        for (const auto reader : version.readers) connection(versionNodes[i], passNodes[reader], IM_COL32(72, 145, 241, alpha));
    }
    bool hit = false;
    for (const auto& node : nodes) {
        const auto p = position(node); const auto end = Add(p, {216 * state.zoom, 68 * state.zoom});
        const auto& name = node.pass ? graph.passes[node.index].name : graph.textures[graph.versions[node.index].texture].name;
        const bool selected = node.pass ? state.pass == int(node.index) : state.texture == int(graph.versions[node.index].texture);
        const bool match = state.filter.PassFilter(name.c_str());
        const auto color = node.pass ? passColor : rtColor;
        draw->AddRectFilled(p, end, match ? IM_COL32(29, 36, 49, 255) : IM_COL32(20, 24, 32, 200), 5);
        draw->AddRect(p, end, selected ? IM_COL32(255, 255, 255, 255) : match ? color : IM_COL32(65, 68, 74, 180), 5, 0, selected ? 2.f : 1.f);
        const auto title = node.pass ? "#" + std::to_string(node.index) + " " + name : name;
        const auto secondary = node.pass ? graph.passes[node.index].category :
            "v" + std::to_string(graph.versions[node.index].number) + " | " + graph.textures[graph.versions[node.index].texture].desc.format +
                (graph.versions[node.index].producer == Graph::Invalid ?
                    graph.versions[node.index].initialized ? " | previous frame" : " | UNWRITTEN" : "");
        const ImVec4 clip{p.x + 2, p.y, end.x - 3, end.y};
        const float font = std::max(5.f, ImGui::GetFontSize() * state.zoom);
        draw->AddText(ImGui::GetFont(), font, Add(p, {8 * state.zoom, 10 * state.zoom}), match ? IM_COL32_WHITE : IM_COL32(100, 100, 110, 255), title.c_str(), nullptr, 200 * state.zoom, &clip);
        draw->AddText(ImGui::GetFont(), font, Add(p, {8 * state.zoom, 38 * state.zoom}), color, secondary.c_str(), nullptr, 0, &clip);
        if (hovered && Inside(io.MousePos, p, end)) {
            hit = true;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (node.pass) SelectPass(graph, int(node.index));
                else SelectTexture(graph, int(graph.versions[node.index].texture));
            }
            ImGui::BeginTooltip(); ImGui::TextUnformatted(title.c_str()); ImGui::TextUnformatted(secondary.c_str()); ImGui::EndTooltip();
        }
    }
    if (hovered && !hit && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { state.pass = state.texture = -1; }
    draw->PopClipRect(); ImGui::EndChild();
    ImGui::TextDisabled("Wheel: zoom at cursor | Middle drag: pan | Click: inspect | RT vN: contents after each write");
}
void DrawLifetime(const Graph& graph) {
    ImGui::BeginChild("lifetime", {0, std::max(180.f, ImGui::GetContentRegionAvail().y - 175.f)}, true, ImGuiWindowFlags_HorizontalScrollbar);
    const auto origin = ImGui::GetCursorScreenPos(); auto* draw = ImGui::GetWindowDrawList();
    const auto mouse = ImGui::GetMousePos(); const bool hover = ImGui::IsWindowHovered();
    constexpr float labelWidth = 235, cell = 38, row = 28;
    for (size_t p = 0; p < graph.passes.size(); ++p) {
        const auto pos = Add(origin, {labelWidth + float(p) * cell, 0});
        const auto text = "#" + std::to_string(p); draw->AddText(pos, passColor, text.c_str());
        if (hover && Inside(mouse, pos, Add(pos, {cell, row}))) ImGui::SetTooltip("%s", graph.passes[p].name.c_str());
    }
    uint32_t r = 1;
    for (uint32_t i = 0; i < graph.textures.size(); ++i) {
        const auto& texture = graph.textures[i];
        if ((!state.unused && texture.firstUse == Graph::Invalid) || !ResourceMatches(texture)) continue;
        const auto pos = Add(origin, {0, float(r++) * row});
        draw->AddText(pos, state.texture == int(i) ? IM_COL32_WHITE : rtColor, texture.name.c_str());
        if (hover && Inside(mouse, pos, Add(pos, {labelWidth, row})) && ImGui::IsMouseClicked(0)) SelectTexture(graph, int(i));
        if (texture.firstUse == Graph::Invalid) continue;
        const auto start = Add(pos, {labelWidth + float(texture.firstUse) * cell, 2});
        const auto end = Add(pos, {labelWidth + float(texture.lastUse + 1) * cell - 3, row - 3});
        draw->AddRectFilled(start, end, IM_COL32(55, 63, 78, 180), 3);
        for (uint32_t p = texture.firstUse; p <= texture.lastUse; ++p) for (const auto& use : graph.passes[p].uses) if (use.texture == i) {
            const auto min = Add(pos, {labelWidth + float(p) * cell, 2}); const auto max = Add(min, {cell - 3, row - 5});
            const bool read = Graph::Reads(use.access), write = Graph::Writes(use.access);
            draw->AddRectFilled(min, max, read && write ? rwColor : write ? rtColor : passColor, 3);
            draw->AddText(Add(min, {4, 2}), IM_COL32(15, 20, 28, 255), read && write ? "RW" : write ? "W" : "R");
            if (hover && Inside(mouse, min, max)) {
                ImGui::SetTooltip("#%u %s\n%s: %s", p, graph.passes[p].name.c_str(), texture.name.c_str(), Graph::AccessName(use.access));
                if (ImGui::IsMouseClicked(0)) SelectPass(graph, int(p));
            }
        }
    }
    ImGui::Dummy({labelWidth + float(graph.passes.size()) * cell, float(r) * row}); ImGui::EndChild();
}
void DrawResources(const Graph& graph) {
    if (!ImGui::BeginTable("render-targets", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
        {0, std::max(180.f, ImGui::GetContentRegionAvail().y - 175.f)})) return;
    for (const char* column : {"Render target", "Format", "Size", "Samples", "MiB*", "First / last Pass"}) ImGui::TableSetupColumn(column);
    ImGui::TableHeadersRow();
    for (uint32_t i = 0; i < graph.textures.size(); ++i) {
        const auto& texture = graph.textures[i]; const auto& desc = texture.desc;
        if ((!state.unused && texture.firstUse == Graph::Invalid) || !ResourceMatches(texture)) continue;
        ImGui::PushID(int(i)); ImGui::TableNextRow(); ImGui::TableNextColumn();
        if (ImGui::Selectable(texture.name.c_str(), state.texture == int(i), ImGuiSelectableFlags_SpanAllColumns)) SelectTexture(graph, int(i));
        ImGui::TableNextColumn(); ImGui::TextUnformatted(desc.format.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%u x %u", desc.width, desc.height);
        ImGui::TableNextColumn(); ImGui::Text("%ux", desc.samples);
        ImGui::TableNextColumn(); ImGui::Text("%.2f", double(desc.EstimatedBytes()) / 1048576.0);
        ImGui::TableNextColumn();
        if (texture.firstUse == Graph::Invalid) ImGui::TextDisabled("Unused");
        else ImGui::Text("#%u - #%u", texture.firstUse, texture.lastUse);
        ImGui::PopID();
    }
    ImGui::EndTable();
}
} // namespace

void EngineUi::ResetFrameGraphPanel() { state = PanelState{}; }
void EngineUi::DrawFrameGraph(EditorState& editor, const FrameGraph& graph) {
    if (!editor.showFrameGraph) return;
    ImGui::SetNextWindowSize({1100, 720}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Frame Graph###FrameGraph", &editor.showFrameGraph)) { ImGui::End(); return; }
    if (state.pass >= int(graph.passes.size()) || (state.pass >= 0 && graph.passes[state.pass].name != state.selectionName)) state.pass = -1;
    if (state.texture >= int(graph.textures.size()) || (state.texture >= 0 && graph.textures[state.texture].name != state.selectionName)) state.texture = -1;
    ImGui::Checkbox("Freeze capture", &editor.freezeFrameGraph); ImGui::SameLine();
    if (ImGui::Button("Fit graph")) state.fit = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(state.pass < 0 && state.texture < 0);
    if (ImGui::Button("Focus selection")) state.focus = true;
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::SliderFloat("Zoom", &state.zoom, 0.06f, 1.8f, "%.2f");
    ImGui::Checkbox("Ordering edges", &state.ordering);
    ImGui::SameLine(); ImGui::Checkbox("Unused RTs", &state.unused);
    state.filter.Draw("Search Pass / RT", 240);
    ImGui::TextWrapped("%s | frame %llu | %zu passes | %zu RT versions | %.2f MiB used RT estimate*",
        graph.view.c_str(), static_cast<unsigned long long>(graph.frame), graph.passes.size(), graph.versions.size(), double(graph.EstimatedBytes()) / 1048576.0);
    Legend();
    if (graph.passes.empty()) ImGui::TextDisabled("Waiting for a recorded frame. Keep a viewport visible.");
    else if (ImGui::BeginTabBar("frame-graph-tabs")) {
        if (ImGui::BeginTabItem("Graph")) { DrawNodes(graph); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("RT lifetime")) { DrawLifetime(graph); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Render targets")) { DrawResources(graph); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
        Details(graph);
    }
    if (!graph.warnings.empty()) {
        ImGui::TextColored({1, 0.5f, 0.3f, 1}, "%zu graph validation warnings", graph.warnings.size());
        if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); for (const auto& warning : graph.warnings) ImGui::TextUnformatted(warning.c_str()); ImGui::EndTooltip(); }
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("*Logical image bytes, excluding swapchain, padding and allocator overhead. Capture of the last recorded graphics frame.");
    ImGui::PopTextWrapPos();
    ImGui::End();
}
} // namespace engine
