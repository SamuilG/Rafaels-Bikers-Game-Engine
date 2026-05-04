#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

enum class ETextureSpace : uint8_t {
    unorm = 0,
    srgb = 1
};

//decoded from glb
struct EngineTexture {
    std::vector<uint8_t> pixels; // compulsory rgba 4 channelsa
    int         width = 0;
    int         height = 0;
    ETextureSpace space = ETextureSpace::unorm;
    std::string name;            //for debug
};

struct EngineMaterial {
    int baseColorTexture = -1;
    int normalTexture = -1;
    int metalRoughTexture = -1;
    int occlusionTexture = -1;
    int emissiveTexture = -1;
    int alphaMaskTexture = -1; // -1 = no alpha mask

    glm::vec3 emissiveFactor{ 0.f, 0.f, 0.f };
    float     alphaCutoff = 0.5f;
    bool      alphaBlend = false;

    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
};

struct EngineNode {
    std::string name;
    int parentIndex = -1;
    glm::mat4 localTransform = glm::mat4(1.0f);
    glm::mat4 globalBindTransform = glm::mat4(1.0f);
};

struct EngineSkeleton {
    std::string name;
    int rootNodeIndex = -1;
    std::vector<uint32_t> jointNodeIndices;
    std::vector<int> jointParentIndices;
    std::vector<glm::mat4> bindLocalTransforms;
    std::vector<glm::mat4> inverseBindMatrices;
    void* runtimeSkeleton = nullptr;

    EngineSkeleton() = default;
    ~EngineSkeleton();
    EngineSkeleton(const EngineSkeleton&) = delete;
    EngineSkeleton& operator=(const EngineSkeleton&) = delete;
    EngineSkeleton(EngineSkeleton&& other) noexcept;
    EngineSkeleton& operator=(EngineSkeleton&& other) noexcept;
};

struct EngineSkeletonPose {
    uint32_t skeletonIndex = 0;
    std::vector<glm::mat4> localJointTransforms;
    std::vector<glm::mat4> globalJointTransforms;
    std::vector<glm::mat4> boneMatrices;
};

enum class EngineAnimationTargetPath : uint8_t {
    Translation = 0,
    Rotation = 1,
    Scale = 2
};

struct EngineAnimationChannel {
    uint32_t jointIndex = 0;
    EngineAnimationTargetPath path = EngineAnimationTargetPath::Translation;
    std::vector<float> keyframeTimes;
    std::vector<glm::vec4> keyframeValues;
};

struct EngineAnimationClip {
    std::string name;
    uint32_t skeletonIndex = 0;
    float durationSeconds = 0.0f;
    std::vector<EngineAnimationChannel> channels;
    void* runtimeAnimation = nullptr;

    EngineAnimationClip() = default;
    ~EngineAnimationClip();
    EngineAnimationClip(const EngineAnimationClip&) = delete;
    EngineAnimationClip& operator=(const EngineAnimationClip&) = delete;
    EngineAnimationClip(EngineAnimationClip&& other) noexcept;
    EngineAnimationClip& operator=(EngineAnimationClip&& other) noexcept;

    bool IsValid() const {
        return (!channels.empty() || runtimeAnimation != nullptr) && durationSeconds >= 0.0f;
    }
};

struct EngineMesh {
    uint32_t                materialIndex = 0;
    std::vector<glm::vec3>  positions;
    std::vector<glm::vec3>  normals;
    std::vector<glm::vec2>  texcoords;
    std::vector<glm::uvec4> boneIndices;
    std::vector<glm::vec4>  boneWeights;
    std::vector<uint32_t>   indices;
    glm::vec3 localAabbMin = glm::vec3(0.0f); //frustum culling
    glm::vec3 localAabbMax = glm::vec3(0.0f); //frustum culling
};

struct EngineInstance {
    uint32_t  meshIndex;
    glm::mat4 transform; // world transform matrix for this instance, calculated from gltf node hierarchy
    std::string name; // for entity naming in SceneManager
    int skeletonIndex = -1;
};

struct EngineModel {
    std::vector<EngineTexture>       textures;
    std::vector<EngineMaterial>      materials;
    std::vector<EngineMesh>          meshes;
    std::vector<EngineInstance>      scenes;
    std::vector<EngineNode>          nodes;
    std::vector<EngineSkeleton>      skeletons;
    std::vector<EngineSkeletonPose>  skeletonPoses;
    std::vector<EngineAnimationClip> animationClips;
    std::map<std::string, glm::mat4> namedTransforms;
};

EngineModel load_engine_model_glb(const char* path);
EngineModel load_engine_model(const char* path);
void update_skeleton_pose_matrices(const EngineSkeleton& skeleton, EngineSkeletonPose& pose);
void update_model_skeleton_poses(EngineModel& model);
void sample_animation_clip(const EngineSkeleton& skeleton, const EngineAnimationClip& clip, float timeSeconds, EngineSkeletonPose& pose);
