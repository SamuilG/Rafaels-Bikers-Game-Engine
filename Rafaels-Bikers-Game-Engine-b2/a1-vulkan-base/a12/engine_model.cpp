// -----------------------------------------------------------------------------
// The following glTF loading logic is partly based on Syoyo Fujita's tinygltf examples.
// See: https://github.com/syoyo/tinygltf/tree/release/examples
// -----------------------------------------------------------------------------

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"
#include "engine_model.hpp"
#include <stdexcept>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// Get the memory address of the data buffer
// Address = Buffer Start + BufferView Offset + Accessor Offset
static const uint8_t* accessorPtr(const tinygltf::Model& m,
    const tinygltf::Accessor& acc)
{
    auto& bv = m.bufferViews[acc.bufferView];
    auto& buf = m.buffers[bv.buffer];
    return buf.data.data() + bv.byteOffset + acc.byteOffset;
}

// Get the byte stride (step size between elements)
static size_t accessorStride(const tinygltf::Model& m,
    const tinygltf::Accessor& acc)
{
    auto& bv = m.bufferViews[acc.bufferView];

    // Use the stride defined in the file, or calculate tightly packed size
    if (bv.byteStride > 0) return bv.byteStride;
    return tinygltf::GetComponentSizeInBytes(acc.componentType)
        * tinygltf::GetNumComponentsInType(acc.type);
}

// Texture Loading 
static std::vector<EngineTexture> loadTextures(const tinygltf::Model& gltf)
{
    std::vector<EngineTexture> out;
    out.reserve(gltf.images.size());

    for (auto& img : gltf.images) {
        EngineTexture tex;
        tex.name = img.name;
        tex.width = img.width;
        tex.height = img.height;
        tex.space = ETextureSpace::unorm; // default

        int pixelCount = img.width * img.height;

        if (img.component == 4) {
            //Already RGBA
            tex.pixels.assign(img.image.begin(), img.image.end());
        }
        else if (img.component == 3) {
            //Transfer RGB to  RGBA
            tex.pixels.resize(pixelCount * 4);
            for (int p = 0; p < pixelCount; ++p) {
                tex.pixels[p * 4 + 0] = img.image[p * 3 + 0];
                tex.pixels[p * 4 + 1] = img.image[p * 3 + 1];
                tex.pixels[p * 4 + 2] = img.image[p * 3 + 2];
                tex.pixels[p * 4 + 3] = 255;
            }
        }
        else if (img.component == 1) {
            // Convert Grayscale (1 channel) to RGBA
            // Duplicate the gray value to R, G, B and set Alpha to 255 (Opaque)
            tex.pixels.resize(pixelCount * 4);
            for (int p = 0; p < pixelCount; ++p) {
                tex.pixels[p * 4 + 0] = img.image[p];
                tex.pixels[p * 4 + 1] = img.image[p];
                tex.pixels[p * 4 + 2] = img.image[p];
                tex.pixels[p * 4 + 3] = 255;
            }
        }

        out.push_back(std::move(tex));
    }
    return out;
}

// gltf.texture.index ¡ú image index
static int texIndex(const tinygltf::Model& gltf, int texIdx) {
    if (texIdx < 0) return -1;
    return gltf.textures[texIdx].source; // image index
}

