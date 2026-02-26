#include <volk/volk.h>

#include <print>
#include <chrono>
#include <limits>
#include <vector>
#include <stdexcept>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if !defined(GLM_FORCE_RADIANS)
#	define GLM_FORCE_RADIANS
#endif
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../labut2/angle.hpp"
using namespace labut2::literals;

#include "../labut2/load.hpp"
#include "../labut2/error.hpp"
#include "../labut2/synch.hpp"
#include "../labut2/vkimage.hpp"
#include "../labut2/commands.hpp"
#include "../labut2/textures.hpp"
#include "../labut2/vkbuffer.hpp"
#include "../labut2/vkobject.hpp"
#include "../labut2/to_string.hpp"
#include "../labut2/descriptors.hpp"
#include "../labut2/vulkan_window.hpp"
namespace lut = labut2;

#include "engine_model.hpp"
#include "camera.hpp"
#include "setup.hpp"
#include "rendering.hpp"
#include "scene_manager.hpp"
#include "physics_system.hpp"
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

namespace glsl
{
	struct MosaicUniform {
		int mosaicOn;
		float pad[3];
	};
}

namespace
{
	namespace local_cfg
	{
		constexpr char const* SceneModel = "assets/a12/models/TScene.glb";
	}

	using Clock_ = std::chrono::steady_clock;
	using Secondsf_ = std::chrono::duration<float, std::ratio<1>>;
	constexpr float kPi_ = 3.14159265359f;

}

