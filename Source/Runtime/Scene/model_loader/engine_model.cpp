// -----------------------------------------------------------------------------
// The following glTF loading logic is partly based on Syoyo Fujita's tinygltf examples.
// See: https://github.com/syoyo/tinygltf/tree/release/examples
// -----------------------------------------------------------------------------

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"
#include "engine_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "../../../../ThirdParty/json/json.hpp"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

void destroyRuntimeSkeleton(void*& runtimeSkeleton)
{
    delete static_cast<ozz::animation::Skeleton*>(runtimeSkeleton);
    runtimeSkeleton = nullptr;
}

void destroyRuntimeAnimation(void*& runtimeAnimation)
{
    delete static_cast<ozz::animation::Animation*>(runtimeAnimation);
    runtimeAnimation = nullptr;
}

} // namespace

EngineSkeleton::~EngineSkeleton()
{
    destroyRuntimeSkeleton(runtimeSkeleton);
}

EngineSkeleton::EngineSkeleton(EngineSkeleton&& other) noexcept
    : name(std::move(other.name))
    , rootNodeIndex(other.rootNodeIndex)
    , jointNodeIndices(std::move(other.jointNodeIndices))
    , jointParentIndices(std::move(other.jointParentIndices))
    , bindLocalTransforms(std::move(other.bindLocalTransforms))
    , inverseBindMatrices(std::move(other.inverseBindMatrices))
    , runtimeSkeleton(other.runtimeSkeleton)
{
    other.runtimeSkeleton = nullptr;
}

EngineSkeleton& EngineSkeleton::operator=(EngineSkeleton&& other) noexcept
{
    if (this != &other) {
        destroyRuntimeSkeleton(runtimeSkeleton);
        name = std::move(other.name);
        rootNodeIndex = other.rootNodeIndex;
        jointNodeIndices = std::move(other.jointNodeIndices);
        jointParentIndices = std::move(other.jointParentIndices);
        bindLocalTransforms = std::move(other.bindLocalTransforms);
        inverseBindMatrices = std::move(other.inverseBindMatrices);
        runtimeSkeleton = other.runtimeSkeleton;
        other.runtimeSkeleton = nullptr;
    }
    return *this;
}

EngineAnimationClip::~EngineAnimationClip()
{
    destroyRuntimeAnimation(runtimeAnimation);
}

EngineAnimationClip::EngineAnimationClip(EngineAnimationClip&& other) noexcept
    : name(std::move(other.name))
    , skeletonIndex(other.skeletonIndex)
    , durationSeconds(other.durationSeconds)
    , channels(std::move(other.channels))
    , runtimeAnimation(other.runtimeAnimation)
{
    other.runtimeAnimation = nullptr;
}

EngineAnimationClip& EngineAnimationClip::operator=(EngineAnimationClip&& other) noexcept
{
    if (this != &other) {
        destroyRuntimeAnimation(runtimeAnimation);
        name = std::move(other.name);
        skeletonIndex = other.skeletonIndex;
        durationSeconds = other.durationSeconds;
        channels = std::move(other.channels);
        runtimeAnimation = other.runtimeAnimation;
        other.runtimeAnimation = nullptr;
    }
    return *this;
}

static const uint8_t* accessorPtr(const tinygltf::Model& m, const tinygltf::Accessor& acc)
{
    if (acc.bufferView < 0 || acc.bufferView >= static_cast<int>(m.bufferViews.size())) return nullptr;
    const auto& bv = m.bufferViews[acc.bufferView];
    const auto& buf = m.buffers[bv.buffer];
    return buf.data.data() + bv.byteOffset + acc.byteOffset;
}

static size_t accessorStride(const tinygltf::Model& m, const tinygltf::Accessor& acc)
{
    const auto& bv = m.bufferViews[acc.bufferView];
    if (bv.byteStride > 0) return bv.byteStride;
    return tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
}

