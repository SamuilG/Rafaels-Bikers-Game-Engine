#include "descriptors.hpp"

#include "error.hpp"
#include "to_string.hpp"

// SOLUTION_TAGS: vulkan-(ex-[^123]|cw-.)

namespace labut2
{

    DescriptorPool create_descriptor_pool( VulkanContext const& aContext, std::uint32_t aMaxDescriptors, std::uint32_t aMaxSets)
    {   // create descriptor pool
        
		VkDescriptorPoolSize const pools[] = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, aMaxDescriptors },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, aMaxDescriptors }
		};

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets        = aMaxSets;
		poolInfo.poolSizeCount  = sizeof(pools)/sizeof(pools[0]);
		poolInfo.pPoolSizes     = pools;

		VkDescriptorPool pool = VK_NULL_HANDLE;
		// create pool
		if( auto const res = vkCreateDescriptorPool( aContext.device, &poolInfo, nullptr, &pool ); VK_SUCCESS != res )
		{
			throw Error( "Unable to create descriptor pool\n"
				"vkCreateDescriptorPool() returned {}", to_string(res)
			);
		}

		// wrap and return
		return DescriptorPool( aContext.device, pool );
	}

    VkDescriptorSet alloc_desc_set( VulkanContext const& aContext, VkDescriptorPool aPool, VkDescriptorSetLayout aSetLayout )
    {   // allocate descriptor set

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool      = aPool;
		allocInfo.descriptorSetCount  = 1;
		allocInfo.pSetLayouts         = &aSetLayout;

		VkDescriptorSet dset = VK_NULL_HANDLE;
		// allocate set
		if( auto const res = vkAllocateDescriptorSets( aContext.device, &allocInfo, &dset ); VK_SUCCESS != res )
		{
			throw Error( "Unable to allocate descriptor set\n"
				"vkAllocateDescriptorSets() returned {}", to_string(res)
			);
		}

		return dset;
	}


}
