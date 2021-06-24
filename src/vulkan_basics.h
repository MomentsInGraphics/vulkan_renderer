//  Copyright (C) 2021, Christoph Peters, Karlsruhe Institute of Technology
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.


#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdint.h>
#include <stddef.h>

/*! This macro initializes a function pointer for the Vulkan function with the
	given name. It uses GLFW to find it. The surrounding scope must have a
	device_t* device with a valid instance. The identifier of the function
	pointer is the function name, prefixed by p.*/
#define VK_LOAD(FUNCTION_NAME) PFN_##FUNCTION_NAME p##FUNCTION_NAME = (PFN_##FUNCTION_NAME) glfwGetInstanceProcAddress(device->instance, #FUNCTION_NAME);

/*! Holds Vulkan objects that are created up to device creation. This includes
	the instance, the physical device and the device. It depends on the choice
	of extensions and devices but not on a window or a resolution.*/
typedef struct device_s {
	//! Number of loaded extensions for the instance
	uint32_t instance_extension_count;
	//! Names of loaded extensions for the instance
	const char** instance_extension_names;
	//! Number of loaded extensions for the device
	uint32_t device_extension_count;
	//! Names of loaded extensions for the device
	const char** device_extension_names;
	//! Boolean indicating whether ray tracing is available with the device
	VkBool32 ray_tracing_supported;

	//! Number of available physical devices
	uint32_t physical_device_count;
	//! List of available physical devices
	VkPhysicalDevice* physical_devices;
	//! The physical device that is being used
	VkPhysicalDevice physical_device;
	//! Properties of the used physical device
	VkPhysicalDeviceProperties physical_device_properties;
	//! Information about memory available from the used physical device
	VkPhysicalDeviceMemoryProperties memory_properties;

	//! This object makes Vulkan functions available
	VkInstance instance;
	//! The Vulkan device object for the physical device above
	VkDevice device;

	//! Number of available queue families
	uint32_t queue_family_count;
	//! Properties for each available queue
	VkQueueFamilyProperties* queue_family_properties;
	//! Index of a queue family that supports graphics and compute
	uint32_t queue_family_index;
	//! A queue based on queue_family_index
	VkQueue queue;
	//! A command pool for queue
	VkCommandPool command_pool;
} device_t;


/*! Holds Vulkan objects that are related to the swapchain. This includes the
	swapchain itself, the window and image views for images in the swapchain.
	It depends on the device and is changed substantially whenever the
	resolution changes.*/
typedef struct swapchain_s {
	//! The extent of the images in the swapchain in pixels (i.e. the
	//! resolution on screen)
	VkExtent2D extent;
	//! The window containing the swapchain
	GLFWwindow* window;
	//! A surface created within this window
	VkSurfaceKHR surface;
	//! Number of available surface formats for the surface above
	uint32_t surface_format_count;
	//! List of available surface formats for the surface above
	VkSurfaceFormatKHR* surface_formats;
	//! The format of the held images
	VkFormat format;
	//! Number of available presentation modes
	uint32_t present_mode_count;
	//! List of available presentation modes
	VkPresentModeKHR* present_modes;
	//! The swapchain created within the window. NULL if the window was
	//! minimized during the last resize.
	VkSwapchainKHR swapchain;
	//! Number of images in the swapchain
	uint32_t image_count;
	//! List of images in the swapchain
	VkImage* images;
	//! An image view for each image of the swapchain
	VkImageView* image_views;
} swapchain_t;


/*! The information needed to request construction of an image.*/
typedef struct image_request_s {
	/*! Complete image creation info. If the number of mip levels is set to
		zero, it will be automatically set using get_mipmap_count_3d().*/
	VkImageCreateInfo image_info;
	/*! Description of the view that is to be created. format and image do not
		need to be set. If the layer count or mip count are zero, they are set
		to match the corresponding values of the image. If sType is not 
		VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, creation of a view is
		skipped.*/
	VkImageViewCreateInfo view_info;
} image_request_t;


/*! This structure combines a Vulkan image object, with meta-data and the view.
	The memory allocation is handled elsewhere, typically by an images_t.*/