static glm::mat4 getNodeTransform(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16) {
        return glm::make_mat4(node.matrix.data());
    }

    glm::mat4 translation = glm::mat4(1.0f);
    glm::mat4 rotation = glm::mat4(1.0f);
    glm::mat4 scale = glm::mat4(1.0f);

    if (node.translation.size() == 3) {
        translation = glm::translate(glm::mat4(1.0f), glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
    }
    if (node.rotation.size() == 4) {
        glm::quat q = glm::make_quat(node.rotation.data());
        rotation = glm::mat4(q);
    }
    if (node.scale.size() == 3) {
        scale = glm::scale(glm::mat4(1.0f), glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    }

    return translation * rotation * scale;
}

static glm::mat4 composeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
{
    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
}

static void decomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
{
    glm::vec3 skew(0.0f);
    glm::vec4 perspective(0.0f);
    glm::quat orientation;

    if (glm::decompose(transform, scale, orientation, translation, skew, perspective)) {
        rotation = glm::normalize(orientation);
        return;
    }

    translation = glm::vec3(transform[3]);
    rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    scale = glm::vec3(1.0f);
}

static std::vector<EngineTexture> loadTextures(const tinygltf::Model& gltf)
{
    std::vector<EngineTexture> out;
    out.reserve(gltf.images.size());

    for (const auto& img : gltf.images) {
        EngineTexture tex;
        tex.name = img.name;
        tex.width = img.width;
        tex.height = img.height;
        tex.space = ETextureSpace::unorm;

        int pixelCount = img.width * img.height;
        if (img.component == 4) {
            tex.pixels.assign(img.image.begin(), img.image.end());
        }
        else if (img.component == 3) {
            tex.pixels.resize(pixelCount * 4);
            for (int p = 0; p < pixelCount; ++p) {
                tex.pixels[p * 4 + 0] = img.image[p * 3 + 0];
                tex.pixels[p * 4 + 1] = img.image[p * 3 + 1];
                tex.pixels[p * 4 + 2] = img.image[p * 3 + 2];
                tex.pixels[p * 4 + 3] = 255;
            }
        }
        else if (img.component == 1) {
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

static int texIndex(const tinygltf::Model& gltf, int texIdx)
{
    if (texIdx < 0 || texIdx >= static_cast<int>(gltf.textures.size())) return -1;
    return gltf.textures[texIdx].source;
}

static std::vector<EngineMaterial> loadMaterials(const tinygltf::Model& gltf, std::vector<EngineTexture>& textures)
{
    std::vector<EngineMaterial> out;
    out.reserve(gltf.materials.size());

    for (const auto& mat : gltf.materials) {
        EngineMaterial m{};
        const auto& pbr = mat.pbrMetallicRoughness;

        int bcIdx = texIndex(gltf, pbr.baseColorTexture.index);
        m.baseColorTexture = bcIdx;
        if (bcIdx >= 0 && bcIdx < static_cast<int>(textures.size())) textures[bcIdx].space = ETextureSpace::srgb;

        if (pbr.baseColorFactor.size() == 4) {
            m.baseColorFactor = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3]));
        }

        m.metalRoughTexture = texIndex(gltf, pbr.metallicRoughnessTexture.index);
        m.metallicFactor = static_cast<float>(pbr.metallicFactor);
        m.roughnessFactor = static_cast<float>(pbr.roughnessFactor);
        m.normalTexture = texIndex(gltf, mat.normalTexture.index);
        m.occlusionTexture = texIndex(gltf, mat.occlusionTexture.index);

        int emIdx = texIndex(gltf, mat.emissiveTexture.index);
        m.emissiveTexture = emIdx;
        if (emIdx >= 0 && emIdx < static_cast<int>(textures.size())) textures[emIdx].space = ETextureSpace::srgb;

        if (mat.emissiveFactor.size() == 3) {
            m.emissiveFactor = glm::vec3(
                static_cast<float>(mat.emissiveFactor[0]),
                static_cast<float>(mat.emissiveFactor[1]),
                static_cast<float>(mat.emissiveFactor[2]));
        }

        if (mat.alphaMode == "MASK") {
            m.alphaMaskTexture = m.baseColorTexture;
            m.alphaCutoff = static_cast<float>(mat.alphaCutoff);
        }
        else if (mat.alphaMode == "BLEND") {
            m.alphaBlend = true;
        }

        out.push_back(m);
    }

    if (out.empty()) out.push_back(EngineMaterial{});
    return out;
}

static glm::uvec4 readJointVec4(const uint8_t* src, int componentType)
{
    glm::uvec4 out(0);
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const auto* v = reinterpret_cast<const uint8_t*>(src);
        out = glm::uvec4(v[0], v[1], v[2], v[3]);
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        const auto* v = reinterpret_cast<const uint16_t*>(src);
        out = glm::uvec4(v[0], v[1], v[2], v[3]);
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        const auto* v = reinterpret_cast<const uint32_t*>(src);
        out = glm::uvec4(v[0], v[1], v[2], v[3]);
        break;
    }
    default:
        break;
    }
    return out;
}

static glm::vec4 readWeightVec4(const uint8_t* src, int componentType, bool normalized)
{
    glm::vec4 out(0.0f);
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT: {
        const auto* v = reinterpret_cast<const float*>(src);
        out = glm::vec4(v[0], v[1], v[2], v[3]);
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const auto* v = reinterpret_cast<const uint8_t*>(src);
        float scale = normalized ? (1.0f / 255.0f) : 1.0f;
        out = glm::vec4(v[0], v[1], v[2], v[3]) * scale;
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        const auto* v = reinterpret_cast<const uint16_t*>(src);
        float scale = normalized ? (1.0f / 65535.0f) : 1.0f;
        out = glm::vec4(v[0], v[1], v[2], v[3]) * scale;
        break;
    }
    default:
        break;
    }

    float sum = out.x + out.y + out.z + out.w;
    if (sum > 0.0f) out /= sum;
    return out;
}