int main() try
{
	// Create Vulkan Window
	auto window = lut::make_vulkan_window();

	// Initialize state
	UserState state{};
	glfwSetWindowUserPointer( window.window, &state );
	glfwSetKeyCallback( window.window, &glfw_callback_key_press );
	glfwSetMouseButtonCallback( window.window, &glfw_callback_button );
	glfwSetCursorPosCallback( window.window, &glfw_callback_motion ); 

	// Create scene data
	// ...
	
	// Create VMA allocator
	lut::Allocator allocator = lut::create_allocator( window );

	// Intialize resources
	lut::DescriptorSetLayout sceneLayout = create_scene_descriptor_layout( window );
	lut::DescriptorSetLayout objectLayout = create_object_descriptor_layout( window );
	lut::DescriptorSetLayout postProcLayout = create_post_proc_descriptor_layout( window );

	lut::PipelineLayout pipeLayout = create_triangle_pipeline_layout( window, sceneLayout.handle, objectLayout.handle );
	lut::PipelineLayout postProcPipelineLayout = create_post_proc_pipeline_layout( window, postProcLayout.handle );

	lut::Pipeline pipe = create_triangle_pipeline( window, pipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT );
	
	// Create multiple debug pipelines
	lut::Pipeline mipPipe = create_debug_pipeline( window, pipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugMipFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT );
	lut::Pipeline depthPipe = create_debug_pipeline( window, pipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDepthFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT );
	lut::Pipeline derivPipe = create_debug_pipeline( window, pipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDerivFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT );
	
	// overdraw/overshading pipelines
	// pipelines for part 2 task 1
	lut::Pipeline overdrawPipe = create_overdraw_pipeline( window, pipeLayout.handle, VK_FORMAT_R8G8B8A8_UNORM );
	lut::Pipeline overshadingPipe = create_overshading_pipeline( window, pipeLayout.handle, VK_FORMAT_R8G8B8A8_UNORM );
	// resolve pass
	lut::Pipeline visResolvePipe = create_vis_resolve_pipeline( window, postProcPipelineLayout.handle, postProcLayout.handle );


	lut::CommandPool cpool = lut::create_command_pool( window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT );

	std::size_t frameIndex = 0;
	std::vector<VkCommandBuffer> cbuffers;
	std::vector<lut::Fence> frameDone;
	std::vector<lut::Semaphore> imageAvailable, renderFinished;
	
	for( std::size_t i = 0; i < window.swapImages.size(); ++i )
	{
		cbuffers.emplace_back( lut::alloc_command_buffer( window, cpool.handle ) );
		frameDone.emplace_back( lut::create_fence( window.device, VK_FENCE_CREATE_SIGNALED_BIT ) );
		imageAvailable.emplace_back( lut::create_semaphore( window.device ) );
		renderFinished.emplace_back( lut::create_semaphore( window.device ) );
	}

	// Load data
	// TexturedMesh planeMesh = create_plane_mesh( window, allocator );
	// TexturedMesh spriteMesh = create_sprite_mesh( window, allocator );
	
	EngineModel model = load_engine_model_glb(local_cfg::SceneModel);
	// 实例化场景管理器并加载实体
	SceneManager sceneManager;
	sceneManager.load_model(model);
	sceneManager.print_all_entities();

	// init physics
	PhysicsSystem physicsSystem;
	physicsSystem.init();

	// creating test physics bodies
	JPH::BodyInterface& body_interface = physicsSystem.get_body_interface();
	
	// create falling sphere (test)
	JPH::SphereShapeSettings sphere_shape_settings(1.0f);
	JPH::ShapeSettings::ShapeResult sphere_shape_result = sphere_shape_settings.Create();
	JPH::ShapeRefC sphere_shape = sphere_shape_result.Get();
	
	JPH::BodyCreationSettings sphere_settings(sphere_shape, JPH::RVec3(10.0, 200.0, 10.0), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Layers::MOVING);
	JPH::BodyID sphere_id = body_interface.CreateAndAddBody(sphere_settings, JPH::EActivation::Activate);
	
	// create ECS entity for the falling sphere
	sceneManager.get_world().entity("FallingSphere")
		.add<DynamicObject>()
		.set<LocalTransform>({ glm::translate(glm::mat4(1.0f), glm::vec3(0, 10, 0)) })
		.set<WorldTransform>({ glm::translate(glm::mat4(1.0f), glm::vec3(0, 10, 0)) })
		.set<MeshComponent>({ 5 }) // 5 = SM_Sphere
		.set<MaterialComponent>({ 0 })
		.set<PhysicsBody>({ sphere_id.GetIndexAndSequenceNumber() });
		

	// create static ground plane (box) - Jolt uses half-extents! So 50, 0.5, 50 is a 100x1x100 box
	JPH::BoxShapeSettings ground_shape_settings(JPH::Vec3(50.0f, 0.5f, 50.0f));
	JPH::ShapeSettings::ShapeResult ground_shape_result = ground_shape_settings.Create();
	JPH::ShapeRefC ground_shape = ground_shape_result.Get();
	
	JPH::BodyCreationSettings ground_settings(ground_shape, JPH::RVec3(0.0, -0.5, 0.0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
	JPH::BodyID ground_id = body_interface.CreateAndAddBody(ground_settings, JPH::EActivation::DontActivate);
	
	sceneManager.get_world().entity("GroundPlane")
		.add<StaticObject>()
		.set<LocalTransform>({ glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.5, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(100, 1, 100)) })
		.set<WorldTransform>({ glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.5, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(100, 1, 100)) })
		.set<PhysicsBody>({ ground_id.GetIndexAndSequenceNumber() });

	// optimise physics broadphase before first update
	physicsSystem.optimize_broad_phase();

	// textures
	std::vector<lut::Image> modelTextures;
	std::vector<lut::ImageView> modelTextureViews;


	for (auto const& tex : model.textures)
	{
		glfwPollEvents();

		VkFormat format = (tex.space == ETextureSpace::srgb)
			? VK_FORMAT_R8G8B8A8_SRGB
			: VK_FORMAT_R8G8B8A8_UNORM;


		//upload texture data to gpu memory
		modelTextures.emplace_back(lut::load_image_texture2d_from_memory(
			tex.pixels.data(),
			static_cast<uint32_t>(tex.width),
			static_cast<uint32_t>(tex.height),
			window, cpool.handle, allocator, format
		));

		//Create an imageview so the shader samplers can interpret the image data
		modelTextureViews.emplace_back(
			lut::create_image_view_texture2d(window, modelTextures.back().image, format)
		);
	}
	
	//just for objects without texture to set a default texture
	lut::Image defaultGrayTexture;
	lut::ImageView defaultGrayTextureView;
	{
		// RGBA: 128, 128, 128, 255 (grey)
		std::uint8_t pixelData[4] = { 128, 128, 128, 255 };

		// uploda 1x1 pixel to GPU
		defaultGrayTexture = lut::load_image_texture2d_from_memory(
			pixelData,
			1, 1,
			window, cpool.handle, allocator,
			VK_FORMAT_R8G8B8A8_UNORM
		);

		//create grey imageview
		defaultGrayTextureView = lut::create_image_view_texture2d(
			window,
			defaultGrayTexture.image,
			VK_FORMAT_R8G8B8A8_UNORM
		);
	}


	// sampler
	lut::Sampler defaultSampler = lut::create_default_sampler( window );
	lut::Sampler debugSampler = create_debug_sampler( window );

	lut::DescriptorPool dpool = lut::create_descriptor_pool( window );

	// material descriptors
	std::vector<VkDescriptorSet> materialDescriptors;
	for( auto const& material : model.materials )
	{
		VkDescriptorSet desc = lut::alloc_desc_set(window, dpool.handle, objectLayout.handle);

		//Base Color
		VkImageView baseColorView = defaultGrayTextureView.handle;
		if (material.baseColorTexture >= 0) {
			baseColorView = modelTextureViews[material.baseColorTexture].handle;
		}

		// 2. Roughness / Metalness
		//if roughness/metallic textures are missing, using gray (0.5 roughness/metal)
		VkImageView mrView = defaultGrayTextureView.handle;
		if (material.metalRoughTexture >= 0) {
			mrView = modelTextureViews[material.metalRoughTexture].handle;
		}

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = baseColorView; 
		imageInfo.sampler = defaultSampler.handle;

		VkDescriptorImageInfo roughInfo{};
		roughInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		roughInfo.imageView = mrView;        
		roughInfo.sampler = defaultSampler.handle;

		VkDescriptorImageInfo metalInfo{};
		metalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		metalInfo.imageView = mrView;        
		metalInfo.sampler = defaultSampler.handle;

		VkWriteDescriptorSet write[3]{};
		write[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write[0].dstSet = desc;
		write[0].dstBinding = 0;
		write[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write[0].descriptorCount = 1;
		write[0].pImageInfo = &imageInfo;

		write[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write[1].dstSet = desc;
		write[1].dstBinding = 1;
		write[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write[1].descriptorCount = 1;
		write[1].pImageInfo = &roughInfo;

		write[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write[2].dstSet = desc;
		write[2].dstBinding = 2;
		write[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write[2].descriptorCount = 1;
		write[2].pImageInfo = &metalInfo;

		vkUpdateDescriptorSets( window.device, 3, write, 0, nullptr );
	
			materialDescriptors.emplace_back( desc );
		}
	
		std::vector<VkDescriptorSet> debugMaterialDescriptors;
		for( auto const& material : model.materials )
		{
			VkDescriptorSet desc = lut::alloc_desc_set( window, dpool.handle, objectLayout.handle );
			
			VkImageView baseColorView = defaultGrayTextureView.handle;
			if (material.baseColorTexture >= 0) baseColorView = modelTextureViews[material.baseColorTexture].handle;

			VkImageView mrView = defaultGrayTextureView.handle;
			if (material.metalRoughTexture >= 0) mrView = modelTextureViews[material.metalRoughTexture].handle;

			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = baseColorView; 
			imageInfo.sampler = debugSampler.handle;

			VkDescriptorImageInfo roughInfo{};
			roughInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			roughInfo.imageView = mrView;        
			roughInfo.sampler = debugSampler.handle;

			VkDescriptorImageInfo metalInfo{};
			metalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			metalInfo.imageView = mrView;       
			metalInfo.sampler = debugSampler.handle;
	
			VkWriteDescriptorSet write[3]{};
			write[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write[0].dstSet = desc;
			write[0].dstBinding = 0;
			write[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write[0].descriptorCount = 1;
			write[0].pImageInfo = &imageInfo;
	
			write[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write[1].dstSet = desc;
			write[1].dstBinding = 1;
			write[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write[1].descriptorCount = 1;
			write[1].pImageInfo = &roughInfo;
	
			write[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write[2].dstSet = desc;
			write[2].dstBinding = 2;
			write[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write[2].descriptorCount = 1;
			write[2].pImageInfo = &metalInfo;
	
			vkUpdateDescriptorSets( window.device, 3, write, 0, nullptr );
	
			debugMaterialDescriptors.emplace_back( desc );
		}

	// meshes
	std::vector<lut::Buffer> meshPositions;
	std::vector<lut::Buffer> meshTexCoords;
	std::vector<lut::Buffer> meshNormals;
	std::vector<lut::Buffer> meshIndices;

	// Mesh upload: use a single command buffer for all uploads to avoid
	// stalling the pipeline with hundreds of submissions.
	VkCommandBuffer uploadCmd = lut::alloc_command_buffer( window, cpool.handle );
	
	VkCommandBufferBeginInfo uploadBeginInfo{};
	uploadBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	uploadBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	
	if( auto const res = vkBeginCommandBuffer( uploadCmd, &uploadBeginInfo ); VK_SUCCESS != res )
		throw lut::Error( "Beginning upload command buffer: {}", lut::to_string(res) );

	// Keep staging buffers alive until submit is complete
	std::vector<lut::Buffer> stagingBuffers;

	for( auto const& mesh : model.meshes )
	{
		// Poll events to keep window responsive
		glfwPollEvents();

		meshPositions.emplace_back( lut::create_buffer(
			allocator,
			mesh.positions.size() * sizeof(glm::vec3),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
		));

		meshTexCoords.emplace_back( lut::create_buffer(
			allocator,
			mesh.texcoords.size() * sizeof(glm::vec2),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
		));

		meshNormals.emplace_back( lut::create_buffer(
			allocator,
			mesh.normals.size() * sizeof(glm::vec3),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
		));

		meshIndices.emplace_back( lut::create_buffer(
			allocator,
			mesh.indices.size() * sizeof(std::uint32_t),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
		));

		// upload data (using staging buffers)
		lut::Buffer posStaging = lut::create_buffer(
			allocator,
			mesh.positions.size() * sizeof(glm::vec3),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		);

		lut::Buffer texStaging = lut::create_buffer(
			allocator,
			mesh.texcoords.size() * sizeof(glm::vec2),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		);

		lut::Buffer normStaging = lut::create_buffer(
			allocator,
			mesh.normals.size() * sizeof(glm::vec3),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		);

		lut::Buffer idxStaging = lut::create_buffer(
			allocator,
			mesh.indices.size() * sizeof(std::uint32_t),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		);

		void* posPtr = nullptr;
		vmaMapMemory( allocator.allocator, posStaging.allocation, &posPtr );
		std::memcpy( posPtr, mesh.positions.data(), mesh.positions.size() * sizeof(glm::vec3) );
		vmaUnmapMemory( allocator.allocator, posStaging.allocation );

		void* texPtr = nullptr;
		vmaMapMemory( allocator.allocator, texStaging.allocation, &texPtr );
		std::memcpy( texPtr, mesh.texcoords.data(), mesh.texcoords.size() * sizeof(glm::vec2) );
		vmaUnmapMemory( allocator.allocator, texStaging.allocation );

		void* normPtr = nullptr;
		vmaMapMemory( allocator.allocator, normStaging.allocation, &normPtr );
		std::memcpy( normPtr, mesh.normals.data(), mesh.normals.size() * sizeof(glm::vec3) );
		vmaUnmapMemory( allocator.allocator, normStaging.allocation );

		void* idxPtr = nullptr;
		vmaMapMemory( allocator.allocator, idxStaging.allocation, &idxPtr );
		std::memcpy( idxPtr, mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t) );
		vmaUnmapMemory( allocator.allocator, idxStaging.allocation );

		VkBufferCopy posCopy{};
		posCopy.size = mesh.positions.size() * sizeof(glm::vec3);
		vkCmdCopyBuffer( uploadCmd, posStaging.buffer, meshPositions.back().buffer, 1, &posCopy );

		VkBufferCopy texCopy{};
		texCopy.size = mesh.texcoords.size() * sizeof(glm::vec2);
		vkCmdCopyBuffer( uploadCmd, texStaging.buffer, meshTexCoords.back().buffer, 1, &texCopy );

		VkBufferCopy normCopy{};
		normCopy.size = mesh.normals.size() * sizeof(glm::vec3);
		vkCmdCopyBuffer( uploadCmd, normStaging.buffer, meshNormals.back().buffer, 1, &normCopy );

		VkBufferCopy idxCopy{};
		idxCopy.size = mesh.indices.size() * sizeof(std::uint32_t);
		vkCmdCopyBuffer( uploadCmd, idxStaging.buffer, meshIndices.back().buffer, 1, &idxCopy );
		
		// Add buffer barriers for vertex attributes
		lut::buffer_barrier( uploadCmd, meshPositions.back().buffer,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
			VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT
		);

		lut::buffer_barrier( uploadCmd, meshTexCoords.back().buffer,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
			VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT
		);

		lut::buffer_barrier( uploadCmd, meshNormals.back().buffer,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
			VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT
		);

		lut::buffer_barrier( uploadCmd, meshIndices.back().buffer,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
			VK_ACCESS_2_INDEX_READ_BIT
		);

		stagingBuffers.emplace_back( std::move(posStaging) );
		stagingBuffers.emplace_back( std::move(texStaging) );
		stagingBuffers.emplace_back( std::move(normStaging) );
		stagingBuffers.emplace_back( std::move(idxStaging) );
	}

	if( auto const res = vkEndCommandBuffer( uploadCmd ); VK_SUCCESS != res )
		throw lut::Error( "Ending upload command buffer: {}", lut::to_string(res) );

	VkCommandBufferSubmitInfo cmdInfo{};
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.commandBuffer = uploadCmd;

	VkSubmitInfo2 uploadSubmitInfo{};
	uploadSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	uploadSubmitInfo.commandBufferInfoCount = 1;
	uploadSubmitInfo.pCommandBufferInfos = &cmdInfo;

	if( auto const res = vkQueueSubmit2( window.graphicsQueue, 1, &uploadSubmitInfo, VK_NULL_HANDLE ); VK_SUCCESS != res )
		throw lut::Error( "Submitting upload command buffer: {}", lut::to_string(res) );
	
	// Wait for uploads to finish before destroying staging buffers
	if( auto const res = vkQueueWaitIdle( window.graphicsQueue ); VK_SUCCESS != res )
		throw lut::Error( "Waiting for upload completion: {}", lut::to_string(res) );

	lut::Buffer sceneUBO = lut::create_buffer(
		allocator,
		sizeof(glsl::SceneUniform),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		0,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);

	// lut::DescriptorPool dpool = lut::create_descriptor_pool( window );

	VkDescriptorSet sceneDescriptors = lut::alloc_desc_set( window, dpool.handle, sceneLayout.handle );

	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = sceneUBO.buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet desc[1]{};
		desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc[0].dstSet = sceneDescriptors;
		desc[0].dstBinding = 0;
		desc[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		desc[0].descriptorCount = 1;
		desc[0].pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets( window.device, 1, desc, 0, nullptr );
	}


	lut::Pipeline alphaPipe = create_alpha_pipeline( window, pipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT );

	// p2_1.5 Shadow Resources
	lut::ImageWithView shadowMap = create_shadow_map( window, allocator );
	// 新增：创建 4 个单层 ImageView 用于渲染循环
	std::vector<VkImageView> shadowCascadeViews;
	for (uint32_t i = 0; i < kCascadeCount; ++i) {
		VkImageViewCreateInfo vInfo{};
		vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vInfo.image = shadowMap.image;
		vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // 必须是 2D 才能绑定为渲染附件
		vInfo.format = cfg::kShadowMapFormat;
		vInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		vInfo.subresourceRange.baseMipLevel = 0;
		vInfo.subresourceRange.levelCount = 1;
		vInfo.subresourceRange.baseArrayLayer = i; // 关键：指向第 i 层
		vInfo.subresourceRange.layerCount = 1;

		VkImageView cView = VK_NULL_HANDLE;
		if (vkCreateImageView(window.device, &vInfo, nullptr, &cView) != VK_SUCCESS) {
			throw lut::Error("Failed to create shadow cascade view");
		}
		shadowCascadeViews.push_back(cView);
	}
	lut::Sampler shadowSampler = create_shadow_sampler( window );
	lut::Pipeline shadowPipe = create_shadow_pipeline( window, pipeLayout.handle );

	lut::ImageWithView depthBuffer = create_depth_buffer( window, allocator );
	
	lut::Pipeline postProcPipe = create_post_proc_pipeline( window, postProcPipelineLayout.handle, postProcLayout.handle );
	lut::ImageWithView offscreenImage = create_offscreen_buffer( window, allocator );
	lut::ImageWithView visImage = create_vis_image( window, allocator ); // p2_1.1
	lut::Sampler postProcSampler = create_post_proc_sampler( window );

	// mosaic UBOs
	std::vector<lut::Buffer> mosaicUBOs;
	// main scene descriptors need shadow map
	// update scene descriptors
	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = sceneUBO.buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorImageInfo shadowInfo{};
		shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
		shadowInfo.imageView = shadowMap.view;
		shadowInfo.sampler = shadowSampler.handle;

		VkWriteDescriptorSet desc[2]{};
		desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc[0].dstSet = sceneDescriptors;
		desc[0].dstBinding = 0;
		desc[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		desc[0].descriptorCount = 1;
		desc[0].pBufferInfo = &bufferInfo;

		desc[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc[1].dstSet = sceneDescriptors;
		desc[1].dstBinding = 1; // shadow map binding
		desc[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc[1].descriptorCount = 1;
		desc[1].pImageInfo = &shadowInfo;


		vkUpdateDescriptorSets( window.device, 2, desc, 0, nullptr );
		
	}

	std::vector<VkDescriptorSet> postProcDescriptors;
	std::vector<VkDescriptorSet> visDescriptors; // p2_1.1

	for( std::size_t i = 0; i < cbuffers.size(); ++i )
	{
		mosaicUBOs.emplace_back( lut::create_buffer(
			allocator,
			sizeof(glsl::MosaicUniform),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT 
		));

		VkDescriptorSet desc = lut::alloc_desc_set( window, dpool.handle, postProcLayout.handle );

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = offscreenImage.view;
		imageInfo.sampler = postProcSampler.handle;

		VkDescriptorBufferInfo buffInfo{};
		buffInfo.buffer = mosaicUBOs.back().buffer;
		buffInfo.offset = 0;
		buffInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet write[2]{};
		write[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write[0].dstSet = desc;
		write[0].dstBinding = 0;
		write[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write[0].descriptorCount = 1;
		write[0].pImageInfo = &imageInfo;

		write[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write[1].dstSet = desc;
		write[1].dstBinding = 1; // Mosaic UBO Binding
		write[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write[1].descriptorCount = 1;
		write[1].pBufferInfo = &buffInfo;

		vkUpdateDescriptorSets( window.device, 2, write, 0, nullptr );

		postProcDescriptors.emplace_back( desc );

		// p2 1.1: vis descriptors
		// reuse postProcLayout (2 bindings) but passthrough shader only uses binding 0
		VkDescriptorSet visDesc = lut::alloc_desc_set( window, dpool.handle, postProcLayout.handle );
		
		VkDescriptorImageInfo visImageInfo{};
		visImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		visImageInfo.imageView = visImage.view;
		visImageInfo.sampler = postProcSampler.handle;

		VkWriteDescriptorSet visWrite[2]{};
		visWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		visWrite[0].dstSet = visDesc;
		visWrite[0].dstBinding = 0;
		visWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		visWrite[0].descriptorCount = 1;
		visWrite[0].pImageInfo = &visImageInfo;

		visWrite[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		visWrite[1].dstSet = visDesc;
		visWrite[1].dstBinding = 1;
		visWrite[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		visWrite[1].descriptorCount = 1;
		visWrite[1].pBufferInfo = &buffInfo; // Reusing mosaic UBO buffer info

		vkUpdateDescriptorSets( window.device, 2, visWrite, 0, nullptr );
		visDescriptors.emplace_back( visDesc );
	}

	// Application main loop
	bool recreateSwapchain = false;



	auto previousClock = Clock_::now();

	while( !glfwWindowShouldClose( window.window ) )
	{
		// Let GLFW process events.
		// glfwPollEvents() checks for events, processes them. If there are no
		// events, it will return immediately. Alternatively, glfwWaitEvents()
		// will wait for any event to occur, process it, and only return at
		// that point. The former is useful for applications where you want to
		// render as fast as possible, whereas the latter is useful for
		// input-driven applications, where redrawing is only needed in
		// reaction to user input (or similar).
		glfwPollEvents(); // or: glfwWaitEvents()

		

		

		if( recreateSwapchain )
		{
			// We need to destroy several objects, which may still be in use by the GPU
			vkDeviceWaitIdle( window.device );

			// Recreate them
			auto const changes = lut::recreate_swapchain( window );

			if( changes.changedFormat )
			{
				pipe = create_triangle_pipeline( window, pipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT );
				alphaPipe = create_alpha_pipeline( window, pipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT );

				mipPipe = create_debug_pipeline( window, pipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugMipFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT );
				depthPipe = create_debug_pipeline( window, pipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDepthFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT );
				derivPipe = create_debug_pipeline( window, pipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDerivFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT );
				
				postProcPipe = create_post_proc_pipeline( window, postProcPipelineLayout.handle, postProcLayout.handle );

				// Recreate (p2_1.1)
				overdrawPipe = create_overdraw_pipeline( window, pipeLayout.handle, VK_FORMAT_R8G8B8A8_UNORM );
				overshadingPipe = create_overshading_pipeline( window, pipeLayout.handle, VK_FORMAT_R8G8B8A8_UNORM );
				visResolvePipe = create_vis_resolve_pipeline( window, postProcPipelineLayout.handle, postProcLayout.handle );
			}

			if( changes.changedSize )
			{
				depthBuffer = create_depth_buffer( window, allocator );
				offscreenImage = create_offscreen_buffer( window, allocator );
				visImage = create_vis_image( window, allocator );

				// Update descriptor set
				VkDescriptorImageInfo imageInfo{};
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageInfo.imageView = offscreenImage.view;
				imageInfo.sampler = postProcSampler.handle;

				for( auto ds : postProcDescriptors )
				{
					VkWriteDescriptorSet desc{};
					desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					desc.dstSet = ds; 
					desc.dstBinding = 0;
					desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					desc.descriptorCount = 1;
					desc.pImageInfo = &imageInfo;

					vkUpdateDescriptorSets( window.device, 1, &desc, 0, nullptr );
				}

				// p2 1.1: update vis descriptors
				VkDescriptorImageInfo visInfo{};
				visInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				visInfo.imageView = visImage.view;
				visInfo.sampler = postProcSampler.handle;

				for( auto ds : visDescriptors )
				{
					VkWriteDescriptorSet desc{};
					desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					desc.dstSet = ds; 
					desc.dstBinding = 0;
					desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					desc.descriptorCount = 1;
					desc.pImageInfo = &visInfo;

					vkUpdateDescriptorSets( window.device, 1, &desc, 0, nullptr );
				}
			}

			recreateSwapchain = false;
			continue;
		}

		// Advance to next frame
		frameIndex++;
		frameIndex %= cbuffers.size();

		// Make sure that the frame resources are no longer in use
		assert( frameIndex < frameDone.size() );

		if( auto const res = vkWaitForFences( window.device, 1, &frameDone[frameIndex].handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max() ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to wait for frame fence {}\n"
				"vkWaitForFences() returned {}", frameIndex, lut::to_string(res)
			);
		}

		// Acquire next swap chain image
		assert( frameIndex < imageAvailable.size() );

		std::uint32_t imageIndex = 0;
		auto const acquireRes = vkAcquireNextImageKHR(
			window.device,
			window.swapchain,
			std::numeric_limits<std::uint64_t>::max(),
			imageAvailable[frameIndex].handle,
			VK_NULL_HANDLE,
			&imageIndex
		);

		if( VK_SUBOPTIMAL_KHR == acquireRes || VK_ERROR_OUT_OF_DATE_KHR == acquireRes )
		{
			recreateSwapchain = true;
			--frameIndex;
			frameIndex %= cbuffers.size();

			continue;
		}

		if( VK_SUCCESS != acquireRes )
		{
			throw lut::Error( "Unable to acquire next swapchain image\n"
				"vkAcquireNextImageKHR() returned {}", lut::to_string(acquireRes)
			);
		}

		// Reset fence
		if( auto const res = vkResetFences( window.device, 1, &frameDone[frameIndex].handle ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to reset frame fence {}\n"
				"vkResetFences() returned {}", frameIndex, lut::to_string(res)
			);
		}

		// Update state
		auto const now = Clock_::now();
		auto  dt = std::chrono::duration_cast<Secondsf_>(now-previousClock).count();
		previousClock = now;

		update_user_state( state, dt );
		physicsSystem.update(dt);
		// 更新 ECS 内部系统（处理层级变换等）
		sceneManager.update(dt, &physicsSystem);


		auto batches = sceneManager.get_render_batches();


		// Record and submit commands for this frame
		assert( std::size_t(imageIndex) < window.swapImages.size() );

		ImageAndView colorTarget;
		colorTarget.image = window.swapImages[imageIndex];
		colorTarget.view = window.swapViews[imageIndex];

		ImageAndView depthTarget;
		depthTarget.image = depthBuffer.image;
		depthTarget.view = depthBuffer.view;

		assert( std::size_t(frameIndex) < cbuffers.size() );

		// Prepare data for this frame
		glsl::SceneUniform sceneUniforms{};
		update_scene_uniforms( sceneUniforms, window.swapchainExtent.width, window.swapchainExtent.height, state );

		VkPipeline currentOpaque = pipe.handle;
		VkPipeline currentAlpha = alphaPipe.handle;
		auto const* currentDescriptors = &materialDescriptors;

		// Task 1.4
		// Debug Visualization Pipeline Switching
		// keys 1-4: switch the pipeline used for drawing
		// Sitch the descriptor set to 'debugMaterialDescriptors'
		// because the debug pipeline requires a sampler with anisotropic filtering DISABLED
		// setup.cpp
		if( state.renderMode == 1 ) // Mode 1: Mipmap Visualization
		{
			// Visualizes texture LOD levels (colored).
			currentOpaque = mipPipe.handle;
			currentAlpha = mipPipe.handle; 
			currentDescriptors = &debugMaterialDescriptors;
		}
		else if( state.renderMode == 2 ) // Mode 2: Depth Visualization
		{
			// Visualizes fragment depth (non-linear grayscale)
			currentOpaque = depthPipe.handle;
			currentAlpha = depthPipe.handle;
			currentDescriptors = &debugMaterialDescriptors;
		}
		else if( state.renderMode == 3 ) // Mode 3: Derivatives Visualization
		{
			// Visualizes partial derivatives of depth (dFdx, dFdy)
			currentOpaque = derivPipe.handle;
			currentAlpha = derivPipe.handle;
			currentDescriptors = &debugMaterialDescriptors;
		}
		else if( state.renderMode == 4 ) // Mode 4: Overdraw
		{
			currentOpaque = overdrawPipe.handle;
			currentAlpha = overdrawPipe.handle;
			currentDescriptors = &debugMaterialDescriptors;
			// rendering.cpp binds descriptors (i think)
		}
		else if( state.renderMode == 5 ) // Mode 5: Overshading
		{
			currentOpaque = overshadingPipe.handle;
			currentAlpha = overshadingPipe.handle;
			currentDescriptors = &debugMaterialDescriptors;
		}

		ImageAndView offscreenTarget;

		VkPipeline resolvePipeline = postProcPipe.handle;
		VkDescriptorSet resolveDescriptors = postProcDescriptors[frameIndex];
		VkPipelineLayout resolveLayout = postProcPipelineLayout.handle;
		VkClearColorValue clearColor = { 0.1f, 0.1f, 0.1f, 1.f };

		if( state.renderMode == 4 || state.renderMode == 5 )
		{
			// Visualization Mode
			offscreenTarget.image = visImage.image;
			offscreenTarget.view = visImage.view;
			
			resolvePipeline = visResolvePipe.handle;
			resolveDescriptors = visDescriptors[frameIndex];
			// same layout (postProcPipelineLayout)
			
			clearColor = { 0.0f, 0.1f, 0.0f, 1.0f }; // dark green
		}
		else
		{
			// Normal Mode
			offscreenTarget.image = offscreenImage.image;
			offscreenTarget.view = offscreenImage.view;
		}

		ImageAndView shadowTarget;
		shadowTarget.image = shadowMap.image;
		shadowTarget.view = shadowMap.view;

		


		record_commands(
			cbuffers[frameIndex],
			currentOpaque,
			currentAlpha,
			colorTarget,
			depthTarget,
			window.swapchainExtent,
			sceneUBO.buffer,
			sceneUniforms,
			pipeLayout.handle,
			sceneDescriptors,
			meshPositions,
			meshTexCoords,
			meshNormals,
			meshIndices,
			model.meshes,
			model.materials,
			*currentDescriptors,
			//model.scenes,
			batches,
			resolvePipeline,
			resolveDescriptors,
			resolveLayout,
			offscreenTarget,
			clearColor,
			shadowPipe.handle,
			shadowTarget,
			shadowCascadeViews
		);
		
		// update mosaic ubo
		{
			glsl::MosaicUniform mosaicInfo{};
			mosaicInfo.mosaicOn = state.mosaicEnabled ? 1 : 0;
			
			void* data = nullptr;
			vmaMapMemory( allocator.allocator, mosaicUBOs[frameIndex].allocation, &data );
			std::memcpy( data, &mosaicInfo, sizeof(glsl::MosaicUniform) );
			vmaUnmapMemory( allocator.allocator, mosaicUBOs[frameIndex].allocation );
		}


		assert( std::size_t(frameIndex) < renderFinished.size() );

		submit_commands(
			window,
			cbuffers[frameIndex],
			frameDone[frameIndex].handle,
			imageAvailable[frameIndex].handle,
			renderFinished[frameIndex].handle
		);

		present_results(
			window.presentQueue,
			window.swapchain,
			imageIndex,
			renderFinished[frameIndex].handle,
			recreateSwapchain
		);
	}

	// Cleanup takes place automatically in the destructors, but we sill need
	// to ensure that all Vulkan commands have finished before that.
	vkDeviceWaitIdle( window.device );
	for (auto view : shadowCascadeViews) {
		vkDestroyImageView(window.device, view, nullptr);
	}

	return 0;
}
catch( std::exception const& eErr )
{
	std::print( stderr, "\n" );
	std::print( stderr, "Error: {}\n", eErr.what() );
	return 1;
}



//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
