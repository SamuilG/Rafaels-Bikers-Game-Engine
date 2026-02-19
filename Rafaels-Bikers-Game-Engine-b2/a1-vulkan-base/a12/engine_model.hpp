#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
	// roughness and metalic share the same texture in gltf, so only one index is needed
    int metalRoughTexture = -1;
    int occlusionTexture = -1;
    int emissiveTexture = -1;
    int alphaMaskTexture = -1; // -1 = no alpha mask

    glm::vec4 baseColorFactor{ 1.f, 1.f, 1.f, 1.f };
    float     metallicFactor = 1.f;
    float     roughnessFactor = 1.f;
    glm::vec3 emissiveFactor{ 0.f, 0.f, 0.f };
    float     alphaCutoff = 0.5f;
    bool      alphaBlend = false;
};


struct EngineMesh {
    uint32_t                materialIndex = 0;
    std::vector<glm::vec3>  positions;
    std::vector<glm::vec3>  normals;
    std::vector<glm::vec2>  texcoords;
    std::vector<uint32_t>   indices;
	// lightmapUVs may be added in the future
};

struct EngineInstance {
    uint32_t  meshIndex; 
	glm::mat4 transform; // world transform matrix for this instance, calculated from gltf node hierarchy
};

struct EngineModel {
    std::vector<EngineTexture>  textures;
    std::vector<EngineMaterial> materials;
    std::vector<EngineMesh>     meshes;
    std::vector<EngineInstance> scenes;
};

EngineModel load_engine_model_glb(const char* path);