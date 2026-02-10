#ifndef BAKED_MODEL_HPP_4C4E19C7_7684_4567_9B48_7CC09595FAC8
#define BAKED_MODEL_HPP_4C4E19C7_7684_4567_9B48_7CC09595FAC8

#include <string>
#include <vector>

#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>


/* Baked file format:
 *
 *  1. Header:
 *    - 16*char: file magic = "\0\0COMP5892Mmesh"
 *    - 16*char: variant = "default" (changes later)
 *
 *  2. Textures
 *    - 1*uint32_t: U = number of (unique) textures
 *    - repeat U times:
 *      - string: path to texture
 *      - 1*uint8_t: texture color space (see ETextureSpace)
 *      - 1*uint8_t: number of channels in texture
 *
 *  3. Material information
 *    - 1*uint32_t: M = number of materials
 *    - repeat M times:
 *      - uint32_t: base color texture index
 *      - uint32_t: roughness texture index
 *      - uint32_t: metalness texture index
 *      - uint32_t: alpha mask texture index; set to 0xffffffff if not available
 *      - uint32_t: normal map texture index; set to 0xffffffff if not available
 *      - uint32_t: emissive texture index;
 *
 *  4. Mesh data
 *    - 1*uint32_t: M = number of meshes
 *    - repeat M times:
 *      - uint32_t : material index
 *      - uint32_t : V = number of vertices
 *      - uint32_t : I = number of indices
 *      - repeat V times: vec3 position
 *      - repeat V times: vec3 normal
 *      - repeat V times: vec2 texture coordinate
 *      - repeat I times: uint32_t index
 *
 * Strings are stored as
 *   - 1*uint32_t: N = length of string in chars, including terminating \0
 *   - repeat N times: char in string
 *
 * See a12-bake/main.cpp (specifically write_model_data_()) for additional
 * information.
 *
 *
 * My suggestion for loading the data into Vulkan is as follows:
 *
 * - Create and load textures. This gives a list of Images (which includes a
 *   VkImage + VmaAllocation) and VkImageViews. We only need to keep these
 *   around -- place them in a vector.
 *
 * - Create a Descriptor Set Layout for material information only. Initially,
 *   this would include three textures (base color, metalness, roughness).
 *
 * - Create a Descriptor Set for each material. You can easily get the
 *   VkImageViews from the list in the first step by the index in the
 *   BaseMaterialInfo. This also avoids loading duplicates of textures if they
 *   are reused across multiple materials.
 *
 * - Upload mesh data. In my reference solution, I created separate VkBuffers
 *   for each mesh (one for each attribute and one for the indices).
 */

enum class ETextureSpace : std::uint8_t
{
	unorm = 0,
	srgb = 1
};

struct BakedTextureInfo
{
	std::string path;
	ETextureSpace space;
	std::uint8_t channels;
};

struct BakedMaterialInfo
{
	std::uint32_t baseColorTextureId;
	std::uint32_t roughnessTextureId;
	std::uint32_t metalnessTextureId;
	std::uint32_t alphaMaskTextureId; // May be set to 0xffffffff if no alpha mask
	std::uint32_t normalMapTextureId; // May be set to 0xffffffff if no normal map
	std::uint32_t emissiveTextureId; // May be set to 0xffffffff if no emissive map

	// The emissive map can be ignored in Assignment 1.2. It is only required 
	// in parts of Assignment 2.2.
};

struct BakedMeshData
{
	std::uint32_t materialId;

	std::vector<glm::vec3> positions;
	std::vector<glm::vec2> texcoords;
	std::vector<glm::vec3> normals;


	std::vector<std::uint32_t> indices;
};

struct BakedModel
{
	std::vector<BakedTextureInfo> textures;
	std::vector<BakedMaterialInfo> materials;
	std::vector<BakedMeshData> meshes;
};

BakedModel load_baked_model( char const* aModelPath );

#endif // BAKED_MODEL_HPP_4C4E19C7_7684_4567_9B48_7CC09595FAC8

