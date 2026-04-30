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

// gltf.texture.index �� image index
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

        // Base color��sRGB��
        int bcIdx = texIndex(gltf, pbr.baseColorTexture.index);
        m.baseColorTexture = bcIdx;
        if (bcIdx >= 0)
            textures[bcIdx].space = ETextureSpace::srgb;
        // ==========================================
        // --- ���ؼ��޸� 1������ȡ������ɫϵ�� ---
        // ==========================================
        if (pbr.baseColorFactor.size() == 4) {
            m.baseColorFactor = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3])
            );
        }
        else {
            m.baseColorFactor = glm::vec4(1.0f); // Ĭ�ϰ�ɫ/����Ӱ��
        }

        // MetalRoughness��UNORM��G=rough B=metal��shared��
        m.metalRoughTexture = texIndex(gltf, pbr.metallicRoughnessTexture.index);


        //if (pbr.baseColorFactor.size() == 4) {
        //    m.baseColorFactor = glm::vec4(
        //        pbr.baseColorFactor[0], pbr.baseColorFactor[1],
        //        pbr.baseColorFactor[2], pbr.baseColorFactor[3]);
        //}

        // MetalRoughness��UNORM��G=rough B=metal��shared��
        //m.metalRoughTexture = texIndex(gltf, pbr.metallicRoughnessTexture.index);
        //m.metallicFactor = static_cast<float>(pbr.metallicFactor);
        //m.roughnessFactor = static_cast<float>(pbr.roughnessFactor);



        // ==========================================
        // --- ���ؼ��޸� 2������ȡ��������ֲڶ�ϵ�� ---
        // ==========================================
        // tinygltf ��û������ʱͨ�����Ĭ��ֵ 1.0
        m.metallicFactor = static_cast<float>(pbr.metallicFactor);
        m.roughnessFactor = static_cast<float>(pbr.roughnessFactor);

        // Normal��UNORM��
        m.normalTexture = texIndex(gltf, mat.normalTexture.index);

        // Occlusion��UNORM��
        m.occlusionTexture = texIndex(gltf, mat.occlusionTexture.index);

        // Emissive��sRGB��
        int emIdx = texIndex(gltf, mat.emissiveTexture.index);
        m.emissiveTexture = emIdx;
        if (emIdx >= 0)
            textures[emIdx].space = ETextureSpace::srgb;

        if (mat.emissiveFactor.size() == 3) {
            m.emissiveFactor = glm::vec3(
                static_cast<float>(mat.emissiveFactor[0]),
                static_cast<float>(mat.emissiveFactor[1]),
                static_cast<float>(mat.emissiveFactor[2])
            );
        }

        // Alpha
        if (mat.alphaMode == "MASK") {
            m.alphaMaskTexture = m.baseColorTexture; // alpha in baseColor.a
            m.alphaCutoff = static_cast<float>(mat.alphaCutoff);
        }
        else if (mat.alphaMode == "BLEND") {
            m.alphaBlend = true;
        }
        printf("[Material] BaseColor: %d, Normal: %d, Roughness: %d, AO: %d\n",
            m.baseColorTexture, m.normalTexture, m.metalRoughTexture, m.occlusionTexture);
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
            if (posIt == prim.attributes.end()) continue;

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

            // JOINTS_0 (bone indices per vertex, up to 4)
            auto jointsIt = prim.attributes.find("JOINTS_0");
            if (jointsIt != prim.attributes.end()) {
                auto& acc = gltf.accessors[jointsIt->second];
                auto* data = accessorPtr(gltf, acc);
                size_t stride = accessorStride(gltf, acc);
                mesh.jointIndices.resize(acc.count, glm::uvec4(0));
                for (size_t k = 0; k < acc.count; ++k) {
                    const uint8_t* src = data + k * stride;
                    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        mesh.jointIndices[k] = glm::uvec4(src[0], src[1], src[2], src[3]);
                    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        auto* s = reinterpret_cast<const uint16_t*>(src);
                        mesh.jointIndices[k] = glm::uvec4(s[0], s[1], s[2], s[3]);
                    }
                }
            }

            // WEIGHTS_0 (influence weights per vertex, sum to 1.0)
            auto weightsIt = prim.attributes.find("WEIGHTS_0");
            if (weightsIt != prim.attributes.end()) {
                auto& acc = gltf.accessors[weightsIt->second];
                auto* data = accessorPtr(gltf, acc);
                size_t stride = accessorStride(gltf, acc);
                mesh.jointWeights.resize(acc.count, glm::vec4(0.0f));
                for (size_t k = 0; k < acc.count; ++k) {
                    const uint8_t* src = data + k * stride;
                    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                        mesh.jointWeights[k] = *reinterpret_cast<const glm::vec4*>(src);
                    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        mesh.jointWeights[k] = glm::vec4(src[0], src[1], src[2], src[3]) / 255.0f;
                    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        auto* s = reinterpret_cast<const uint16_t*>(src);
                        mesh.jointWeights[k] = glm::vec4(s[0], s[1], s[2], s[3]) / 65535.0f;
                    }
                }
            }

            if (!mesh.jointIndices.empty() && !mesh.jointWeights.empty())
                mesh.isSkinned = true;

            // Frustum culling AABB
            if (!mesh.positions.empty()) {
                mesh.localAabbMin = mesh.positions[0];
                mesh.localAabbMax = mesh.positions[0];
                for (const glm::vec3& position : mesh.positions) {
                    mesh.localAabbMin = glm::min(mesh.localAabbMin, position);
                    mesh.localAabbMax = glm::max(mesh.localAabbMax, position);
                }
            }

            meshMap[i].push_back(static_cast<uint32_t>(out.size()));
            out.push_back(std::move(mesh));
        }
    }
    return out;
}