// ---------- Material Parsing ----------
static std::vector<EngineMaterial> loadMaterials(
    const tinygltf::Model& gltf,
    std::vector<EngineTexture>& textures) // sRGB
{
    std::vector<EngineMaterial> out;
    out.reserve(gltf.materials.size());

    for (auto& mat : gltf.materials) {
        EngineMaterial m{};
        auto& pbr = mat.pbrMetallicRoughness;

        // Base color£¨sRGB£©
        int bcIdx = texIndex(gltf, pbr.baseColorTexture.index);
        m.baseColorTexture = bcIdx;
        if (bcIdx >= 0)
            textures[bcIdx].space = ETextureSpace::srgb;

        if (pbr.baseColorFactor.size() == 4) {
            m.baseColorFactor = glm::vec4(
                pbr.baseColorFactor[0], pbr.baseColorFactor[1],
                pbr.baseColorFactor[2], pbr.baseColorFactor[3]);
        }

        // MetalRoughness£¨UNORM£¬G=rough B=metal£¬shared£©
        m.metalRoughTexture = texIndex(gltf, pbr.metallicRoughnessTexture.index);
        m.metallicFactor = static_cast<float>(pbr.metallicFactor);
        m.roughnessFactor = static_cast<float>(pbr.roughnessFactor);

        // Normal£¨UNORM£©
        m.normalTexture = texIndex(gltf, mat.normalTexture.index);

        // Occlusion£¨UNORM£©
        m.occlusionTexture = texIndex(gltf, mat.occlusionTexture.index);

        // Emissive£¨sRGB£©
        int emIdx = texIndex(gltf, mat.emissiveTexture.index);
        m.emissiveTexture = emIdx;
        if (emIdx >= 0)
            textures[emIdx].space = ETextureSpace::srgb;

        if (mat.emissiveFactor.size() == 3) {
            m.emissiveFactor = glm::vec3(
                mat.emissiveFactor[0],
                mat.emissiveFactor[1],
                mat.emissiveFactor[2]);
        }

        // Alpha
        if (mat.alphaMode == "MASK") {
            m.alphaMaskTexture = m.baseColorTexture; // alpha in baseColor.a
            m.alphaCutoff = static_cast<float>(mat.alphaCutoff);
        }
        else if (mat.alphaMode == "BLEND") {
            m.alphaBlend = true;
        }

        out.push_back(m);
    }
    return out;
}


static std::vector<EngineMesh> loadMeshes(
    const tinygltf::Model& gltf,
    std::vector<std::vector<uint32_t>>& meshMap)
{
    std::vector<EngineMesh> out;
    meshMap.resize(gltf.meshes.size()); 

    for (size_t i = 0; i < gltf.meshes.size(); ++i) {
        auto& gltfMesh = gltf.meshes[i];

        for (auto& prim : gltfMesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

            EngineMesh mesh;

            mesh.materialIndex = prim.material >= 0
                ? static_cast<uint32_t>(prim.material) : 0;

            // POSITION
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue; // skip useless primitive

            {
                auto& acc = gltf.accessors[posIt->second];
                auto* data = accessorPtr(gltf, acc);
                size_t stride = accessorStride(gltf, acc);
                mesh.positions.resize(acc.count);
                for (size_t k = 0; k < acc.count; ++k)
                    mesh.positions[k] = *reinterpret_cast<const glm::vec3*>(data + k * stride);
            }

            // NORMAL 
            auto normIt = prim.attributes.find("NORMAL");
            if (normIt != prim.attributes.end()) {
                auto& acc = gltf.accessors[normIt->second];
                auto* data = accessorPtr(gltf, acc);
                size_t stride = accessorStride(gltf, acc);
                mesh.normals.resize(acc.count);
                for (size_t k = 0; k < acc.count; ++k)
                    mesh.normals[k] = *reinterpret_cast<const glm::vec3*>(data + k * stride);
            }
            else {
                mesh.normals.assign(mesh.positions.size(), glm::vec3(0.f, 1.f, 0.f));
            }

            // TEXCOORD_0 
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end()) {
                auto& acc = gltf.accessors[uvIt->second];
                auto* data = accessorPtr(gltf, acc);
                size_t stride = accessorStride(gltf, acc);
                mesh.texcoords.resize(acc.count);
                for (size_t k = 0; k < acc.count; ++k)
                    mesh.texcoords[k] = *reinterpret_cast<const glm::vec2*>(data + k * stride);
            }
            else {
                mesh.texcoords.assign(mesh.positions.size(), glm::vec2(0.f));
            }

            // INDICES
            if (prim.indices >= 0) {
                auto& acc = gltf.accessors[prim.indices];
                auto* data = accessorPtr(gltf, acc);
                mesh.indices.reserve(acc.count);
                for (size_t k = 0; k < acc.count; ++k) {
                    uint32_t idx = 0;
                    switch (acc.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                        idx = reinterpret_cast<const uint32_t*>(data)[k]; break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        idx = reinterpret_cast<const uint16_t*>(data)[k]; break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        idx = reinterpret_cast<const uint8_t*>(data)[k];  break;
                    }
                    mesh.indices.push_back(idx);
                }
            }

			// record the current EngineMesh index for this glTF mesh
			// out.size() is the index of the EngineMesh we are about to add
            meshMap[i].push_back(static_cast<uint32_t>(out.size()));

            out.push_back(std::move(mesh));
        }
    }
    return out;
}