static void loadSkinAttributes(const tinygltf::Model& gltf, const tinygltf::Primitive& prim, EngineMesh& mesh)
{
    mesh.boneIndices.assign(mesh.positions.size(), glm::uvec4(0, 0, 0, 0));
    mesh.boneWeights.assign(mesh.positions.size(), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

    auto jointIt = prim.attributes.find("JOINTS_0");
    if (jointIt != prim.attributes.end()) {
        const auto& acc = gltf.accessors[jointIt->second];
        const auto* data = accessorPtr(gltf, acc);
        if (data && acc.type == TINYGLTF_TYPE_VEC4) {
            size_t stride = accessorStride(gltf, acc);
            size_t count = std::min(mesh.boneIndices.size(), static_cast<size_t>(acc.count));
            for (size_t k = 0; k < count; ++k) {
                mesh.boneIndices[k] = readJointVec4(data + k * stride, acc.componentType);
            }
        }
    }

    auto weightIt = prim.attributes.find("WEIGHTS_0");
    if (weightIt != prim.attributes.end()) {
        const auto& acc = gltf.accessors[weightIt->second];
        const auto* data = accessorPtr(gltf, acc);
        if (data && acc.type == TINYGLTF_TYPE_VEC4) {
            size_t stride = accessorStride(gltf, acc);
            size_t count = std::min(mesh.boneWeights.size(), static_cast<size_t>(acc.count));
            for (size_t k = 0; k < count; ++k) {
                mesh.boneWeights[k] = readWeightVec4(data + k * stride, acc.componentType, acc.normalized);
            }
        }
    }
}

static std::vector<EngineMesh> loadMeshes(const tinygltf::Model& gltf, std::vector<std::vector<uint32_t>>& meshMap)
{
    std::vector<EngineMesh> out;
    meshMap.resize(gltf.meshes.size());

    for (size_t i = 0; i < gltf.meshes.size(); ++i) {
        const auto& gltfMesh = gltf.meshes[i];

        for (const auto& prim : gltfMesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

            EngineMesh mesh;
            mesh.materialIndex = prim.material >= 0 ? static_cast<uint32_t>(prim.material) : 0;

            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;

            {
                const auto& acc = gltf.accessors[posIt->second];
                const auto* data = accessorPtr(gltf, acc);
                size_t stride = accessorStride(gltf, acc);
                mesh.positions.resize(acc.count);
                for (size_t k = 0; k < acc.count; ++k) mesh.positions[k] = *reinterpret_cast<const glm::vec3*>(data + k * stride);
            }

            auto normIt = prim.attributes.find("NORMAL");
            if (normIt != prim.attributes.end()) {
                const auto& acc = gltf.accessors[normIt->second];
                const auto* data = accessorPtr(gltf, acc);
                size_t stride = accessorStride(gltf, acc);
                mesh.normals.resize(acc.count);
                for (size_t k = 0; k < acc.count; ++k) mesh.normals[k] = *reinterpret_cast<const glm::vec3*>(data + k * stride);
            }
            else {
                mesh.normals.assign(mesh.positions.size(), glm::vec3(0.f, 1.f, 0.f));
            }

            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end()) {
                const auto& acc = gltf.accessors[uvIt->second];
                const auto* data = accessorPtr(gltf, acc);
                size_t stride = accessorStride(gltf, acc);
                mesh.texcoords.resize(acc.count);
                for (size_t k = 0; k < acc.count; ++k) mesh.texcoords[k] = *reinterpret_cast<const glm::vec2*>(data + k * stride);
            }
            else {
                mesh.texcoords.assign(mesh.positions.size(), glm::vec2(0.f));
            }

            loadSkinAttributes(gltf, prim, mesh);

            if (prim.indices >= 0) {
                const auto& acc = gltf.accessors[prim.indices];
                const auto* data = accessorPtr(gltf, acc);
                mesh.indices.reserve(acc.count);
                for (size_t k = 0; k < acc.count; ++k) {
                    uint32_t idx = 0;
                    switch (acc.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: idx = reinterpret_cast<const uint32_t*>(data)[k]; break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: idx = reinterpret_cast<const uint16_t*>(data)[k]; break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: idx = reinterpret_cast<const uint8_t*>(data)[k]; break;
                    default: break;
                    }
                    mesh.indices.push_back(idx);
                }
            }
            else {
                mesh.indices.reserve(mesh.positions.size());
                for (uint32_t k = 0; k < mesh.positions.size(); ++k) mesh.indices.push_back(k);
            }

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

static std::vector<EngineNode> loadNodes(const tinygltf::Model& gltf)
{
    std::vector<EngineNode> nodes(gltf.nodes.size());
    for (size_t i = 0; i < gltf.nodes.size(); ++i) {
        nodes[i].name = gltf.nodes[i].name.empty() ? ("Node_" + std::to_string(i)) : gltf.nodes[i].name;
        nodes[i].localTransform = getNodeTransform(gltf.nodes[i]);
    }

    for (size_t i = 0; i < gltf.nodes.size(); ++i) {
        for (int child : gltf.nodes[i].children) {
            if (child >= 0 && child < static_cast<int>(nodes.size())) nodes[child].parentIndex = static_cast<int>(i);
        }
    }

    auto computeGlobal = [&](auto&& self, int nodeIndex, const glm::mat4& parent) -> void {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size())) return;
        nodes[nodeIndex].globalBindTransform = parent * nodes[nodeIndex].localTransform;
        for (int child : gltf.nodes[nodeIndex].children) self(self, child, nodes[nodeIndex].globalBindTransform);
    };

    if (!gltf.scenes.empty()) {
        int sceneIdx = gltf.defaultScene > -1 ? gltf.defaultScene : 0;
        for (int root : gltf.scenes[sceneIdx].nodes) computeGlobal(computeGlobal, root, glm::mat4(1.0f));
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].parentIndex < 0 && nodes[i].globalBindTransform == glm::mat4(1.0f)) {
            computeGlobal(computeGlobal, static_cast<int>(i), glm::mat4(1.0f));
        }
    }

    return nodes;
}