typedef struct image_s {
	//! The creation info used to create image
	VkImageCreateInfo image_info;
	//! The creation info used to create view
	VkImageViewCreateInfo view_info;
	//! The Vulkan object for the image
	VkImage image;
	//! A view onto the contents of this image or NULL if no view was requested
	VkImageView view;
	//! The offset of this image within the used memory allocation
	VkDeviceSize memory_offset;
	//! The required size of the memory allocation for this image in bytes. It
	//! may be larger than the image data itself.
	VkDeviceSize memory_size;
	//! Non-zero iff this image has a dedicated memory allocation
	uint32_t dedicated_allocation;
	//! The index of the memory allocation that is bound to this image
	uint32_t memory_index;
} image_t;


/*! This object handles a list of Vulkan images among with the corresponding
	memory allocations.*/
typedef struct images_s {
	//! Number of held images
	uint32_t image_count;
	//! The held images
	image_t* images;
	//! The number of used device memory allocations
	uint32_t memory_count;
	/*! The memory allocations used to store the images. The intent is that all
		images share one allocation, except for those which prefer dedicated
		allocatins.*/
	VkDeviceMemory* memories;
	/*! The memory properties that have to be suported for the memory
		allocations. Combination of VkMemoryPropertyFlagBits.*/
	VkMemoryPropertyFlags memory_properties;
} images_t;


//! Combines a buffer handle with offset and size
typedef struct buffer_s {
	//! The buffer handle
	VkBuffer buffer;
	//! The offset in the bound memory allocation in bytes
	VkDeviceSize offset;
	//! The size of this buffer without padding in bytes
	VkDeviceSize size;
} buffer_t;


//! A list of buffers that all share a single memory allocation
typedef struct buffers_s {
	//! Number of held buffers
	uint32_t buffer_count;
	//! Array of buffer_count buffers
	buffer_t* buffers;
	//! The memory allocation that serves all of the buffers
	VkDeviceMemory memory;
	//! The size in bytes of the whole memory allocation
	VkDeviceSize size;
} buffers_t;


//! Handles all information needed to compile a shader into a module
typedef struct shader_request_s {
	//! A path to the file with the GLSL source code (relative to the CWD)
	char* shader_file_path;
	//! The director(ies) which are searched for includes
	char* include_path;
	//! The name of the function that serves as entry point
	char* entry_point;
	//! A single bit from VkShaderStageFlagBits to indicate the targeted shader
	//! stage
	VkShaderStageFlags stage;
	//! Number of defines
	uint32_t define_count;
	//! A list of strings providing the defines, either as "IDENTIFIER" or
	//! "IDENTIFIER=VALUE". Do not use white space, these strings go into the
	//! command line unmodified.
	char** defines;
} shader_request_t;


//! Bundles a Vulkan shader module with its SPIRV code
typedef struct shader_s {
	//! The Vulkan shader module
	VkShaderModule module;
	//! The size of the compiled SPIRV code in bytes
	size_t spirv_size;
	//! The compiled SPIRV code
	uint32_t* spirv_code;
} shader_t;


/*! This structure combines a pipeline state object with everything that is
	needed to construct descriptor sets for it and the descriptor sets
	themselves.*/
typedef struct pipeline_with_bindings_s {
	//! Descriptor layout used by pipeline_layout
	VkDescriptorSetLayout descriptor_set_layout;
	//! Pipeline layout used by pipeline
	VkPipelineLayout pipeline_layout;
	//! Descriptor pool used to allocate descriptor_sets. It matches
	//! descriptor_set_layout.
	VkDescriptorPool descriptor_pool;
	//! An array with one descriptor set per swapchain image
	VkDescriptorSet* descriptor_sets;
	//! The pipeline state
	VkPipeline pipeline;
} pipeline_with_bindings_t;


//! Specifies a single descriptor layout and a number of 
typedef struct descriptor_set_request_s {
	//! The stageFlags member of each entry of bindings is ORed with this value
	//! before using it
	VkShaderStageFlagBits stage_flags;
	//! The minimal number of descriptors per binding. Setting this to one is a
	//! good way to avoid some redundant specifications.
	uint32_t min_descriptor_count;
	//! Number of entries in bindings
	uint32_t binding_count;
	//! A specification of the bindings in the layout. The member binding is
	//! overwritten by the array index before use, stageFlags is ORed with
	//! stage_flags and descriptorCount clamped to a minimum of
	//! min_descriptor_count.
	VkDescriptorSetLayoutBinding* bindings;
} descriptor_set_request_t;


