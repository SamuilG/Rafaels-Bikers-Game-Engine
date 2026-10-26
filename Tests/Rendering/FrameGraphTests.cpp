#include "Runtime/Renderer/FrameGraph.hpp"
#include <iostream>

using Graph = engine::FrameGraph;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() try {
    Graph graph; graph.Begin("CPU fixture", 1);
    const Graph::TextureDesc desc{100, 50, 4, 1, 8, "RGBA16F"};
    const auto color = graph.RegisterTexture(1, "Color", desc);
    Require(graph.RegisterTexture(1, "Alias", desc) == color, "Physical RT alias was counted twice");
    const auto depth0 = graph.RegisterTexture(2, "Depth layer 0", desc, 0);
    const auto depth1 = graph.RegisterTexture(2, "Depth layer 1", desc, 1);
    Require(depth0 != depth1, "Independent array layers were collapsed");
    auto p = graph.AddPass("Forward"); graph.UseTexture(p, color, Graph::Access::Color);
    p = graph.AddPass("SSR"); graph.UseTexture(p, color, Graph::Access::Sample);
    p = graph.AddPass("Composite"); graph.UseTexture(p, color, Graph::Access::LoadColor);
    p = graph.AddPass("Output"); graph.UseTexture(p, color, Graph::Access::CopySource);
    graph.Compile();
    Require(graph.warnings.empty(), "Valid pipeline has warnings");
    Require(graph.versions.size() == 2, "Attachment load/store did not create exactly two content versions");
    Require(graph.passes[2].uses[0].input == 0 && graph.passes[2].uses[0].output == 1,
        "Read/write pass lost the old or new RT version");
    Require(graph.versions[0].readers == std::vector<uint32_t>({1, 2}) && graph.versions[1].readers == std::vector<uint32_t>({3}),
        "Consumers were connected to overwritten RT contents");
    bool raw = false, war = false, waw = false;
    for (const auto& dep : graph.dependencies) {
        Require(dep.before < dep.after, "Dependency has a self edge or cycle");
        raw |= dep.hazard == Graph::Hazard::ReadAfterWrite;
        war |= dep.hazard == Graph::Hazard::WriteAfterRead;
        waw |= dep.hazard == Graph::Hazard::WriteAfterWrite;
    }
    Require(raw && war && waw, "RAW/WAR/WAW ordering hazards were not retained");
    Require(graph.textures[color].firstUse == 0 && graph.textures[color].lastUse == 3, "RT use interval is incorrect");
    Require(graph.EstimatedBytes() == 100 * 50 * 4 * 8, "Estimate counted unused RTs or content versions as allocations");
    const auto deps = graph.dependencies.size(); graph.Compile();
    Require(graph.dependencies.size() == deps && graph.versions.size() == 2, "Repeated compile accumulated stale data");
    auto snapshot = graph; graph.Begin("Next frame", 2);
    Require(graph.passes.empty() && graph.textures.empty() && graph.versions.empty(), "New frame retained stale GPU identities");
    Require(snapshot.frame == 1 && snapshot.versions.size() == 2, "Snapshot was changed by the next capture");
    std::cout << "PASS physical identity, layers, versions, RAW/WAR/WAW, intervals, estimates and snapshots\n";

    auto historyDesc = desc; historyDesc.samples = 1; historyDesc.importedContents = true;
    auto swapDesc = historyDesc; swapDesc.external = true;
    const auto history = graph.RegisterTexture(3, "Previous history", historyDesc);
    const auto swap = graph.RegisterTexture(4, "Swapchain", swapDesc);
    p = graph.AddPass("TAA"); graph.UseTexture(p, history, Graph::Access::Sample); graph.UseTexture(p, swap, Graph::Access::Color);
    p = graph.AddPass("Present"); graph.UseTexture(p, swap, Graph::Access::Present); graph.Compile();
    Require(graph.warnings.empty() && graph.versions[0].producer == Graph::Invalid && graph.versions[0].initialized,
        "Imported history was treated as an unwritten transient");
    Require(graph.EstimatedBytes() == historyDesc.EstimatedBytes(), "Swapchain was counted as engine image memory");
    const auto missing = graph.RegisterTexture(5, "Missing writer", desc);
    p = graph.AddPass("Bad read"); graph.UseTexture(p, missing, Graph::Access::Sample); graph.Compile();
    Require(graph.warnings.size() == 1, "Unwritten RT read was not reported");
    bool rejected = false;
    try { graph.UseTexture(99, missing, Graph::Access::Sample); } catch (const std::out_of_range&) { rejected = true; }
    Require(rejected, "Invalid graph handle was accepted");
    std::cout << "PASS previous-frame input, presentation boundary, unwritten reads and invalid handles\n";
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