static std::vector<EngineSkeleton> loadSkeletons(const tinygltf::Model& gltf, const std::vector<EngineNode>& nodes)
{
    std::vector<EngineSkeleton> skeletons;
    skeletons.reserve(gltf.skins.size());

    for (size_t skinIndex = 0; skinIndex < gltf.skins.size(); ++skinIndex) {
        const auto& gltfSkin = gltf.skins[skinIndex];
        EngineSkeleton skeleton;
        skeleton.name = gltfSkin.name.empty() ? ("Skeleton_" + std::to_string(skinIndex)) : gltfSkin.name;
        skeleton.rootNodeIndex = gltfSkin.skeleton;
        skeleton.jointNodeIndices.reserve(gltfSkin.joints.size());

        for (int jointNode : gltfSkin.joints) {
            skeleton.jointNodeIndices.push_back(static_cast<uint32_t>(jointNode));
        }

        skeleton.bindLocalTransforms.assign(skeleton.jointNodeIndices.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < skeleton.jointNodeIndices.size(); ++i) {
            uint32_t nodeIndex = skeleton.jointNodeIndices[i];
            if (nodeIndex < nodes.size()) {
                skeleton.bindLocalTransforms[i] = nodes[nodeIndex].localTransform;
            }
        }

        skeleton.inverseBindMatrices.assign(skeleton.jointNodeIndices.size(), glm::mat4(1.0f));
        if (gltfSkin.inverseBindMatrices >= 0) {
            const auto& acc = gltf.accessors[gltfSkin.inverseBindMatrices];
            const auto* data = accessorPtr(gltf, acc);
            if (data && acc.type == TINYGLTF_TYPE_MAT4 && acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                size_t stride = accessorStride(gltf, acc);
                size_t count = std::min(skeleton.inverseBindMatrices.size(), static_cast<size_t>(acc.count));
                for (size_t i = 0; i < count; ++i) {
                    skeleton.inverseBindMatrices[i] = glm::make_mat4(reinterpret_cast<const float*>(data + i * stride));
                }
            }
        }
        else {
            for (size_t i = 0; i < skeleton.jointNodeIndices.size(); ++i) {
                uint32_t nodeIndex = skeleton.jointNodeIndices[i];
                if (nodeIndex < nodes.size()) skeleton.inverseBindMatrices[i] = glm::inverse(nodes[nodeIndex].globalBindTransform);
            }
        }

        std::unordered_map<uint32_t, int> jointToLocal;
        for (size_t i = 0; i < skeleton.jointNodeIndices.size(); ++i) jointToLocal[skeleton.jointNodeIndices[i]] = static_cast<int>(i);

        skeleton.jointParentIndices.assign(skeleton.jointNodeIndices.size(), -1);
        for (size_t i = 0; i < skeleton.jointNodeIndices.size(); ++i) {
            uint32_t nodeIndex = skeleton.jointNodeIndices[i];
            if (nodeIndex >= nodes.size()) continue;
            auto parentIt = jointToLocal.find(static_cast<uint32_t>(nodes[nodeIndex].parentIndex));
            if (parentIt != jointToLocal.end()) skeleton.jointParentIndices[i] = parentIt->second;
        }

        skeletons.push_back(std::move(skeleton));
    }

    return skeletons;
}

static std::vector<EngineSkeletonPose> buildBindPoses(const std::vector<EngineSkeleton>& skeletons, const std::vector<EngineNode>& nodes)
{
    std::vector<EngineSkeletonPose> poses;
    poses.reserve(skeletons.size());

    for (size_t s = 0; s < skeletons.size(); ++s) {
        const EngineSkeleton& skeleton = skeletons[s];
        EngineSkeletonPose pose;
        pose.skeletonIndex = static_cast<uint32_t>(s);
        pose.localJointTransforms.resize(skeleton.jointNodeIndices.size(), glm::mat4(1.0f));
        pose.globalJointTransforms.resize(skeleton.jointNodeIndices.size(), glm::mat4(1.0f));
        pose.boneMatrices.resize(skeleton.jointNodeIndices.size(), glm::mat4(1.0f));

        for (size_t j = 0; j < skeleton.jointNodeIndices.size(); ++j) {
            if (j < skeleton.bindLocalTransforms.size()) {
                pose.localJointTransforms[j] = skeleton.bindLocalTransforms[j];
            }
        }

        update_skeleton_pose_matrices(skeleton, pose);

        poses.push_back(std::move(pose));
    }

    return poses;
}

static glm::vec4 readAnimationValue(const uint8_t* src, int componentType, int componentCount)
{
    glm::vec4 value(0.0f);
    if (componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        return value;
    }

    const auto* asFloat = reinterpret_cast<const float*>(src);
    if (componentCount > 0) value.x = asFloat[0];
    if (componentCount > 1) value.y = asFloat[1];
    if (componentCount > 2) value.z = asFloat[2];
    if (componentCount > 3) value.w = asFloat[3];
    return value;
}