/*! Creates all Vulkan objects that are created up to device creation. This
	includes the instance, the physical device and the device. It depends on
	the choice of extensions and devices but not on a window or a resolution.
	\param device The output structure. The calling side has to invoke
		destroy_vulkan_device() if the call succeeded.
	\param application_internal_name The name of the application that is
		advertised to Vulkan.
	\param physical_device_index The index of the physical device that is to
		be used in the list produced by vkEnumeratePhysicalDevices().
	\param request_ray_tracing Whether you want a device that supports ray
		tracing. If the physical device, does not support it, device creation
		still succeeds. Check device->ray_tracing_supported for the outcome.
	\return 0 indicates success. Upon failure, device is zeroed.*/
int create_vulkan_device(device_t* device, const char* application_internal_name, uint32_t physical_device_index, VkBool32 request_ray_tracing);

/*! Destroys a device that has been created successfully by
	create_vulkan_device().
	\param device The device that is to be destroyed and zeroed.*/
void destroy_vulkan_device(device_t* device);


/*! Creates Vulkan objects that are related to the swapchain. This includes the
	swapchain itself, the window, various buffers and image views. It depends
	on the device and is changed substantially whenever the resolution changes.
	Upon resize, the window is kept and shortcuts are used.
	\param swapchain The output structure and for resizes the existing
		swapchain. The calling side has to invoke destroy_swapchain()
		if the call succeeded.
	\param device A successfully created device.
	\param resize Whether to perform a resize (VK_TRUE) or first
		initialization.
	\param application_display_name The display name of the application, which
		is used as Window title. Irrelevant for resize.
	\param width, height The dimensions of the client area of the created
		window and thus the swapchain. The window manager may not respect this
		request, i.e. the actual resolution may differ. Irrelevant for resize.
	\param use_vsync 1 to use vertical synchronization, 0 to render as fast as
		possible (always do this for profiling).
	\return 0 indicates success. 1 upon failure, in which case swapchain is
		destroyed. 2 to indicate that the window is minimized. Swapchain
		resize can be successful once that changes.*/
int create_or_resize_swapchain(swapchain_t* swapchain, const device_t* device, VkBool32 resize, 
	const char* application_display_name, uint32_t width, uint32_t height, VkBool32 use_vsync);

//! Returns the aspect ratio, i.e. width / height for the given swapchain.
static inline float get_aspect_ratio(const swapchain_t* swapchain) {
	return ((float) swapchain->extent.width) / ((float) swapchain->extent.height);
}

/*! Destroys a swapchain that has been created successfully by
	create_swapchain().
	\param swapchain The swapchain that is to be destroyed and zeroed.
	\param device The device that has been used to construct the swapchain.*/
void destroy_swapchain(swapchain_t* swapchain, const device_t* device);


/*! Goes through memory types available from device and identifies the lowest
	index that satisfies all given requirements. 
	\param memory_type_bits A bit mask indicating which memory type indices are
		admissible. Available from VkMemoryRequirements.
	\param property_mask A combination of VkMemoryPropertyFlagBits.
	\return 0 if type_index was set to a valid reply, 1 if no compatible memory
		is available.*/
int find_memory_type(uint32_t* type_index, const device_t* device, uint32_t memory_type_bits, VkMemoryPropertyFlags property_mask);

/*! Returns the smallest number that is greater equal offset and a multiple of
	the given positive integer. Useful for memory alignment.*/
static inline VkDeviceSize align_memory_offset(VkDeviceSize offset, VkDeviceSize alignment) {
	return ((offset + alignment - 1) / alignment) * alignment;
}


/*! This utility function computes the number of mipmap levels needed to get
	from a resource of the given size to one texel. This is the maximal number
	of mipmaps that can be created.*/
static inline uint32_t get_mipmap_count_1d(uint32_t width) {
	int32_t padded_width = (int32_t)(2 * width - 1);
	uint32_t mipmap_count = 0;
	while (padded_width > 0) {
		padded_width &= 0x7ffffffe;
		padded_width >>= 1;
		++mipmap_count;
	}
	return mipmap_count;
}

