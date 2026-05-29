#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
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
    // Material slots must match the PBR shader descriptors.
    int baseColorTexture = -1;
    int normalTexture = -1;
    int metalRoughTexture = -1; // glTF ORM texture
    int occlusionTexture = -1;
    int emissiveTexture = -1;
    int alphaMaskTexture = -1;
    int _pad[2]; // pad to 32 bytes

    // Material factors, ordered to match PushConstants.
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    glm::vec4 emissiveFactor = glm::vec4(0.0f);

    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float alphaCutoff = 0.5f;
    float _pad2;

    bool  alphaBlend = false;
};

struct EngineMesh {
    uint32_t                materialIndex = 0;
    std::vector<glm::vec3>  positions;
    std::vector<glm::vec3>  normals;
    std::vector<glm::vec2>  texcoords;
    std::vector<uint32_t>   indices;
    glm::vec3 localAabbMin = glm::vec3(0.0f); //frustum culling
    glm::vec3 localAabbMax = glm::vec3(0.0f); //frustum culling
    // Skeletal skinning data (only populated when isSkinned == true)
    std::vector<glm::uvec4> jointIndices; // 4 joint indices per vertex
    std::vector<glm::vec4>  jointWeights; // 4 weights per vertex (should sum to 1)
    bool isSkinned = false;
};

struct EngineInstance {
    uint32_t  meshIndex;
    glm::mat4 transform; // world transform matrix for this instance
    std::string name;    // for entity naming in SceneManager
    int skinIndex = -1;  // index into EngineModel::skins (-1 = not skinned)
    int nodeIndex = -1;  // gltf node index (for animation mapping)
};

// One node in the GLTF node hierarchy (needed for joint matrix computation)
struct EngineNode {
    int parentIndex = -1;            // -1 for root nodes
    glm::mat4 localTransform = glm::mat4(1.0f); // default local transform from GLTF
    std::string name;
};

// Skin: maps joint nodes to their inverse bind matrices
struct EngineSkin {
    std::vector<int>       joints;              // gltf node indices for each joint
    std::vector<glm::mat4> inverseBindMatrices; // one per joint
    int skeletonRoot = -1;                      // root node index (-1 = not specified)
};

// Animation keyframe sampler (linear or step interpolation)
struct EngineAnimSampler {
    std::vector<float>      times;   // keyframe timestamps in seconds
    std::vector<glm::vec4>  values;  // T/S: (x,y,z,0), R: (x,y,z,w) quaternion
    int interp = 0;                  // 0=LINEAR, 1=STEP
};

// One channel: drives a specific TRS property of one node
struct EngineAnimChannel {
    int samplerIndex = -1;
    int nodeIndex    = -1;
    int path         = -1; // 0=translation, 1=rotation, 2=scale
};

// One animation clip
struct EngineAnimation {
    std::string name;
    std::vector<EngineAnimSampler> samplers;
    std::vector<EngineAnimChannel> channels;
    float duration = 0.0f; // max timestamp across all samplers
};

struct EngineModel {
    std::vector<EngineTexture>   textures;
    std::vector<EngineMaterial>  materials;
    std::vector<EngineMesh>      meshes;
    std::vector<EngineInstance>  scenes;
    // Animation / skinning data
    std::vector<EngineNode>      nodes;
    std::vector<EngineSkin>      skins;
    std::vector<EngineAnimation> animations;
    // Named empty nodes (e.g. "Anchor" for rider seating position)
    std::map<std::string, glm::mat4> namedTransforms;
};

// Keep this layout in sync with the GLSL PushConstants block.
struct PushConstants {
    glm::mat4 transform;        // 64 bytes
    glm::vec4 baseColorFactor;  // 16 bytes
    glm::vec4 emissiveFactor;   // 16 bytes
    float metallicFactor;       // 4 bytes
    float roughnessFactor;      // 4 bytes
    float alphaCutoff;          // 4 bytes
    float _pad;                 // 4 bytes padding
    glm::vec4 clipPlane;
};
EngineModel load_engine_model_glb(const char* path);