static std::vector<EngineAnimationClip> loadAnimationClips(
    const tinygltf::Model& gltf,
    const std::vector<EngineSkeleton>& skeletons)
{
    std::vector<EngineAnimationClip> clips;

    std::vector<std::unordered_map<uint32_t, uint32_t>> jointMaps(skeletons.size());
    for (size_t skeletonIndex = 0; skeletonIndex < skeletons.size(); ++skeletonIndex) {
        const auto& skeleton = skeletons[skeletonIndex];
        auto& jointMap = jointMaps[skeletonIndex];
        for (uint32_t jointIndex = 0; jointIndex < skeleton.jointNodeIndices.size(); ++jointIndex) {
            jointMap[skeleton.jointNodeIndices[jointIndex]] = jointIndex;
        }
    }

    for (size_t animationIndex = 0; animationIndex < gltf.animations.size(); ++animationIndex) {
        const auto& animation = gltf.animations[animationIndex];

        for (size_t skeletonIndex = 0; skeletonIndex < skeletons.size(); ++skeletonIndex) {
            EngineAnimationClip clip;
            clip.name = animation.name.empty()
                ? ("Animation_" + std::to_string(animationIndex) + "_" + std::to_string(skeletonIndex))
                : animation.name + "_Skeleton_" + std::to_string(skeletonIndex);
            clip.skeletonIndex = static_cast<uint32_t>(skeletonIndex);

            const auto& jointMap = jointMaps[skeletonIndex];
            for (const auto& channel : animation.channels) {
                if (channel.target_node < 0) {
                    continue;
                }

                auto jointIt = jointMap.find(static_cast<uint32_t>(channel.target_node));
                if (jointIt == jointMap.end()) {
                    continue;
                }

                if (channel.sampler < 0 || channel.sampler >= static_cast<int>(animation.samplers.size())) {
                    continue;
                }

                const auto& sampler = animation.samplers[channel.sampler];
                if (sampler.input < 0 || sampler.input >= static_cast<int>(gltf.accessors.size()) ||
                    sampler.output < 0 || sampler.output >= static_cast<int>(gltf.accessors.size())) {
                    continue;
                }

                const auto& inputAccessor = gltf.accessors[sampler.input];
                const auto& outputAccessor = gltf.accessors[sampler.output];
                const uint8_t* inputData = accessorPtr(gltf, inputAccessor);
                const uint8_t* outputData = accessorPtr(gltf, outputAccessor);
                if (!inputData || !outputData || inputAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
                    continue;
                }

                const size_t inputStride = accessorStride(gltf, inputAccessor);
                const size_t outputStride = accessorStride(gltf, outputAccessor);
                const int componentCount = tinygltf::GetNumComponentsInType(outputAccessor.type);
                if (componentCount < 3) {
                    continue;
                }

                EngineAnimationChannel outChannel;
                outChannel.jointIndex = jointIt->second;
                if (channel.target_path == "rotation") {
                    outChannel.path = EngineAnimationTargetPath::Rotation;
                }
                else if (channel.target_path == "scale") {
                    outChannel.path = EngineAnimationTargetPath::Scale;
                }
                else if (channel.target_path == "translation") {
                    outChannel.path = EngineAnimationTargetPath::Translation;
                }
                else {
                    continue;
                }

                const bool isCubicSpline = sampler.interpolation == "CUBICSPLINE";
                const size_t keyframeCount = static_cast<size_t>(inputAccessor.count);
                outChannel.keyframeTimes.reserve(keyframeCount);
                outChannel.keyframeValues.reserve(keyframeCount);

                for (size_t keyIndex = 0; keyIndex < keyframeCount; ++keyIndex) {
                    const float* timePtr = reinterpret_cast<const float*>(inputData + keyIndex * inputStride);
                    outChannel.keyframeTimes.push_back(*timePtr);
                    clip.durationSeconds = std::max(clip.durationSeconds, *timePtr);

                    size_t outputIndex = isCubicSpline ? keyIndex * 3 + 1 : keyIndex;
                    outChannel.keyframeValues.push_back(readAnimationValue(outputData + outputIndex * outputStride, outputAccessor.componentType, componentCount));
                }

                if (!outChannel.keyframeTimes.empty()) {
                    clip.channels.push_back(std::move(outChannel));
                }
            }

            if (clip.IsValid()) {
                clips.push_back(std::move(clip));
            }
        }
    }

    return clips;
}

static glm::vec4 sampleChannelValue(const EngineAnimationChannel& channel, float timeSeconds)
{
    if (channel.keyframeTimes.empty() || channel.keyframeValues.empty()) {
        return glm::vec4(0.0f);
    }

    if (channel.keyframeTimes.size() == 1 || timeSeconds <= channel.keyframeTimes.front()) {
        return channel.keyframeValues.front();
    }

    const size_t lastIndex = channel.keyframeTimes.size() - 1;
    if (timeSeconds >= channel.keyframeTimes[lastIndex]) {
        return channel.keyframeValues[lastIndex];
    }

    for (size_t keyIndex = 0; keyIndex < lastIndex; ++keyIndex) {
        const float t0 = channel.keyframeTimes[keyIndex];
        const float t1 = channel.keyframeTimes[keyIndex + 1];
        if (timeSeconds < t0 || timeSeconds > t1) {
            continue;
        }

        const float alpha = (t1 > t0) ? ((timeSeconds - t0) / (t1 - t0)) : 0.0f;
        if (channel.path == EngineAnimationTargetPath::Rotation) {
            glm::quat q0 = glm::normalize(glm::quat(channel.keyframeValues[keyIndex].w, channel.keyframeValues[keyIndex].x, channel.keyframeValues[keyIndex].y, channel.keyframeValues[keyIndex].z));
            glm::quat q1 = glm::normalize(glm::quat(channel.keyframeValues[keyIndex + 1].w, channel.keyframeValues[keyIndex + 1].x, channel.keyframeValues[keyIndex + 1].y, channel.keyframeValues[keyIndex + 1].z));
            const glm::quat sampled = glm::normalize(glm::slerp(q0, q1, alpha));
            return glm::vec4(sampled.x, sampled.y, sampled.z, sampled.w);
        }

        return glm::mix(channel.keyframeValues[keyIndex], channel.keyframeValues[keyIndex + 1], alpha);
    }

    return channel.keyframeValues.back();
}

void update_skeleton_pose_matrices(const EngineSkeleton& skeleton, EngineSkeletonPose& pose)
{
    const size_t jointCount = skeleton.jointNodeIndices.size();
    if (pose.localJointTransforms.size() != jointCount) {
        pose.localJointTransforms.resize(jointCount, glm::mat4(1.0f));
    }
    if (pose.globalJointTransforms.size() != jointCount) {
        pose.globalJointTransforms.resize(jointCount, glm::mat4(1.0f));
    }
    if (pose.boneMatrices.size() != jointCount) {
        pose.boneMatrices.resize(jointCount, glm::mat4(1.0f));
    }

    for (size_t i = 0; i < jointCount; ++i) {
        const int parentIndex = i < skeleton.jointParentIndices.size() ? skeleton.jointParentIndices[i] : -1;
        if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < jointCount) {
            pose.globalJointTransforms[i] = pose.globalJointTransforms[parentIndex] * pose.localJointTransforms[i];
        }
        else {
            pose.globalJointTransforms[i] = pose.localJointTransforms[i];
        }

        const glm::mat4 inverseBind = i < skeleton.inverseBindMatrices.size()
            ? skeleton.inverseBindMatrices[i]
            : glm::mat4(1.0f);
        pose.boneMatrices[i] = pose.globalJointTransforms[i] * inverseBind;
    }
}