//! \return The maximum of get_mipmap_count_1d() for all given extents
static inline uint32_t get_mipmap_count_3d(VkExtent3D extent) {
	uint32_t counts[3] = {
		get_mipmap_count_1d(extent.width),
		get_mipmap_count_1d(extent.height),
		get_mipmap_count_1d(extent.depth)
	};
	uint32_t result = counts[0];
	result = (result < counts[1]) ? counts[1] : result;
	result = (result < counts[2]) ? counts[2] : result;
	return result;
}


/*! Prints information about each of the given requested images on multiple
	lines.*/
void print_image_requests(const image_request_t* image_requests, uint32_t image_count);

/*! This function creates images according to all of the given requests,
	creates views for them and allocates memory. Images get dedicated memory if
	they prefer it. All other images share one allocation.
	\param images The result is written into this object. Upon success, the
		calling side is responsible for destroying it using
		destroy_images().
	\param device The used device.
	\param requests Pointer to descriptions of the requested images.
	\param image_count The number of requested images, matching the length of
		requests.
	\param memory_properties The memory flags that you want to enforce for the
		memory allocations. The most common choice would be
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT.
	\return 0 on success.*/
int create_images(images_t* images, const device_t* device,
	const image_request_t* requests, uint32_t image_count, VkMemoryPropertyFlags memory_properties);

/*! Destroys an images that has been created successfully by
	create_images().*/
void destroy_images(images_t* images, const device_t* device);


/*! Creates one or more buffers according to the given specifications, performs
	a single memory allocation for all of them and binds it.
	\param buffers The output object. Use destroy_buffers() to free it.
	\param device The used device.
	\param buffer_infos A specification of each buffer that is to be created
		(buffer_count in total).
	\param buffer_count The number of buffers to create.
	\param memory_properties The memory flags that you want to enforce for the
		memory allocation. Combination of VkMemoryHeapFlagBits.
	\return 0 on success.*/
int create_buffers(buffers_t* buffers, const device_t* device, const VkBufferCreateInfo* buffer_infos, uint32_t buffer_count, VkMemoryPropertyFlags memory_properties);

/*! Destroys all buffers in the given object, frees the device memory
	allocation, destroys arrays, zeros handles and zeros the object.*/
void destroy_buffers(buffers_t* buffers, const device_t* device);


/*! Implements copy_buffers(), copy_images() and copy_buffers_to_images() using
	a single command buffer.*/
int copy_buffers_and_images(const device_t* device,
	uint32_t buffer_count, const VkBuffer* source_buffers, const VkBuffer* destination_buffers, VkBufferCopy* buffer_regions,
	uint32_t image_count, const VkImage* source_images, const VkImage* destination_images, VkImageLayout source_layout,
	VkImageLayout destination_layout_before, VkImageLayout destination_layout_after, VkImageCopy* image_regions,
	uint32_t buffer_to_image_count, const VkBuffer* image_source_buffers, const VkImage* buffer_destination_images,
	VkImageLayout buffer_destination_layout_before, VkImageLayout buffer_destination_layout_after, VkBufferImageCopy* buffer_to_image_regions);

/*! This function copies data between buffers, e.g. to get data from staging
	buffers into device local buffers. Upon successful return, the copying has
	been completed.
	\param buffer_count Number of buffer pairs for which to perform a copy.
	\param source_buffers Buffers with usage VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		from which to copy.
	\param destination_buffers Buffers with usage
		VK_BUFFER_USAGE_TRANSFER_DST_BIT to which to copy.
	\param buffer_region The region to copy for each source and destination
		buffer.
	\return 0 on success.*/
static inline int copy_buffers(const device_t* device,
	uint32_t buffer_count, const VkBuffer* source_buffers, const VkBuffer* destination_buffers, VkBufferCopy* buffer_regions)
{
	return copy_buffers_and_images(device, buffer_count, source_buffers, destination_buffers, buffer_regions,
		0, NULL, NULL, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, NULL,
		0, NULL, NULL, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, NULL);
}

/*! This function copies data between images, e.g. to get data from staging
	images into device local images. Upon successful return, the copying has
	been completed.
	\param image_count Number of image pairs for which to perform a copy.
	\param source_images Images with usage VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		from which to copy.
	\param destination_images Images with usage
		VK_IMAGE_USAGE_TRANSFER_DST_BIT to which to copy.
	\param source_layout The current layouts of all source images. If the values
		are different, you have to invoke this function multiple times.
	\param destination_layout_before The current layout of the destination
		images. Pass VK_IMAGE_LAYOUT_UNDEFINED if you want to completely
		replace all current contents of the images.
	\param destination_layout_after Destination images are first transferred
		into layout VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL and once copying is
		done, they are transferred into this given layout.
	\param image_region The region to copy for each source and destination
		image.
	\return 0 on success.*/