// Forward declaration (defined later in this file)
static glm::mat4 getNodeTransform(const tinygltf::Node& node);

// Build the flat node array with parent indices and default local transforms
static std::vector<EngineNode> loadNodes(const tinygltf::Model& gltf)
{
    std::vector<EngineNode> nodes(gltf.nodes.size());

    // First pass: set default local transforms
    for (size_t i = 0; i < gltf.nodes.size(); ++i) {
        nodes[i].name = gltf.nodes[i].name;
        nodes[i].localTransform = getNodeTransform(gltf.nodes[i]);
        nodes[i].parentIndex = -1;
    }

    // Second pass: assign parent indices from children lists
    for (size_t i = 0; i < gltf.nodes.size(); ++i) {
        for (int child : gltf.nodes[i].children) {
            if (child >= 0 && child < (int)nodes.size())
                nodes[child].parentIndex = (int)i;
        }
    }

    return nodes;
}

// Load skin data (joint lists and inverse bind matrices)
static std::vector<EngineSkin> loadSkins(const tinygltf::Model& gltf)
{
    std::vector<EngineSkin> skins;
    skins.reserve(gltf.skins.size());

    for (auto& gltfSkin : gltf.skins) {
        EngineSkin skin;
        skin.skeletonRoot = gltfSkin.skeleton;
        skin.joints = gltfSkin.joints;

        // Load inverse bind matrices
        if (gltfSkin.inverseBindMatrices >= 0) {
            auto& acc = gltf.accessors[gltfSkin.inverseBindMatrices];
            auto* data = accessorPtr(gltf, acc);
            size_t stride = accessorStride(gltf, acc);
            skin.inverseBindMatrices.resize(acc.count, glm::mat4(1.0f));
            for (size_t k = 0; k < acc.count; ++k)
                skin.inverseBindMatrices[k] = *reinterpret_cast<const glm::mat4*>(data + k * stride);
        } else {
            // Default: identity matrices for each joint
            skin.inverseBindMatrices.resize(skin.joints.size(), glm::mat4(1.0f));
        }

        skins.push_back(std::move(skin));
    }
    return skins;
}