void update_model_skeleton_poses(EngineModel& model)
{
    for (auto& pose : model.skeletonPoses) {
        if (pose.skeletonIndex >= model.skeletons.size()) {
            continue;
        }

        update_skeleton_pose_matrices(model.skeletons[pose.skeletonIndex], pose);
    }
}

void sample_animation_clip(const EngineSkeleton& skeleton, const EngineAnimationClip& clip, float timeSeconds, EngineSkeletonPose& pose)
{
    const size_t jointCount = skeleton.jointNodeIndices.size();
    if (jointCount == 0) {
        return;
    }

    std::vector<glm::vec3> translations(jointCount, glm::vec3(0.0f));
    std::vector<glm::quat> rotations(jointCount, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    std::vector<glm::vec3> scales(jointCount, glm::vec3(1.0f));

    for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
        const glm::mat4 baseTransform = jointIndex < skeleton.bindLocalTransforms.size()
            ? skeleton.bindLocalTransforms[jointIndex]
            : glm::mat4(1.0f);
        decomposeTransform(baseTransform, translations[jointIndex], rotations[jointIndex], scales[jointIndex]);
    }

    for (const auto& channel : clip.channels) {
        if (channel.jointIndex >= jointCount) {
            continue;
        }

        const glm::vec4 sampled = sampleChannelValue(channel, timeSeconds);
        switch (channel.path) {
        case EngineAnimationTargetPath::Translation:
            translations[channel.jointIndex] = glm::vec3(sampled);
            break;
        case EngineAnimationTargetPath::Rotation:
            rotations[channel.jointIndex] = glm::normalize(glm::quat(sampled.w, sampled.x, sampled.y, sampled.z));
            break;
        case EngineAnimationTargetPath::Scale:
            scales[channel.jointIndex] = glm::vec3(sampled);
            break;
        }
    }

    if (pose.localJointTransforms.size() != jointCount) {
        pose.localJointTransforms.resize(jointCount, glm::mat4(1.0f));
    }

    for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
        pose.localJointTransforms[jointIndex] = composeTransform(translations[jointIndex], rotations[jointIndex], scales[jointIndex]);
    }

    update_skeleton_pose_matrices(skeleton, pose);
}

static void processNode(
    const tinygltf::Model& gltf,
    int nodeIdx,
    const glm::mat4& parentMatrix,
    const std::vector<std::vector<uint32_t>>& meshMap,
    std::vector<EngineInstance>& outInstances)
{
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(gltf.nodes.size())) return;
    const tinygltf::Node& node = gltf.nodes[nodeIdx];

    glm::mat4 globalMatrix = parentMatrix * getNodeTransform(node);

    if (node.mesh >= 0 && node.mesh < static_cast<int>(meshMap.size())) {
        const auto& engineMeshIndices = meshMap[node.mesh];
        for (uint32_t meshIndex : engineMeshIndices) {
            EngineInstance instance;
            instance.meshIndex = meshIndex;
            instance.transform = globalMatrix;
            instance.name = node.name.empty() ? ("Node_" + std::to_string(nodeIdx)) : node.name;
            instance.skeletonIndex = node.skin;
            outInstances.push_back(instance);
        }
    }

    for (int childIdx : node.children) {
        processNode(gltf, childIdx, globalMatrix, meshMap, outInstances);
    }
}

static bool shouldUseWhitePrototypeMaterial(const std::string& filePath)
{
    std::string lower = filePath;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower.find("ragdoll") != std::string::npos ||
        lower.find("cesium_man") != std::string::npos;
}
static void applyWhitePrototypeMaterial(EngineModel& model)
{
    EngineMaterial white{};
    white.baseColorTexture = -1;
    white.normalTexture = -1;
    white.metalRoughTexture = -1;
    white.occlusionTexture = -1;
    white.emissiveTexture = -1;
    white.alphaMaskTexture = -1;
    white.baseColorFactor = glm::vec4(1.0f);
    white.metallicFactor = 0.0f;
    white.roughnessFactor = 0.65f;

    model.textures.clear();
    model.materials.clear();
    model.materials.push_back(white);
    for (auto& mesh : model.meshes) mesh.materialIndex = 0;
}

static glm::mat4 ozzRestPoseToGlmMatrix(const ozz::math::SoaTransform& restPose, int lane)
{
    float tx[4], ty[4], tz[4];
    float qx[4], qy[4], qz[4], qw[4];
    float sx[4], sy[4], sz[4];

    ozz::math::StorePtrU(restPose.translation.x, tx);
    ozz::math::StorePtrU(restPose.translation.y, ty);
    ozz::math::StorePtrU(restPose.translation.z, tz);
    ozz::math::StorePtrU(restPose.rotation.x, qx);
    ozz::math::StorePtrU(restPose.rotation.y, qy);
    ozz::math::StorePtrU(restPose.rotation.z, qz);
    ozz::math::StorePtrU(restPose.rotation.w, qw);
    ozz::math::StorePtrU(restPose.scale.x, sx);
    ozz::math::StorePtrU(restPose.scale.y, sy);
    ozz::math::StorePtrU(restPose.scale.z, sz);

    return composeTransform(
        glm::vec3(tx[lane], ty[lane], tz[lane]),
        glm::normalize(glm::quat(qw[lane], qx[lane], qy[lane], qz[lane])),
        glm::vec3(sx[lane], sy[lane], sz[lane]));
}