static inline int copy_images(const device_t* device,
	uint32_t image_count, const VkImage* source_images, const VkImage* destination_images,
	VkImageLayout source_layout, VkImageLayout destination_layout_before, VkImageLayout destination_layout_after, VkImageCopy* image_regions)
{
	return copy_buffers_and_images(device, 0, NULL, NULL, NULL,
		image_count, source_images, destination_images,
		source_layout, destination_layout_before, destination_layout_after, image_regions,
		0, NULL, NULL, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, NULL);
}

/*! This function copies data from buffers to images, e.g. to fill textures
	with binary data from staging buffers. Upon successful return, the copying
	has been completed.
	\param image_count Number of image pairs for which to perform a copy.
	\param source_images Images with usage VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		from which to copy.
	\param destination_images Images with usage
		VK_IMAGE_USAGE_TRANSFER_DST_BIT to which to copy.
	\param buffer_destination_layout_before The current layout of the
		destination images. Pass VK_IMAGE_LAYOUT_UNDEFINED if you want to
		completely replace all current contents of the images.
	\param buffer_destination_layout_after Destination images are first
		transitioned into layout VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL and once
		copying is done, they are transferred into this given layout.
	\param image_region The region to copy for each source and destination
		image.
	\return 0 on success.*/
static inline int copy_buffers_to_images(const device_t* device,
	uint32_t buffer_to_image_count, const VkBuffer* image_source_buffers, const VkImage* buffer_destination_images,
	VkImageLayout buffer_destination_layout_before, VkImageLayout buffer_destination_layout_after, VkBufferImageCopy* buffer_to_image_regions)
{
	return copy_buffers_and_images(device, 0, NULL, NULL, NULL, 0, NULL, NULL,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, NULL,
		buffer_to_image_count, image_source_buffers, buffer_destination_images,
		buffer_destination_layout_before, buffer_destination_layout_after, buffer_to_image_regions);
}


/*! This function compiles the requested shader and creates a module for it.
	\param shader The output object. On success, it must be deleted using
		destroy_shader().
	\param device The used device.
	\param shader_request All attributes characterizing the shader to compile
	\return 0 on success.
	\note This function creates a file with extension .spv next to the given
		shader. If it existed before, it is overwritten without prompt.
	\note The debug build compiles shaders in a way that is optimal for
		debugging, the release build optimizes for speed.*/
int compile_glsl_shader(shader_t* shader, const device_t* device, const shader_request_t* request);

/*! Forwards to compile_glsl_shader(). Upon failure, it keeps asking on the
	console whether another attempt should be made until the user declines or
	the shader has been fixed.
	\return 0 if compilation succeeded (after any number of tries).*/
int compile_glsl_shader_with_second_chance(shader_t* shader, const device_t* device, const shader_request_t* request);

//! Frees and nulls the given shader
void destroy_shader(shader_t* shader, const device_t* device);


/*! Creates a single descriptor set layout with a pipeline layout using only
	that layout and allocates the requested number of descriptor sets by means
	of a newly created descriptor pool. The pipeline is not created and assumed
	to be not created yet. The layout is defined by the given request.
	\return 0 if all objects were created / allocated successfully.*/
int create_descriptor_sets(pipeline_with_bindings_t* pipeline, const device_t* device, const descriptor_set_request_t* request, uint32_t descriptor_set_count);

/*! This little utility helps with writing to descriptor sets. For each entry
	of the given array, it:
	- sets sType to VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	- sets descriptorType to the corresponding value in request (based on
	  dstBinding, if any),
	- sets descriptorCount to the corresponding value in request or to
	  request->min_descriptor_count if that is bigger.*/
void complete_descriptor_set_write(uint32_t write_count, VkWriteDescriptorSet* writes, const descriptor_set_request_t* request);

//! Frees objects and zeros
void destroy_pipeline_with_bindings(pipeline_with_bindings_t* pipeline, const device_t* device);
