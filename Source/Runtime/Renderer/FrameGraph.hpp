#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine {

// A value-only capture of the recorded graphics queue. Compile derives resource
// versions, dependencies and use intervals; snapshots never own GPU objects.
class FrameGraph {
public:
    using Id = uint32_t;
    static constexpr Id Invalid = std::numeric_limits<Id>::max();
    enum class Access { Sample, Color, Depth, DepthRead, Resolve, LoadColor, CopySource, CopyDestination, Present };
    enum class Hazard { ReadAfterWrite, WriteAfterRead, WriteAfterWrite };
    struct TextureDesc {
        uint32_t width = 0, height = 0, samples = 1, layers = 1, bytesPerTexel = 0;
        std::string format;
        bool importedContents = false, external = false;
        uint64_t EstimatedBytes() const { return uint64_t(width) * height * samples * layers * bytesPerTexel; }
    };
    struct Texture {
        uint64_t image = 0;
        uint32_t layer = 0;
        std::string name;
        TextureDesc desc;
        Id firstUse = Invalid, lastUse = Invalid;
    };
    struct Use {
        Id texture;
        Access access;
        Id input = Invalid, output = Invalid;
    };
    struct Pass { std::string name, category; std::vector<Use> uses; uint32_t level = 0; };
    struct Version {
        Id texture, number, producer = Invalid;
        std::vector<Id> readers;
        bool initialized = true;
    };
    struct Dependency { Id before, after, texture; Hazard hazard; };

    std::string view;
    uint64_t frame = 0;
    std::vector<Texture> textures;
    std::vector<Pass> passes;
    std::vector<Version> versions;
    std::vector<Dependency> dependencies;
    std::vector<std::string> warnings;

    static bool Reads(Access access) {
        return access == Access::Sample || access == Access::DepthRead || access == Access::LoadColor ||
            access == Access::CopySource || access == Access::Present;
    }
    static bool Writes(Access access) { return !Reads(access) || access == Access::LoadColor; }
    static const char* AccessName(Access access) {
        switch (access) {
        case Access::Sample: return "Sample";
        case Access::Color: return "Color write";
        case Access::Depth: return "Depth write";
        case Access::DepthRead: return "Depth read";
        case Access::Resolve: return "MSAA resolve write";
        case Access::LoadColor: return "Color load + write";
        case Access::CopySource: return "Copy source";
        case Access::CopyDestination: return "Copy destination";
        case Access::Present: return "Present";
        }
        return "Unknown";
    }
    void Begin(std::string name, uint64_t serial) {
        view = std::move(name); frame = serial;
        textures.clear(); passes.clear(); versions.clear(); dependencies.clear(); warnings.clear();
    }
    Id FindTexture(uint64_t image, uint32_t layer = 0) const {
        for (Id i = 0; i < textures.size(); ++i)
            if (textures[i].image == image && textures[i].layer == layer) return i;
        return Invalid;
    }
    Id RegisterTexture(uint64_t image, std::string name, TextureDesc desc, uint32_t layer = 0) {
        if (!image) return Invalid;
        const auto existing = FindTexture(image, layer);
        if (existing != Invalid) return existing;
        textures.push_back({image, layer, std::move(name), std::move(desc)});
        return Id(textures.size() - 1);
    }
    Id AddPass(std::string name, std::string category = "Graphics") {
        passes.push_back({std::move(name), std::move(category), {}});
        return Id(passes.size() - 1);
    }
    void UseTexture(Id pass, Id texture, Access access) {
        if (pass >= passes.size() || texture >= textures.size()) throw std::out_of_range("Invalid frame graph use");
        auto& uses = passes[pass].uses;
        for (auto& use : uses) if (use.texture == texture) {
            // The capture expresses attachment load and store as one use.
            if ((Reads(use.access) || Reads(access)) && (Writes(use.access) || Writes(access)))
                use.access = Access::LoadColor;
            return;
        }
        uses.push_back({texture, access});
    }
    void Compile() {
        versions.clear(); dependencies.clear(); warnings.clear();
        struct State { Id version = Invalid, writer = Invalid; std::vector<Id> readers; uint32_t number = 0; };
        std::vector<State> states(textures.size());
        for (auto& texture : textures) texture.firstUse = texture.lastUse = Invalid;
        auto edge = [&](Id before, Id after, Id texture, Hazard hazard) {
            if (before != Invalid && before != after) dependencies.push_back({before, after, texture, hazard});
        };
        for (Id p = 0; p < passes.size(); ++p) {
            auto& pass = passes[p]; pass.level = 1;
            for (auto& use : pass.uses) {
                use.input = use.output = Invalid;
                auto& texture = textures.at(use.texture);
                auto& state = states[use.texture];
                if (texture.firstUse == Invalid) texture.firstUse = p;
                texture.lastUse = p;
                if (Reads(use.access)) {
                    if (state.version == Invalid) {
                        state.version = Id(versions.size());
                        versions.push_back({use.texture, 0, Invalid, {}, texture.desc.importedContents});
                        if (!texture.desc.importedContents)
                            warnings.push_back(pass.name + " reads unwritten RT: " + texture.name);
                    }
                    use.input = state.version;
                    versions[state.version].readers.push_back(p);
                    edge(state.writer, p, use.texture, Hazard::ReadAfterWrite);
                    state.readers.push_back(p);
                }
                if (Writes(use.access)) {
                    edge(state.writer, p, use.texture, Hazard::WriteAfterWrite);
                    for (const auto reader : state.readers) edge(reader, p, use.texture, Hazard::WriteAfterRead);
                    state.readers.clear(); state.writer = p;
                    use.output = state.version = Id(versions.size());
                    versions.push_back({use.texture, ++state.number, p, {}});
                }
            }
            for (const auto& dep : dependencies) if (dep.after == p)
                pass.level = std::max(pass.level, passes[dep.before].level + 2);
        }
    }
    uint64_t EstimatedBytes() const {
        uint64_t bytes = 0;
        for (const auto& texture : textures)
            if (texture.firstUse != Invalid && !texture.desc.external) bytes += texture.desc.EstimatedBytes();
        return bytes;
    }
};
} // namespace engine