static EngineMaterial loadXBotMaterial(const fs::path& materialPath)
{
    EngineMaterial material{};
    std::ifstream stream(materialPath);
    if (!stream.is_open()) {
        return material;
    }

    const json data = json::parse(stream, nullptr, false);
    if (data.is_discarded() || !data.contains("properties")) {
        return material;
    }

    const json& properties = data["properties"];
    auto loadVec4 = [&](const char* key, glm::vec4 fallback) {
        if (!properties.contains(key)) {
            return fallback;
        }
        const json& value = properties[key]["value"];
        if (!value.is_array() || value.size() < 4) {
            return fallback;
        }
        return glm::vec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>());
    };

    material.baseColorFactor = loadVec4("baseColorFactor", glm::vec4(1.0f));
    const glm::vec4 emissive = loadVec4("emissiveColorFactor", glm::vec4(0.0f));
    material.emissiveFactor = glm::vec3(emissive);
    return material;
}

static void loadXBotSkeleton(const fs::path& skeletonPath, EngineSkeleton& skeleton)
{
    ozz::io::File file(skeletonPath.string().c_str(), "rb");
    if (!file.opened()) {
        throw std::runtime_error("Failed to open ozz skeleton: " + skeletonPath.string());
    }

    ozz::io::IArchive archive(&file);
    if (!archive.TestTag<ozz::animation::Skeleton>()) {
        throw std::runtime_error("Invalid ozz skeleton archive: " + skeletonPath.string());
    }

    ozz::animation::Skeleton runtimeSkeleton;
    archive >> runtimeSkeleton;

    skeleton.name = skeletonPath.stem().string();
    skeleton.rootNodeIndex = -1;
    skeleton.runtimeSkeleton = new ozz::animation::Skeleton(std::move(runtimeSkeleton));

    auto* runtime = static_cast<ozz::animation::Skeleton*>(skeleton.runtimeSkeleton);
    const int jointCount = runtime->num_joints();
    skeleton.jointNodeIndices.resize(jointCount);
    skeleton.jointParentIndices.resize(jointCount);
    skeleton.bindLocalTransforms.resize(jointCount, glm::mat4(1.0f));

    const auto parents = runtime->joint_parents();
    const auto restPoses = runtime->joint_rest_poses();
    for (int jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
        skeleton.jointNodeIndices[jointIndex] = static_cast<uint32_t>(jointIndex);
        skeleton.jointParentIndices[jointIndex] = parents[jointIndex];
        skeleton.bindLocalTransforms[jointIndex] = ozzRestPoseToGlmMatrix(restPoses[jointIndex / 4], jointIndex % 4);
    }
}

static void loadXBotAnimations(const fs::path& animationsDir, uint32_t skeletonIndex, std::vector<EngineAnimationClip>& clips)
{
    if (!fs::exists(animationsDir)) {
        return;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(animationsDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ozz") {
            continue;
        }

        ozz::io::File file(entry.path().string().c_str(), "rb");
        if (!file.opened()) {
            continue;
        }

        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Animation>()) {
            continue;
        }

        ozz::animation::Animation runtimeAnimation;
        archive >> runtimeAnimation;

        EngineAnimationClip clip{};
        clip.name = runtimeAnimation.name()[0] != '\0' ? runtimeAnimation.name() : entry.path().stem().string();
        clip.skeletonIndex = skeletonIndex;
        clip.durationSeconds = runtimeAnimation.duration();
        clip.runtimeAnimation = new ozz::animation::Animation(std::move(runtimeAnimation));
        clips.push_back(std::move(clip));
    }
}