static glm::mat4 getNodeTransform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        return glm::make_mat4(node.matrix.data());
    }
    else {
      
        glm::mat4 translation = glm::mat4(1.0f);
        glm::mat4 rotation = glm::mat4(1.0f);
        glm::mat4 scale = glm::mat4(1.0f);

        if (node.translation.size() == 3) {
            translation = glm::translate(glm::mat4(1.0f),
                glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
        }
        if (node.rotation.size() == 4) {
            glm::quat q = glm::make_quat(node.rotation.data());
            rotation = glm::mat4(q);
        }
        if (node.scale.size() == 3) {
            scale = glm::scale(glm::mat4(1.0f),
                glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
        }
        return translation * rotation * scale;
    }
}

static void processNode(
    const tinygltf::Model& gltf,
    int nodeIdx,
    const glm::mat4& parentMatrix,
    const std::vector<std::vector<uint32_t>>& meshMap,
    std::vector<EngineInstance>& outInstances)
{
    if (nodeIdx < 0 || nodeIdx >= gltf.nodes.size()) return;
    const tinygltf::Node& node = gltf.nodes[nodeIdx];

	// 1. calculate the global transform matrix for the current node

    glm::mat4 localMatrix = getNodeTransform(node);
    glm::mat4 globalMatrix = parentMatrix * localMatrix;

    // 2. Create Instances if the node has a mesh
    if (node.mesh >= 0 && node.mesh < meshMap.size()) {
        const auto& engineMeshIndices = meshMap[node.mesh];
        // Create an instance for every primitive in the mesh
        for (uint32_t meshIndex : engineMeshIndices) {
            EngineInstance instance;
            instance.meshIndex = meshIndex; // Reference to geometry
            instance.transform = globalMatrix;  // World position
            outInstances.push_back(instance);
        }
    }

    // 3. Process children recursively
    for (int childIdx : node.children) {
        processNode(gltf, childIdx, globalMatrix, meshMap, outInstances);
    }
}


EngineModel load_engine_model_glb(const char* path)
{
    tinygltf::Model    gltf;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    if (!loader.LoadBinaryFromFile(&gltf, &err, &warn, path))
        throw std::runtime_error(std::string("tinygltf: ") + err);
    if (!warn.empty())
        fprintf(stderr, "[tinygltf warn] %s\n", warn.c_str());

    EngineModel model;

    // 1. Parse Resources (Textures, Materials, Meshes)
    model.textures = loadTextures(gltf);
    model.materials = loadMaterials(gltf, model.textures);


    // Load Meshes and build the ID mapping table
    std::vector<std::vector<uint32_t>> meshMap;
    model.meshes = loadMeshes(gltf, meshMap);

    // 2. Build Scene Graph (Nodes -> Instances)
    if (gltf.scenes.size() > 0) {
        int sceneIdx = gltf.defaultScene > -1 ? gltf.defaultScene : 0;
        const tinygltf::Scene& scene = gltf.scenes[sceneIdx];

        for (int nodeIdx : scene.nodes) {
            // Process all root nodes
            processNode(gltf, nodeIdx, glm::mat4(1.0f), meshMap, model.scenes);
        }
    }
    else {
        // Fallback: If no scene exists, just list all meshes at identity
        for (size_t i = 0; i < model.meshes.size(); ++i) {
            EngineInstance instance;
            instance.meshIndex = static_cast<uint32_t>(i);
            instance.transform = glm::mat4(1.0f);
            model.scenes.push_back(instance);
        }
    }

    return model;
}