// Load animation clips
static std::vector<EngineAnimation> loadAnimations(const tinygltf::Model& gltf)
{
    std::vector<EngineAnimation> animations;
    animations.reserve(gltf.animations.size());

    for (auto& gltfAnim : gltf.animations) {
        EngineAnimation anim;
        anim.name = gltfAnim.name;

        // Load samplers
        for (auto& gltfSampler : gltfAnim.samplers) {
            EngineAnimSampler sampler;

            // Interpolation mode
            if (gltfSampler.interpolation == "STEP")
                sampler.interp = 1;
            else
                sampler.interp = 0; // LINEAR (default)

            // Input: timestamps
            if (gltfSampler.input >= 0) {
                auto& acc = gltf.accessors[gltfSampler.input];
                auto* data = accessorPtr(gltf, acc);
                size_t stride = accessorStride(gltf, acc);
                sampler.times.resize(acc.count);
                for (size_t k = 0; k < acc.count; ++k)
                    sampler.times[k] = *reinterpret_cast<const float*>(data + k * stride);
                if (!sampler.times.empty())
                    anim.duration = glm::max(anim.duration, sampler.times.back());
            }

            // Output: TRS values
            if (gltfSampler.output >= 0) {
                auto& acc = gltf.accessors[gltfSampler.output];
                auto* data = accessorPtr(gltf, acc);
                size_t stride = accessorStride(gltf, acc);
                sampler.values.resize(acc.count, glm::vec4(0.0f));
                for (size_t k = 0; k < acc.count; ++k) {
                    const float* src = reinterpret_cast<const float*>(data + k * stride);
                    if (acc.type == TINYGLTF_TYPE_VEC3) {
                        sampler.values[k] = glm::vec4(src[0], src[1], src[2], 0.0f);
                    } else if (acc.type == TINYGLTF_TYPE_VEC4) {
                        sampler.values[k] = glm::vec4(src[0], src[1], src[2], src[3]);
                    }
                }
            }

            anim.samplers.push_back(std::move(sampler));
        }

        // Load channels
        for (auto& gltfChan : gltfAnim.channels) {
            EngineAnimChannel chan;
            chan.samplerIndex = gltfChan.sampler;
            chan.nodeIndex    = gltfChan.target_node;
            const std::string& path = gltfChan.target_path;
            if      (path == "translation") chan.path = 0;
            else if (path == "rotation")    chan.path = 1;
            else if (path == "scale")       chan.path = 2;
            else continue; // skip unknown paths (e.g., "weights" for morph targets)
            anim.channels.push_back(chan);
        }

        animations.push_back(std::move(anim));
    }
    return animations;
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
    std::vector<EngineInstance>& outInstances,
    std::map<std::string, glm::mat4>& namedTransforms)
{
    if (nodeIdx < 0 || nodeIdx >= (int)gltf.nodes.size()) return;
    const tinygltf::Node& node = gltf.nodes[nodeIdx];

    glm::mat4 localMatrix  = getNodeTransform(node);
    glm::mat4 globalMatrix = parentMatrix * localMatrix;

    // Record transform for every named node (including empty/anchor nodes with no mesh)
    if (!node.name.empty())
        namedTransforms[node.name] = globalMatrix;

    if (node.mesh >= 0 && node.mesh < (int)meshMap.size()) {
        const auto& engineMeshIndices = meshMap[node.mesh];
        for (uint32_t meshIndex : engineMeshIndices) {
            EngineInstance instance;
            instance.meshIndex  = meshIndex;
            instance.transform  = globalMatrix;
            instance.name       = node.name.empty() ? ("Node_" + std::to_string(nodeIdx)) : node.name;
            instance.skinIndex  = node.skin;   // -1 if no skin
            instance.nodeIndex  = nodeIdx;
            outInstances.push_back(instance);
        }
    }

    for (int childIdx : node.children) {
        processNode(gltf, childIdx, globalMatrix, meshMap, outInstances, namedTransforms);
    }
}



EngineModel load_engine_model_glb(const char* path)
{
    tinygltf::Model    gltf;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    // --- �ؼ��޸ģ������ļ���׺��ѡ����ط�ʽ ---
    std::string filePath(path);
    bool ret = false;

    
    if (filePath.length() >= 5 && filePath.substr(filePath.length() - 5) == ".gltf") {
        ret = loader.LoadASCIIFromFile(&gltf, &err, &warn, path);
    }
    
    else {
        ret = loader.LoadBinaryFromFile(&gltf, &err, &warn, path);
    }

    if (!ret)
        throw std::runtime_error(std::string("tinygltf failed to load ") + path + ": " + err);

    if (!warn.empty())
        fprintf(stderr, "[tinygltf warn] %s\n", warn.c_str());
    EngineModel model;

    // 1. Parse textures, materials, meshes
    model.textures  = loadTextures(gltf);
    model.materials = loadMaterials(gltf, model.textures);

    std::vector<std::vector<uint32_t>> meshMap;
    model.meshes = loadMeshes(gltf, meshMap);

    // 2. Node hierarchy (needed for animation joint-matrix computation)
    model.nodes = loadNodes(gltf);

    // 3. Skins and animations
    model.skins      = loadSkins(gltf);
    model.animations = loadAnimations(gltf);

    // 4. Build scene instances from node graph
    if (!gltf.scenes.empty()) {
        int sceneIdx = gltf.defaultScene > -1 ? gltf.defaultScene : 0;
        for (int nodeIdx : gltf.scenes[sceneIdx].nodes)
            processNode(gltf, nodeIdx, glm::mat4(1.0f), meshMap, model.scenes, model.namedTransforms);
    }
    else {
        for (size_t i = 0; i < model.meshes.size(); ++i) {
            EngineInstance inst;
            inst.meshIndex = static_cast<uint32_t>(i);
            inst.transform = glm::mat4(1.0f);
            inst.name      = "Fallback_Mesh_" + std::to_string(i);
            model.scenes.push_back(inst);
        }
    }

    return model;
}