static EngineModel load_engine_model_xbot(const char* path)
{
    const fs::path meshPath(path);
    std::ifstream meshStream(meshPath);
    if (!meshStream.is_open()) {
        throw std::runtime_error("Failed to open X Bot mesh: " + meshPath.string());
    }

    const json meshJson = json::parse(meshStream, nullptr, false);
    if (meshJson.is_discarded()) {
        throw std::runtime_error("Failed to parse X Bot mesh json: " + meshPath.string());
    }

    const fs::path assetDir = meshPath.parent_path();
    const fs::path bufferPath = assetDir / meshJson["buffer"].get<std::string>();

    std::ifstream bufferStream(bufferPath, std::ios::binary);
    if (!bufferStream.is_open()) {
        throw std::runtime_error("Failed to open X Bot buffer: " + bufferPath.string());
    }
    std::vector<char> bufferData((std::istreambuf_iterator<char>(bufferStream)), std::istreambuf_iterator<char>());
    const uint8_t* rawBytes = reinterpret_cast<const uint8_t*>(bufferData.data());

    EngineModel model;

    if (meshJson.contains("meshes")) {
        for (const json& meshInfo : meshJson["meshes"]) {
            model.materials.push_back(loadXBotMaterial(assetDir / meshInfo["material"].get<std::string>()));
        }
    }
    if (model.materials.empty()) {
        model.materials.push_back(EngineMaterial{});
    }

    const json& vertices = meshJson["vertices"];
    const json& indices = meshJson["indices"];
    const size_t vertexStride = vertices["stride"].get<size_t>();
    const size_t vertexBaseOffset = vertices["byteOffset"].get<size_t>();
    const size_t indexBaseOffset = indices["byteOffset"].get<size_t>();
    const size_t indexStride = indices["stride"].get<size_t>();

    for (size_t meshMaterialIndex = 0; meshMaterialIndex < meshJson["meshes"].size(); ++meshMaterialIndex) {
        const json& meshInfo = meshJson["meshes"][meshMaterialIndex];

        EngineMesh mesh{};
        mesh.materialIndex = static_cast<uint32_t>(std::min(meshMaterialIndex, model.materials.size() - 1));

        const size_t vertexOffset = meshInfo["vertexOffset"].get<size_t>();
        const size_t vertexCount = meshInfo["numVertices"].get<size_t>();
        mesh.positions.resize(vertexCount);
        mesh.normals.resize(vertexCount);
        mesh.texcoords.resize(vertexCount);
        mesh.boneIndices.resize(vertexCount);
        mesh.boneWeights.resize(vertexCount);

        for (size_t i = 0; i < vertexCount; ++i) {
            const uint8_t* vertexPtr = rawBytes + vertexBaseOffset + (vertexOffset + i) * vertexStride;
            mesh.positions[i] = *reinterpret_cast<const glm::vec3*>(vertexPtr + 0);
            mesh.normals[i] = *reinterpret_cast<const glm::vec3*>(vertexPtr + 12);
            mesh.texcoords[i] = *reinterpret_cast<const glm::vec2*>(vertexPtr + 24);
            mesh.boneIndices[i] = glm::uvec4(
                *reinterpret_cast<const int32_t*>(vertexPtr + 32),
                *reinterpret_cast<const int32_t*>(vertexPtr + 36),
                *reinterpret_cast<const int32_t*>(vertexPtr + 40),
                *reinterpret_cast<const int32_t*>(vertexPtr + 44));
            mesh.boneWeights[i] = *reinterpret_cast<const glm::vec4*>(vertexPtr + 48);
        }

        const json& lod = meshInfo["LOD"][0];
        const size_t indexOffset = lod["indexOffset"].get<size_t>();
        const size_t indexCount = lod["numIndices"].get<size_t>();
        mesh.indices.resize(indexCount);
        for (size_t i = 0; i < indexCount; ++i) {
            const uint8_t* indexPtr = rawBytes + indexBaseOffset + (indexOffset + i) * indexStride;
            mesh.indices[i] = *reinterpret_cast<const uint32_t*>(indexPtr);
        }

        if (meshInfo.contains("aabb")) {
            const json& aabb = meshInfo["aabb"];
            mesh.localAabbMin = glm::vec3(aabb["min"][0].get<float>(), aabb["min"][1].get<float>(), aabb["min"][2].get<float>());
            mesh.localAabbMax = glm::vec3(aabb["max"][0].get<float>(), aabb["max"][1].get<float>(), aabb["max"][2].get<float>());
        }

        model.meshes.push_back(std::move(mesh));
    }

    EngineSkeleton skeleton{};
    loadXBotSkeleton(assetDir / "skeleton.ozz", skeleton);
    const json& inverseBindPose = meshJson["inverseBindPose"];
    const size_t inverseBindOffset = inverseBindPose["byteOffset"].get<size_t>();
    const size_t inverseBindCount = inverseBindPose["count"].get<size_t>();
    skeleton.inverseBindMatrices.resize(inverseBindCount, glm::mat4(1.0f));
    for (size_t i = 0; i < inverseBindCount; ++i) {
        skeleton.inverseBindMatrices[i] = *reinterpret_cast<const glm::mat4*>(rawBytes + inverseBindOffset + i * sizeof(glm::mat4));
    }
    model.skeletons.push_back(std::move(skeleton));

    EngineSkeletonPose pose{};
    pose.skeletonIndex = 0;
    pose.localJointTransforms = model.skeletons[0].bindLocalTransforms;
    update_skeleton_pose_matrices(model.skeletons[0], pose);
    model.skeletonPoses.push_back(std::move(pose));

    loadXBotAnimations(assetDir / "animations", 0u, model.animationClips);

    for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
        EngineInstance instance{};
        instance.meshIndex = static_cast<uint32_t>(meshIndex);
        instance.transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
        instance.name = meshJson["meshes"][meshIndex]["name"].get<std::string>();
        instance.skeletonIndex = 0;
        model.scenes.push_back(std::move(instance));
    }

    return model;
}

EngineModel load_engine_model_glb(const char* path)
{
    const fs::path assetPath(path);
    if (assetPath.extension() == ".mesh") {
        return load_engine_model_xbot(path);
    }

    tinygltf::Model gltf;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    std::string filePath(path);
    bool ret = false;
    if (filePath.length() >= 5 && filePath.substr(filePath.length() - 5) == ".gltf") {
        ret = loader.LoadASCIIFromFile(&gltf, &err, &warn, path);
    }
    else {
        ret = loader.LoadBinaryFromFile(&gltf, &err, &warn, path);
    }

    if (!ret) throw std::runtime_error(std::string("tinygltf failed to load ") + path + ": " + err);
    if (!warn.empty()) std::fprintf(stderr, "[tinygltf warn] %s\n", warn.c_str());

    EngineModel model;
    model.textures = loadTextures(gltf);
    model.materials = loadMaterials(gltf, model.textures);
    model.nodes = loadNodes(gltf);
    model.skeletons = loadSkeletons(gltf, model.nodes);
    model.skeletonPoses = buildBindPoses(model.skeletons, model.nodes);
    model.animationClips = loadAnimationClips(gltf, model.skeletons);

    std::vector<std::vector<uint32_t>> meshMap;
    model.meshes = loadMeshes(gltf, meshMap);

    if (!gltf.scenes.empty()) {
        int sceneIdx = gltf.defaultScene > -1 ? gltf.defaultScene : 0;
        const tinygltf::Scene& scene = gltf.scenes[sceneIdx];
        for (int nodeIdx : scene.nodes) {
            processNode(gltf, nodeIdx, glm::mat4(1.0f), meshMap, model.scenes);
        }
    }
    else {
        for (size_t i = 0; i < model.meshes.size(); ++i) {
            EngineInstance instance;
            instance.meshIndex = static_cast<uint32_t>(i);
            instance.transform = glm::mat4(1.0f);
            instance.name = "Fallback_Mesh_" + std::to_string(i);
            model.scenes.push_back(instance);
        }
    }

    if (shouldUseWhitePrototypeMaterial(filePath)) applyWhitePrototypeMaterial(model);
    return model;
}

EngineModel load_engine_model(const char* path)
{
    return load_engine_model_glb(path);
}
