//  Copyright (C) 2022, Christoph Peters, Karlsruhe Institute of Technology
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


#include "main.h"
#include "math_utilities.h"
#include "string_utilities.h"
#include "frame_timer.h"
#include "user_interface.h"
#include "textures.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <stdlib.h>
#include <string.h>


/*! GLFW callbacks do not support passing a user-defined pointer. Thus, we have
	a single static, global pointer to the running application to give them access
	to whatever they need.*/
static application_t* g_glfw_application = NULL;

const scene_source_t g_scene_sources[] = {
	{ "Warrok", "data/warrok.vks", "data/characters_textures", "data/quicksaves/warrok.save", 4, 170 },
	{ "Joe", "data/joe.vks", "data/characters_textures", "data/quicksaves/joe.save", 6, 350 },
	{ "Chad", "data/chad.vks", "data/characters_textures", "data/quicksaves/chad.save", 7, 320 },
	{ "Shannon", "data/shannon.vks", "data/characters_textures", "data/quicksaves/shannon.save", 7, 250 },
	{ "Elvis", "data/elvis.vks", "data/characters_textures", "data/quicksaves/elvis.save", 9, 400 },
	{ "Boss", "data/boss.vks", "data/characters_textures", "data/quicksaves/boss.save", 10, 300 },
	{ "Characters", "data/characters.vks", "data/characters_textures", "data/quicksaves/characters.save", 10, 7000 },
};


void copy_scene_source(scene_source_t* dest, const scene_source_t* source) {
	(*dest) = (*source);
	dest->name = copy_string(source->name);
	dest->file_path = copy_string(source->file_path);
	dest->texture_path = copy_string(source->texture_path);
	dest->quick_save_path = copy_string(source->quick_save_path);
}


void destroy_scene_source(scene_source_t* scene) {
	free(scene->name);
	free(scene->file_path);
	free(scene->texture_path);
	free(scene->quick_save_path);
	memset(scene, 0, sizeof(*scene));
}


/*! Writes the camera and other mutable scene data of the given scene into its
	associated quicksave file.*/
void quick_save(scene_specification_t* scene) {
	FILE* file = fopen(scene->source.quick_save_path, "wb");
	if (!file) {
		printf("Quick save failed. Please check path and permissions: %s\n", scene->source.quick_save_path);
		return;
	}
	fwrite(&scene->camera, sizeof(scene->camera), 1, file);
	fwrite(&scene->light_inclination, sizeof(scene->light_inclination), 1, file);
	fwrite(&scene->light_azimuth, sizeof(scene->light_azimuth), 1, file);
	fwrite(scene->light_irradiance, sizeof(scene->light_irradiance), 1, file);
	fwrite(&scene->time, sizeof(scene->time), 1, file);
	fclose(file);
}

/*! Loads camera and other mutable scene data from the quicksave file specified
	for the given scene and writes them into the scene specification. If
	necessary, updates are flagged.*/
void quick_load(scene_specification_t* scene, application_updates_t* updates) {
	FILE* file = fopen(scene->source.quick_save_path, "rb");
	if (!file) {
		printf("Failed to load a quick save. Please check path and permissions: %s\n", scene->source.quick_save_path);
		return;
	}
	fread(&scene->camera, sizeof(scene->camera), 1, file);
	fread(&scene->light_inclination, sizeof(scene->light_inclination), 1, file);
	fread(&scene->light_azimuth, sizeof(scene->light_azimuth), 1, file);
	fread(scene->light_irradiance, sizeof(scene->light_irradiance), 1, file);
	fread(&scene->time, sizeof(scene->time), 1, file);
	fclose(file);
}


//! Fills the given object with a complete specification of the default scene
void specify_default_scene(scene_specification_t* scene) {
	uint32_t scene_index = scene_characters;
	destroy_scene_source(&scene->source);
	copy_scene_source(&scene->source, &g_scene_sources[scene_index]);
	first_person_camera_t camera = {
		.near = 0.05f, .far = 1.0e5f,
		.vertical_fov = 0.33f * M_PI_F,
		.rotation_x = 0.43f * M_PI_F,
		.rotation_z = 1.3f * M_PI_F,
		.position_world_space = {-3.0f, -2.0f, 1.65f},
		.speed = 2.0f
	};
	scene->camera = camera;
	scene->light_inclination = -0.3f * M_PI_F;
	scene->light_irradiance[0] = scene->light_irradiance[1] = scene->light_irradiance[2] = 5.0f;
	scene->time = 0.0f;
	// Try to quick load. Upon success, it will override the defaults above.
	quick_load(scene, NULL);
}



//! Frees memory and zeros
void destroy_scene_specification(scene_specification_t* scene) {
	destroy_scene_source(&scene->source);
	memset(scene, 0, sizeof(*scene));
}


//! Sets render settings to default values
void specify_default_render_settings(render_settings_t* settings) {
	settings->exposure_factor = 1.0f;
	settings->roughness = 0.5f;
	settings->error_display = error_display_none;
	settings->error_min_exponent = -5.0f;
	settings->error_max_exponent = -3.5f;
	settings->v_sync = VK_TRUE;
	settings->show_gui = VK_TRUE;
	settings->playback_speed = 0.0f;
	settings->compression_params.method = blend_attribute_compression_permutation_coding;
	const scene_source_t* scene = &g_scene_sources[scene_characters];
	settings->compression_params.vertex_size = 8;
	settings->compression_params.max_bone_count = scene->available_bone_count;
	settings->compression_params.max_tuple_count = scene->max_tuple_count;
	complete_blend_attribute_compression_parameters(&settings->compression_params);
	settings->requested_vertex_size = settings->compression_params.vertex_size;
	settings->requested_max_bone_count = settings->compression_params.max_bone_count;
	settings->instance_count = 1;
}


//! Frees objects and zeros
void destroy_render_targets(render_targets_t* render_targets, const device_t* device) {
	destroy_images(&render_targets->targets_allocation, device);
	memset(render_targets, 0, sizeof(*render_targets));
}

//! Creates render targets and associated objects
int create_render_targets(render_targets_t* targets, const device_t* device, const swapchain_t* swapchain) {
	memset(targets, 0, sizeof(*targets));
	VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
	if (swapchain->format == VK_FORMAT_A2R10G10B10_UNORM_PACK32 || swapchain->format == VK_FORMAT_A2B10G10R10_UNORM_PACK32)
		color_format = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
	image_request_t image_requests[] = {
		{// depth buffer
			.image_info = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				.imageType = VK_IMAGE_TYPE_2D,
				.format = VK_FORMAT_D32_SFLOAT,
				.extent = {swapchain->extent.width, swapchain->extent.height, 1},
				.mipLevels = 1, .arrayLayers = 1, .samples = 1,
				.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
			},
			.view_info = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT
				}
			}
		},
	};
	targets->target_count = COUNT_OF(image_requests);
	targets->duplicate_count = swapchain->image_count;
	// Duplicate the requests per swapchain image
	image_request_t* all_requests = malloc(sizeof(image_requests) * targets->duplicate_count);
	for (uint32_t i = 0; i != targets->duplicate_count; ++i) {
		memcpy(all_requests + i * targets->target_count, image_requests, sizeof(image_requests));
	}
	if (create_images(&targets->targets_allocation, device, all_requests,
		targets->target_count * targets->duplicate_count, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
	{
		printf("Failed to create render targets.\n");
		free(all_requests);
		destroy_render_targets(targets, device);
		return 1;
	}
	free(all_requests);
	targets->targets = (void*) targets->targets_allocation.images;
	return 0;
}


//! Frees objects and zeros
void destroy_constant_buffers(constant_buffers_t* constant_buffers, const device_t* device) {
	if (constant_buffers->data)
		vkUnmapMemory(device->device, constant_buffers->buffers.memory);
	destroy_buffers(&constant_buffers->buffers, device);
	memset(constant_buffers, 0, sizeof(*constant_buffers));
}

//! Allocates constant buffers and maps their memory
int create_constant_buffers(constant_buffers_t* constant_buffers, const device_t* device, const swapchain_t* swapchain, const scene_specification_t* scene_specification, const render_settings_t* render_settings) {
	memset(constant_buffers, 0, sizeof(*constant_buffers));
	// Compute the total size for the constant buffer
	size_t size = sizeof(per_frame_constants_t);
	// Create one constant buffer per swapchain image
	VkBufferCreateInfo constant_buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
	};
	VkBufferCreateInfo* constant_buffer_infos = malloc(sizeof(VkBufferCreateInfo) * swapchain->image_count);
	for (uint32_t i = 0; i != swapchain->image_count; ++i)
		constant_buffer_infos[i] = constant_buffer_info;
	if (create_aligned_buffers(&constant_buffers->buffers, device, constant_buffer_infos, swapchain->image_count, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, device->physical_device_properties.limits.nonCoherentAtomSize)) {
		printf("Failed to create constant buffers.\n");
		free(constant_buffer_infos);
		destroy_constant_buffers(constant_buffers, device);
		return 1;
	}
	free(constant_buffer_infos);
	// Map the complete memory
    if (vkMapMemory(device->device, constant_buffers->buffers.memory, 0, constant_buffers->buffers.size, 0, &constant_buffers->data)) {
		printf("Failed to map constant buffers.\n");
		destroy_constant_buffers(constant_buffers, device);
		return 1;
	}
	return 0;
}


//! Types of vertex attributes that may be fed to shaders via
//! declare_variable_size_vertex_attribute()
typedef enum vertex_attribute_type_e {
	//! The vertex buffer provides an array of uint16_t per vertex, in the
	//! shader each of them becomes a uint
	vertex_attribute_uint16,
	//! The vertex buffer provides an array of float per vertex, in the shader
	//! each of them becomes a float
	vertex_attribute_float32,
	//! The vertex buffer provides a sequence of bytes, in the shader four
	//! subsequent bytes are provided as one uint
	vertex_attribute_bytes,
} vertex_attribute_type_t;


/*! Vertex attributes are limited to whatever VkFormat allows. They cannot be
	arbitrarily big and the limit for their number (given by
	VkPhysicalDeviceLimits::maxVertexInputAttributes) may be as low as 16. This
	function deals with this situation in a unified way by defining multiple
	attributes of different sizes (as few as possible) and turning them into a
	homogeneous array.
	\param attributes New attributes are written to this array. It must provide
		enough space.
	\param location Pointer to the first index in attributes that should be
		overwritten. This index is also used as location element and gets
		incremented for each added vertex attribute.
	\param declaration_define Set to a pointer to a string that declares a
		define called DECLARE_<attribute_name> (see shader_request_s::defines).
		Placing this define in a vertex shader in global scope declares the
		vertex attributes. The calling side has to free this.
	\param array_define Like declaration_define. This define is called
		MAKE_<attribute_name>_ARRAY. Invoking it in a local scope of a vertex
		shader turns the global vertex attributes into a local, homogeneous
		array called <attribute_name> as specified by type.
	\param attribute_name The name of the attribute being declared (plural).
	\param value_length The number of scalars or bytes provided per vertex.
	\param binding The binding index of the vertex buffer providing these
		attributes.
	\param type Specifies what type of data is being fed and how it is exposed
		in the shader.*/
void declare_variable_size_vertex_attribute(
	VkVertexInputAttributeDescription* attributes, uint32_t* location,
	char** declaration_define, char** array_define, const char* attribute_name,
	uint32_t value_length, uint32_t binding, vertex_attribute_type_t type)
{
	// How many entries a vector specified by VkFormat can have
	static const uint32_t vec_lengths[] = { 4, 2, 1 };
	// How many bytes a vector specified by VkFormat can have. The first index
	// is value_length % 4 to ensure consistent alignment.
	static const uint32_t byte_lengths[4][3] = {
		{ 16, 8, 4 },
		{ 4, 2, 1 },
		{ 8, 4, 2 },
		{ 4, 2, 1 },
	};
	// Mapping lengths from the previous two arrays to a suitable VkFormat
	static const VkFormat uint16_formats[] = { 0, VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, 0, VK_FORMAT_R16G16B16A16_UINT };
	static const VkFormat float_formats[] = { 0, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, 0, VK_FORMAT_R32G32B32A32_SFLOAT };
	static const VkFormat byte_formats[4][17] = {
		{ 0, 0, 0, 0, VK_FORMAT_R32_UINT, 0, 0, 0, VK_FORMAT_R32G32_UINT, 0, 0, 0, 0, 0, 0, 0, VK_FORMAT_R32G32B32A32_UINT },
		{ 0, VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, 0, VK_FORMAT_R8G8B8A8_UINT },
		{ 0, 0, VK_FORMAT_R16_UINT, 0, VK_FORMAT_R16G16_UINT, 0, 0, 0, VK_FORMAT_R16G16B16A16_UINT },
		{ 0, VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, 0, VK_FORMAT_R8G8B8A8_UINT },
	};
	// Mapping the number of vector entries in the GLSL types to type names
	static const char* uint_types[] = { NULL, "uint", "uvec2", NULL, "uvec4" };
	static const char* float_types[] = { NULL, "float", "vec2", NULL, "vec4" };
	// Make a choice among the arrays declared above
	const uint32_t* supported_lengths;
	uint32_t supported_length_count;
	const VkFormat* length_formats;
	const char* const* length_types;
	uint32_t glsl_scalar_length = 1;
	uint32_t value_size;
	switch (type) {
	case vertex_attribute_uint16:
		supported_lengths = vec_lengths;
		supported_length_count = COUNT_OF(vec_lengths);
		length_formats = uint16_formats;
		length_types = uint_types;
		value_size = sizeof(uint16_t);
		break;
	case vertex_attribute_float32:
		supported_lengths = vec_lengths;
		supported_length_count = COUNT_OF(vec_lengths);
		length_formats = float_formats;
		length_types = float_types;
		value_size = sizeof(float);
		break;
	case vertex_attribute_bytes:
		supported_lengths = byte_lengths[value_length % 4];
		supported_length_count = COUNT_OF(byte_lengths[0]);
		length_formats = byte_formats[value_length % 4];
		glsl_scalar_length = supported_lengths[2];
		length_types = uint_types;
		value_size = sizeof(uint8_t);
		break;
	default:
		(*declaration_define) = (*array_define) = NULL;
		return;
	}
	// Figure out how to decompose the per-vertex array in the vertex buffer
	uint32_t attribute_lengths[64];
	uint32_t attribute_count = write_as_sum(attribute_lengths, value_length, supported_length_count, supported_lengths);
	// Now take care of the declarations
	uint32_t total_size = 0;
	char* pieces[64 * 6];
	uint32_t count = 0;
	pieces[count++] = copy_string("DECLARE_");
	pieces[count++] = copy_string(attribute_name);
	pieces[count++] = copy_string("=\"");
	for (uint32_t i = 0; i != attribute_count; ++i) {
		// Define the attribute in the pipeline
		attributes[*location].location = *location;
		attributes[*location].binding = binding;
		attributes[*location].format = length_formats[attribute_lengths[i]];
		attributes[*location].offset = total_size;
		// Declare the attribute in the shader
		pieces[count++] = format_uint("layout (location = %u) in ", *location);
		pieces[count++] = copy_string(length_types[attribute_lengths[i] / glsl_scalar_length]);
		pieces[count++] = copy_string(" g_");
		pieces[count++] = copy_string(attribute_name);
		pieces[count++] = format_uint("_%u;  ", i);
		// Iterate onwards
		++(*location);
		total_size += attribute_lengths[i] * value_size;
	}
	pieces[count++] = copy_string("\"");
	(*declaration_define) = concatenate_strings(count, (const char* const*) pieces);
	for (uint32_t i = 0; i != count; ++i)
		free(pieces[i]);
	count = 0;
	// Finally, produce the code that will turn it all into an array
	pieces[count++] = copy_string("MAKE_");
	pieces[count++] = copy_string(attribute_name);
	pieces[count++] = copy_string((type == vertex_attribute_float32) ? "_ARRAY=\"float " : "_ARRAY=\"uint ");
	pieces[count++] = copy_string(attribute_name);
	pieces[count++] = copy_string("[] = {");
	uint32_t shift = 0;
	for (uint32_t i = 0; i != attribute_count; ++i) {
		uint32_t length = attribute_lengths[i];
		uint32_t glsl_length = length / glsl_scalar_length;
		for (uint32_t j = 0; j != glsl_length; ++j) {
			pieces[count++] = copy_string("g_");
			pieces[count++] = copy_string(attribute_name);
			if (glsl_length > 1)
				pieces[count++] = format_uint2("_%u[%u]", i, j);
			else
				pieces[count++] = format_uint("_%u", i);
			if (type == vertex_attribute_bytes) {
				int uint_complete = ((shift + glsl_scalar_length) % 4 == 0 || shift + glsl_scalar_length == value_length);
				pieces[count++] = format_uint(uint_complete ? " * 0x%x, " : " * 0x%x + ", 1 << ((shift % 4) * 8));
				shift += glsl_scalar_length;
			}
			else
				pieces[count++] = copy_string(", ");
		}
	}
	pieces[count++] = copy_string("};\"");
	(*array_define) = concatenate_strings(count, (const char* const*) pieces);
	for (uint32_t i = 0; i != count; ++i)
		free(pieces[i]);
}


//! Frees objects and zeros
void destroy_forward_pass(forward_pass_t* pass, const device_t* device) {
	destroy_pipeline_with_bindings(&pass->pipeline, device);
	destroy_shader(&pass->vertex_shader, device);
	destroy_shader(&pass->fragment_shader, device);
	memset(pass, 0, sizeof(*pass));
}

//! Creates Vulkan objects for the forward rendering pass
int create_forward_pass(forward_pass_t* pass, const device_t* device, const swapchain_t* swapchain,
	const scene_t* scene, const constant_buffers_t* constant_buffers, const render_targets_t* render_targets, 
	const render_pass_t* render_pass, const render_settings_t* render_settings)
{
	memset(pass, 0, sizeof(*pass));
	pipeline_with_bindings_t* pipeline = &pass->pipeline;
	// Figure out what techniques will be used
	VkBool32 use_ground_truth = (scene->mesh.compression_params.method == blend_attribute_compression_none || render_settings->error_display != error_display_none);
	VkBool32 use_compressed = (scene->mesh.compression_params.method != blend_attribute_compression_none);
	VkBool32 use_table = use_compressed;
	// Create a pipeline layout for the forward pass
	VkDescriptorSetLayoutBinding layout_bindings[] = {
		{ .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
		{ .binding = 1 },
		{ .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
		{ .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER },
		{ .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER },
	};
	get_materials_descriptor_layout(&layout_bindings[1], 1, &scene->materials);
	descriptor_set_request_t set_request = {
		.stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.min_descriptor_count = 1,
		.binding_count = COUNT_OF(layout_bindings),
		.bindings = layout_bindings,
	};
	if (create_descriptor_sets(pipeline, device, &set_request, swapchain->image_count)) {
		printf("Failed to create a descriptor set for the forward pass.\n");
		destroy_forward_pass(pass, device);
		return 1;
	}
	// Write to the descriptor set
	VkDescriptorBufferInfo descriptor_buffer_info = { .offset = 0 };
	VkDescriptorImageInfo descriptor_image_info = {
		.sampler = scene->animation.sampler,
		.imageView = scene->animation.texture.images[0].view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	uint32_t material_write_index = 1;
	VkWriteDescriptorSet descriptor_set_writes[] = {
		{ .dstBinding = 0, .pBufferInfo = &descriptor_buffer_info },
		{ .dstBinding = 1 },
		{ .dstBinding = 2, .pImageInfo = &descriptor_image_info },
		{ .dstBinding = 3, .pTexelBufferView = &scene->mesh.material_indices_view },
		{ .dstBinding = 4, .pTexelBufferView = &scene->mesh.bone_index_table_view },
	};
	descriptor_set_writes[1].pImageInfo = get_materials_descriptor_infos(&descriptor_set_writes[material_write_index].descriptorCount, &scene->materials);
	complete_descriptor_set_write(COUNT_OF(descriptor_set_writes), descriptor_set_writes, &set_request);
	for (uint32_t i = 0; i != swapchain->image_count; ++i) {
		descriptor_buffer_info.buffer = constant_buffers->buffers.buffers[i].buffer;
		descriptor_buffer_info.range = constant_buffers->buffers.buffers[i].size;
		for (uint32_t j = 0; j != COUNT_OF(descriptor_set_writes); ++j)
			descriptor_set_writes[j].dstSet = pipeline->descriptor_sets[i];
		vkUpdateDescriptorSets(device->device, COUNT_OF(descriptor_set_writes), descriptor_set_writes, 0, NULL);
	}
	
	// Bind the vertex buffer
	uint64_t vertex_count = scene->mesh.triangle_count * 3;
	VkVertexInputBindingDescription vertex_binding = { .binding = 0, .stride = scene->mesh.vertices.size / vertex_count };
	pass->vertex_buffers[0] = scene->mesh.vertices.buffer;
	pass->vertex_buffer_count = 1;
	// All vertex attributes are fed to the shader as a chunk of binary data
	VkVertexInputAttributeDescription vertex_attributes[64] = { 0 };
	uint32_t location = 0;
	char *declare_vertex_data, *make_vertex_data_array;
	declare_variable_size_vertex_attribute(vertex_attributes, &location, &declare_vertex_data, &make_vertex_data_array, "vertex_data", vertex_binding.stride, 0, vertex_attribute_bytes);
	VkPipelineVertexInputStateCreateInfo vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = pass->vertex_buffer_count,
		.pVertexBindingDescriptions = &vertex_binding,
		.vertexAttributeDescriptionCount = location,
		.pVertexAttributeDescriptions = vertex_attributes,
	};
	// Determine the offset in 4-byte steps to the beginning of the compressed
	// blend attributes
	uint32_t compressed_offset = 4;
	if (scene->mesh.store_ground_truth)
		compressed_offset += ((sizeof(float) + sizeof(uint16_t)) * scene->mesh.compression_params.max_bone_count) / 4;

	// Create a define describing the used codec for permutation coding
	const blend_attribute_codec_t* permutation_codec = &scene->mesh.compression_params.permutation_coding;
	char* define_pieces[14] = { format_uint("PERMUTATION_CODEC=\"{ %u, { ", permutation_codec->weight_value_count) };
	for (uint32_t i = 0; i != scene->mesh.compression_params.max_bone_count - 1; ++i)
		define_pieces[i + 1] = format_uint("%u, ", permutation_codec->extra_value_counts[i]);
	define_pieces[scene->mesh.compression_params.max_bone_count] = format_uint("}, %u }\"", permutation_codec->payload_value_count_over_factorial);
	char* permutation_codec_define = concatenate_strings(scene->mesh.compression_params.max_bone_count + 1, (const char* const*) define_pieces);
	for (uint32_t i = 0; i != COUNT_OF(define_pieces); ++i)
		free(define_pieces[i]);

	// Compile the shaders using the appropriate defines
	VkBool32 output_linear_rgb = swapchain->format == VK_FORMAT_R8G8B8A8_SRGB || swapchain->format == VK_FORMAT_B8G8R8A8_SRGB;
	blend_attribute_compression_method_t compression_method = scene->mesh.compression_params.method;
	char* defines[] = {
		format_uint("MATERIAL_COUNT=%u", (uint32_t) scene->materials.material_count),
		format_uint("OUTPUT_LINEAR_RGB=%u", output_linear_rgb),
		format_uint("MAX_BONE_COUNT=%u", scene->mesh.compression_params.max_bone_count),
		format_uint("ENTRY_COUNT=%u", scene->mesh.compression_params.max_bone_count - 1),
		format_uint("COMPRESSED_SIZE=%u", scene->mesh.compression_params.vertex_size),
		format_uint("GROUND_TRUTH_AVAILABLE=%u", use_ground_truth),
		format_uint("ERROR_DISPLAY_NONE=%u", render_settings->error_display == error_display_none),
		format_uint("ERROR_DISPLAY_POSITIONS_LOGARITHMIC=%u", render_settings->error_display == error_display_positions_logarithmic),
		format_uint("TUPLE_VECTOR_SIZE=%u", scene->mesh.tuple_vector_size),
		format_uint("COMPRESSED_OFFSET=%u", compressed_offset),
		permutation_codec_define,
		declare_vertex_data,
		make_vertex_data_array,
		format_uint("BLEND_ATTRIBUTE_COMPRESSION_NONE=%u", compression_method == blend_attribute_compression_none),
		format_uint("BLEND_ATTRIBUTE_COMPRESSION_UNIT_CUBE_SAMPLING=%u", compression_method == blend_attribute_compression_unit_cube_sampling),
		format_uint("BLEND_ATTRIBUTE_COMPRESSION_POWER_OF_TWO_AABB=%u", compression_method == blend_attribute_compression_power_of_two_aabb),
		format_uint("BLEND_ATTRIBUTE_COMPRESSION_OPTIMAL_SIMPLEX_SAMPLING_19=%u", compression_method == blend_attribute_compression_optimal_simplex_sampling_19),
		format_uint("BLEND_ATTRIBUTE_COMPRESSION_OPTIMAL_SIMPLEX_SAMPLING_22=%u", compression_method == blend_attribute_compression_optimal_simplex_sampling_22),
		format_uint("BLEND_ATTRIBUTE_COMPRESSION_OPTIMAL_SIMPLEX_SAMPLING_35=%u", compression_method == blend_attribute_compression_optimal_simplex_sampling_35),
		format_uint("WEIGHT_BASE_BIT_COUNT=%u", scene->mesh.compression_params.weight_base_bit_count),
		format_uint("TUPLE_INDEX_BIT_COUNT=%u", scene->mesh.compression_params.tuple_index_bit_count),
		format_uint("BLEND_ATTRIBUTE_COMPRESSION_PERMUTATION_CODING=%u", compression_method == blend_attribute_compression_permutation_coding),
	};
	shader_request_t vertex_shader_request = {
		.shader_file_path = "src/shaders/forward_pass.vert.glsl",
		.include_path = "src/shaders",
		.entry_point = "main",
		.define_count = COUNT_OF(defines),
		.defines = defines,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
	};
	shader_request_t fragment_shader_request = {
		.shader_file_path = "src/shaders/forward_pass.frag.glsl",
		.include_path = "src/shaders",
		.entry_point = "main",
		.define_count = COUNT_OF(defines),
		.defines = defines,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
	};
	int compile_result = (
		compile_glsl_shader_with_second_chance(&pass->vertex_shader, device, &vertex_shader_request)
		|| compile_glsl_shader_with_second_chance(&pass->fragment_shader, device, &fragment_shader_request));
	for (uint32_t i = 0; i != COUNT_OF(defines); ++i)
		free(defines[i]);
	if (compile_result) {
		printf("Failed to compile the vertex or pixel shader for the forward pass.\n");
		destroy_forward_pass(pass, device);
		return 1;
	}

	// Define other graphics pipeline state
	VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.primitiveRestartEnable = VK_FALSE,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	VkPipelineRasterizationStateCreateInfo raster_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};
	VkPipelineColorBlendAttachmentState blend_attachment_state = {
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};
	VkPipelineColorBlendStateCreateInfo blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment_state,
		.logicOp = VK_LOGIC_OP_NO_OP,
		.blendConstants = {1.0f, 1.0f, 1.0f, 1.0f}
	};
	VkViewport viewport = {
		.x = 0.0f, .y = 0.0f,
		.width = (float)swapchain->extent.width, .height = (float)swapchain->extent.height,
		.minDepth = 0.0f, .maxDepth = 1.0f
	};
	VkRect2D scissor = {.extent = swapchain->extent};
	VkPipelineViewportStateCreateInfo viewport_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
		.pScissors = &scissor,
		.pViewports = &viewport
	};
	VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS
	};
	VkPipelineMultisampleStateCreateInfo multi_sample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};
	VkPipelineShaderStageCreateInfo shader_stages[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = pass->vertex_shader.module,
			.pName = "main"
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = pass->fragment_shader.module,
			.pName = "main"
		}
	};
	VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.layout = pipeline->pipeline_layout,
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &input_assembly_info,
		.pRasterizationState = &raster_info,
		.pColorBlendState = &blend_info,
		.pTessellationState = NULL,
		.pMultisampleState = &multi_sample_info,
		.pDynamicState = NULL,
		.pViewportState = &viewport_info,
		.pDepthStencilState = &depth_stencil_info,
		.stageCount = 2,
		.pStages = shader_stages,
		.renderPass = render_pass->render_pass,
		.subpass = 0
	};
	if (vkCreateGraphicsPipelines(device->device, NULL, 1, &pipeline_info, NULL, &pass->pipeline.pipeline)) {
		printf("Failed to create a graphics pipeline for the forward pass.\n");
		destroy_forward_pass(pass, device);
		return 1;
	}
	return 0;
}


//! Frees objects and zeros
void destroy_interface_pass(interface_pass_t* pass, const device_t* device) {
	for (uint32_t i = 0; i != pass->frame_count; ++i)
		if (pass->frames) free(pass->frames[i].draws);
	free(pass->frames);
	destroy_buffers(&pass->geometry_allocation, device);
	destroy_images(&pass->texture, device);
	destroy_pipeline_with_bindings(&pass->pipeline, device);
	destroy_shader(&pass->vertex_shader, device);
	destroy_shader(&pass->fragment_shader, device);
	if (pass->sampler) vkDestroySampler(device->device, pass->sampler, NULL);
	memset(pass, 0, sizeof(*pass));
}

//! Creates the pass for rendering a user interface
int create_interface_pass(interface_pass_t* pass, const device_t* device, imgui_handle_t imgui, const swapchain_t* swapchain, const render_targets_t* render_targets, const render_pass_t* render_pass) {
	memset(pass, 0, sizeof(*pass));
	// Create geometry buffers and map memory
	uint32_t imgui_quad_count = 0xFFFF;
	VkBufferCreateInfo geometry_infos[] = {
		{ // 0 - Imgui vertices
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizeof(imgui_vertex_t) * 4 * imgui_quad_count,
			.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
		},
		{ // 1 - Imgui indices
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizeof(uint16_t) * 6 * imgui_quad_count,
			.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT
		}
	};
	pass->frame_count = swapchain->image_count;
	uint32_t geometry_count = COUNT_OF(geometry_infos) * pass->frame_count;
	VkBufferCreateInfo* duplicate_geometry_infos = malloc(sizeof(VkBufferCreateInfo) * geometry_count);
	for (uint32_t i = 0; i != geometry_count; ++i)
		duplicate_geometry_infos[i] = geometry_infos[i % COUNT_OF(geometry_infos)];
	if (create_aligned_buffers(&pass->geometry_allocation, device, duplicate_geometry_infos, geometry_count, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, device->physical_device_properties.limits.nonCoherentAtomSize)) {
		printf("Failed to create geometry buffers for the interface pass.\n");
		destroy_interface_pass(pass, device);
		free(duplicate_geometry_infos);
		return 1;
	}
	free(duplicate_geometry_infos);
	pass->geometries = (void*) pass->geometry_allocation.buffers;
	if (vkMapMemory(device->device, pass->geometry_allocation.memory, 0, pass->geometry_allocation.size, 0, &pass->geometry_data)) {
		printf("Failed to map geometry buffers for the interface pass.\n");
		destroy_interface_pass(pass, device);
		return 1;
	}
	// Prepare the object used to query drawing commands
	pass->frames = malloc(sizeof(imgui_frame_t) * pass->frame_count);
	memset(pass->frames, 0, sizeof(imgui_frame_t) * pass->frame_count);
	for (uint32_t i = 0; i != pass->frame_count; ++i) {
		pass->frames[i].draws_size = 1000;
		pass->frames[i].draws = malloc(sizeof(imgui_draw_t) * pass->frames[i].draws_size);
		pass->frames[i].vertices = (imgui_vertex_t*) (((uint8_t*) pass->geometry_data) + pass->geometries[i].vertices.offset);
		pass->frames[i].indices = (uint16_t*) (((uint8_t*) pass->geometry_data) + pass->geometries[i].indices.offset);
		pass->frames[i].vertices_size = 4 * imgui_quad_count;
		pass->frames[i].indices_size = 6 * imgui_quad_count;
	}

	// Compile vertex and fragment shaders for imgui
	VkBool32 output_linear_rgb = swapchain->format == VK_FORMAT_R8G8B8A8_SRGB || swapchain->format == VK_FORMAT_B8G8R8A8_SRGB;
	char* fragment_defines[2] = {
		"OUTPUT_LINEAR_RGB=0",
		"OUTPUT_LINEAR_RGB=1"
	};
	char* gui_defines[] = {
		copy_string(fragment_defines[output_linear_rgb ? 1 : 0]),
		format_uint("VIEWPORT_WIDTH=%u", swapchain->extent.width),
		format_uint("VIEWPORT_HEIGHT=%u", swapchain->extent.height),
	};
	shader_request_t gui_vertex_request = {
		.shader_file_path = "src/shaders/imgui.vert.glsl",
		.include_path = "src/shaders",
		.entry_point = "main",
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.define_count = COUNT_OF(gui_defines), .defines = gui_defines
	};
	shader_request_t gui_fragment_request = {
		.shader_file_path = "src/shaders/imgui.frag.glsl",
		.include_path = "src/shaders",
		.entry_point = "main",
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.define_count = COUNT_OF(gui_defines), .defines = gui_defines
	};
	if (compile_glsl_shader_with_second_chance(&pass->vertex_shader, device, &gui_vertex_request)
		|| compile_glsl_shader_with_second_chance(&pass->fragment_shader, device, &gui_fragment_request))
	{
		printf("Failed to compile shaders for the GUI rendering.\n");
		destroy_interface_pass(pass, device);
		for (uint32_t i = 0; i != COUNT_OF(gui_defines); ++i)
			free(gui_defines[i]);
		return 1;
	}
	for (uint32_t i = 0; i != COUNT_OF(gui_defines); ++i)
		free(gui_defines[i]);
	// Create a sampler for the font texture of imgui
	VkSamplerCreateInfo gui_sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	};
	if (vkCreateSampler(device->device, &gui_sampler_info, NULL, &pass->sampler)) {
		printf("Failed to create a sampler for rendering the GUI.\n");
		destroy_interface_pass(pass, device);
		return 1;
	}

	// Create the image with fonts for imgui
	VkExtent2D gui_texture_extent;
	get_imgui_image(NULL, &gui_texture_extent.width, &gui_texture_extent.height, imgui);
	VkBufferCreateInfo gui_staging_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof(uint8_t) * gui_texture_extent.width * gui_texture_extent.height,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
	};
	buffers_t gui_staging_buffer;
	uint8_t* gui_staging_data;
	if (create_buffers(&gui_staging_buffer, device, &gui_staging_info, 1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		|| vkMapMemory(device->device, gui_staging_buffer.memory, 0, gui_staging_buffer.size, 0, (void**) &gui_staging_data))
	{
		printf("Failed to create and map a staging buffer for the the GUI.\n");
		destroy_buffers(&gui_staging_buffer, device);
		destroy_interface_pass(pass, device);
		return 1;
	}
	get_imgui_image(gui_staging_data, NULL, NULL, imgui);
	vkUnmapMemory(device->device, gui_staging_buffer.memory);
	image_request_t gui_texture_request = {
		.image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = VK_FORMAT_R8_UNORM,
			.extent = {gui_texture_extent.width, gui_texture_extent.height, 1},
			.mipLevels = 1, .arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
		},
		.view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT}
		}
	};
	VkBufferImageCopy gui_texture_region = {
		.imageExtent = gui_texture_request.image_info.extent,
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = 1
		}
	};
	if (create_images(&pass->texture, device, &gui_texture_request, 1, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
		|| copy_buffers_to_images(device, 1, &gui_staging_buffer.buffers[0].buffer, &pass->texture.images[0].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &gui_texture_region))
	{
		printf("Failed to create and fill the GUI texture.\n");
		destroy_buffers(&gui_staging_buffer, device);
		destroy_interface_pass(pass, device);
		return 1;
	}
	destroy_buffers(&gui_staging_buffer, device);

	// Create the pipeline with descriptor set for GUI rendering
	pipeline_with_bindings_t* pipeline = &pass->pipeline;
	// Begin by allocating descriptor sets
	VkDescriptorSetLayoutBinding sampler_binding = {
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
	};
	descriptor_set_request_t set_request = {
		.stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.min_descriptor_count = 1,
		.binding_count = 1,
		.bindings = &sampler_binding,
	};
	if (create_descriptor_sets(pipeline, device, &set_request, swapchain->image_count)) {
		printf("Failed to allocate descriptor sets for the interface pass.\n");
		destroy_interface_pass(pass, device);
		return 1;
	}
	// Write to the descriptor set
	VkDescriptorImageInfo gui_descriptor_image_info = {
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.imageView = pass->texture.images[0].view,
		.sampler = pass->sampler
	};
	VkWriteDescriptorSet descriptor_set_write = { .pImageInfo = &gui_descriptor_image_info  };
	complete_descriptor_set_write(1, &descriptor_set_write, &set_request);
	for (uint32_t j = 0; j != swapchain->image_count; ++j) {
		descriptor_set_write.dstSet = pipeline->descriptor_sets[j];
		vkUpdateDescriptorSets(device->device, 1, &descriptor_set_write, 0, NULL);
	}
	// Define the graphics pipeline state
	VkVertexInputBindingDescription gui_vertex_bindings[] = {
		{.binding = 0, .stride = sizeof(imgui_vertex_t)}
	};
	VkVertexInputAttributeDescription gui_vertex_attributes[] = {
		{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0},
		{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = sizeof(float) * 2},
		{.location = 2, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = sizeof(float) * 4},
	};
	VkPipelineVertexInputStateCreateInfo vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = COUNT_OF(gui_vertex_bindings), .pVertexBindingDescriptions = gui_vertex_bindings,
		.vertexAttributeDescriptionCount = COUNT_OF(gui_vertex_attributes),	.pVertexAttributeDescriptions = gui_vertex_attributes,
	};
	VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.primitiveRestartEnable = VK_FALSE,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	VkPipelineRasterizationStateCreateInfo raster_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.lineWidth = 1.0f,
	};
	VkPipelineColorBlendAttachmentState blend_attachment_state = {
		.blendEnable = VK_TRUE,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};
	VkPipelineColorBlendStateCreateInfo blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1, .pAttachments = &blend_attachment_state,
		.logicOp = VK_LOGIC_OP_NO_OP,
		.blendConstants = {1.0f, 1.0f, 1.0f, 1.0f}
	};
	VkViewport viewport = {
		.x = 0.0f, .y = 0.0f,
		.width = (float) swapchain->extent.width, .height = (float) swapchain->extent.height,
		.minDepth = 0.0f, .maxDepth = 1.0f
	};
	VkRect2D scissor = {.extent = swapchain->extent};
	VkPipelineViewportStateCreateInfo viewport_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .pViewports = &viewport,
		.scissorCount = 1, .pScissors = &scissor,
	};
	VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_FALSE, .depthWriteEnable = VK_FALSE
	};
	VkPipelineMultisampleStateCreateInfo multi_sample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};
	VkPipelineShaderStageCreateInfo shader_stages[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = pass->vertex_shader.module,
			.pName = "main"
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = pass->fragment_shader.module,
			.pName = "main"
		}
	};
	VkDynamicState dynamic_scissor_state = VK_DYNAMIC_STATE_SCISSOR;
	VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 1, .pDynamicStates = &dynamic_scissor_state
	};
	VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.layout = pipeline->pipeline_layout,
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &input_assembly_info,
		.pRasterizationState = &raster_info,
		.pColorBlendState = &blend_info,
		.pTessellationState = NULL,
		.pMultisampleState = &multi_sample_info,
		.pDynamicState = NULL,
		.pViewportState = &viewport_info,
		.pDepthStencilState = &depth_stencil_info,
		.pDynamicState = &dynamic_state,
		.stageCount = 2, .pStages = shader_stages,
		.renderPass = render_pass->render_pass,
		.subpass = 1,
	};
	if (vkCreateGraphicsPipelines(device->device, NULL, 1, &pipeline_info, NULL, &pipeline->pipeline)) {
		printf("Failed to create a graphics pipeline for the transfer pass.\n");
		destroy_interface_pass(pass, device);
		return 1;
	}
	return 0;
}


//! Frees objects and zeros
void destroy_render_pass(render_pass_t* pass, const device_t* device) {
	for (uint32_t i = 0; i != pass->framebuffer_count; ++i)
		if (pass->framebuffers[i])
			vkDestroyFramebuffer(device->device, pass->framebuffers[i], NULL);
	free(pass->framebuffers);
	if (pass->render_pass) vkDestroyRenderPass(device->device, pass->render_pass, NULL);
	memset(pass, 0, sizeof(*pass));
}


//! Creates the render pass that renders a complete frame
int create_render_pass(render_pass_t* pass, const device_t* device, const swapchain_t* swapchain, const render_targets_t* render_targets) {
	memset(pass, 0, sizeof(*pass));
	// Create the render pass
	VkAttachmentDescription attachments[] = {
		{ // 0 - Depth buffer
			.format = render_targets->targets[0].depth_buffer.image_info.format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		},
		{ // 1 - Swapchain image
			.format = swapchain->format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		},
	};
	VkAttachmentReference depth_reference = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkAttachmentReference swapchain_output_reference = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpasses[] = {
		{ // 0 - forward pass
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.pDepthStencilAttachment = &depth_reference,
			.colorAttachmentCount = 1, .pColorAttachments = &swapchain_output_reference,
		},
		{ // 1 - interface pass
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.colorAttachmentCount = 1, .pColorAttachments = &swapchain_output_reference,
		},
	};
	VkSubpassDependency dependencies[] = {
		{ // Swapchain image has been acquired
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		},
		{ // The forward pass has finished drawing
			.srcSubpass = 0,
			.dstSubpass = 1,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		},
	};
	VkRenderPassCreateInfo renderpass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = COUNT_OF(attachments), .pAttachments = attachments,
		.subpassCount = COUNT_OF(subpasses), .pSubpasses = subpasses,
		.dependencyCount = COUNT_OF(dependencies), .pDependencies = dependencies
	};
	if (vkCreateRenderPass(device->device, &renderpass_info, NULL, &pass->render_pass)) {
		printf("Failed to create a render pass for the forward pass.\n");
		destroy_render_pass(pass, device);
		return 1;
	}

	// Create one framebuffer per swapchain image
	VkImageView framebuffer_attachments[2];
	VkFramebufferCreateInfo framebuffer_info = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = pass->render_pass,
		.attachmentCount = COUNT_OF(framebuffer_attachments),
		.pAttachments = framebuffer_attachments,
		.width = swapchain->extent.width,
		.height = swapchain->extent.height,
		.layers = 1
	};
	pass->framebuffer_count = swapchain->image_count;
	pass->framebuffers = malloc(sizeof(VkFramebuffer) * pass->framebuffer_count);
	memset(pass->framebuffers, 0, sizeof(VkFramebuffer) * pass->framebuffer_count);
	for (uint32_t i = 0; i != pass->framebuffer_count; ++i) {
		framebuffer_attachments[0] = render_targets->targets[i].depth_buffer.view;
		framebuffer_attachments[1] = swapchain->image_views[i];
		if (vkCreateFramebuffer(device->device, &framebuffer_info, NULL, &pass->framebuffers[i])) {
			printf("Failed to create a framebuffer for the main render pass.\n");
			destroy_render_pass(pass, device);
			return 1;
		}
	}
	return 0;
}


/*! Adds commands for rendering the imgui user interface to the given command
	buffer, which is currently being recorded.
	\return 0 on success.*/
int render_gui(VkCommandBuffer cmd, application_t* app, uint32_t swapchain_index) {
	interface_pass_t* pass = &app->interface_pass;
	if (get_imgui_frame(&pass->frames[swapchain_index], app->imgui))
		return 1;
	VkMappedMemoryRange gui_ranges[] = {
		{
			.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
			.memory = pass->geometry_allocation.memory,
			.offset = pass->geometries[swapchain_index].buffers[0].offset,
			.size = get_mapped_memory_range_size(&app->device, &pass->geometry_allocation, 2 * swapchain_index + 0),
		},
		{
			.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
			.memory = pass->geometry_allocation.memory,
			.offset = pass->geometries[swapchain_index].buffers[1].offset,
			.size = get_mapped_memory_range_size(&app->device, &pass->geometry_allocation, 2 * swapchain_index + 1),
		},
	};
	vkFlushMappedMemoryRanges(app->device.device, COUNT_OF(gui_ranges), gui_ranges);
	// Record all draw calls
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass->pipeline.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 
		app->interface_pass.pipeline.pipeline_layout, 0, 1, &pass->pipeline.descriptor_sets[swapchain_index], 0, NULL);
	vkCmdBindIndexBuffer(cmd, pass->geometries[swapchain_index].indices.buffer, 0, VK_INDEX_TYPE_UINT16);
	const VkDeviceSize offsets[1] = {0};
	vkCmdBindVertexBuffers(cmd, 0, 1, &pass->geometries[swapchain_index].vertices.buffer, offsets);
	for (uint32_t i = 0; i != pass->frames[swapchain_index].draw_count; ++i) {
		const imgui_draw_t* draw = &pass->frames[swapchain_index].draws[i];
		VkRect2D scissor = {
			.offset = {draw->scissor_x, draw->scissor_y},
			.extent = {draw->scissor_width, draw->scissor_height}
		};
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdDrawIndexed(cmd, (uint32_t) draw->triangle_count, 1, (uint32_t) draw->index_offset, 0, 0);
	}
	return 0;
}


/*! This function records commands for rendering a frame to the given swapchain
	image into the given command buffer
	\return 0 on success.*/
int record_render_frame_commands(VkCommandBuffer cmd, application_t* app, uint32_t swapchain_index) {
	const device_t* device = &app->device;
	// Start recording commands
	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	if (vkBeginCommandBuffer(cmd, &begin_info)) {
		printf("Failed to begin using a command buffer for rendering the scene.\n");
		return 1;
	}
	// Begin the render pass that renders the whole frame
	VkClearValue clear_values[] = {
		{.depthStencil = {.depth = 1.0f}},
		{.color = {.float32 = {1.0f, 1.0f, 1.0f, 1.0f} } },
	};
	VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = app->render_pass.render_pass,
		.framebuffer = app->render_pass.framebuffers[swapchain_index],
		.renderArea.offset = {0, 0},
		.renderArea.extent = app->swapchain.extent,
		.clearValueCount = COUNT_OF(clear_values), .pClearValues = clear_values
	};
	vkCmdBeginRenderPass(cmd, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
	// Render the scene to the swap chain
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->forward_pass.pipeline.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 
		app->forward_pass.pipeline.pipeline_layout, 0, 1, &app->forward_pass.pipeline.descriptor_sets[swapchain_index], 0, NULL);
	const VkDeviceSize offsets[32] = { 0 };
	vkCmdBindVertexBuffers(cmd, 0, app->forward_pass.vertex_buffer_count, app->forward_pass.vertex_buffers, offsets);
	vkCmdDraw(cmd, (uint32_t)app->scene.mesh.triangle_count * 3, app->render_settings.instance_count, 0, 0);
	// Run the interface pass
	vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
	if (app->render_settings.show_gui && !app->screenshot.path_hdr) {
		if (render_gui(cmd, app, swapchain_index)) {
			printf("Failed to render the user interface.\n");
			return 1;
		}
	}
	// The frame is rendered completely
	vkCmdEndRenderPass(cmd);

	// Finish recording
	if (vkEndCommandBuffer(cmd)) {
		printf("Failed to end using a command buffer for rendering the scene.\n");
		return 1;
	}
	return 0;
}


//! Frees objects and zeros
void destroy_frame_sync(frame_sync_t* sync, const device_t* device) {
	if (sync->image_acquired) vkDestroySemaphore(device->device, sync->image_acquired, NULL);
	memset(sync, 0, sizeof(*sync));
}

//! Creates synchronization objects for rendering a single frame
//! \return 0 on success.
int create_frame_sync(frame_sync_t* sync, const device_t* device) {
	memset(sync, 0, sizeof(*sync));
	VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	if (vkCreateSemaphore(device->device, &semaphore_info, NULL, &sync->image_acquired)) {
		printf("Failed to create a semaphore.\n");
		destroy_frame_sync(sync, device);
		return 1;
	}
	return 0;
}


//! Frees objects and zeros
void destroy_frame_queue(frame_queue_t* queue, const device_t* device) {
	for (uint32_t i = 0; i != queue->frame_count; ++i) {
		if (queue->workloads) {
			frame_workload_t* workload = &queue->workloads[i];
			if (workload->command_buffer)
				vkFreeCommandBuffers(device->device, device->command_pool, 1, &workload->command_buffer);
			if (workload->drawing_finished_fence)
				vkDestroyFence(device->device, workload->drawing_finished_fence, NULL);
		}
		if (queue->syncs)
			destroy_frame_sync(&queue->syncs[i], device);
	}
	free(queue->workloads);
	free(queue->syncs);
	memset(queue, 0, sizeof(*queue));
}

//! Creates the frame queue, including both synchronization objects and
//! command buffers
int create_frame_queue(frame_queue_t* queue, const device_t* device, const swapchain_t* swapchain) {
	memset(queue, 0, sizeof(*queue));
	// Create synchronization objects
	queue->frame_count = swapchain->image_count;
	queue->syncs = malloc(sizeof(frame_sync_t) * queue->frame_count);
	memset(queue->syncs, 0, sizeof(frame_sync_t) * queue->frame_count);
	for (uint32_t i = 0; i != queue->frame_count; ++i) {
		if (create_frame_sync(&queue->syncs[i], device)) {
			destroy_frame_queue(queue, device);
			return 1;
		}
	}
	// Allocate command buffers for rendering the scene to each of the swapchain
	// images
	VkCommandBufferAllocateInfo command_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = device->command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};
	queue->workloads = malloc(sizeof(frame_workload_t) * queue->frame_count);
	memset(queue->workloads, 0, sizeof(frame_workload_t) * queue->frame_count);
	for (uint32_t i = 0; i != queue->frame_count; ++i) {
		if (vkAllocateCommandBuffers(device->device, &command_info, &queue->workloads[i].command_buffer)) {
			printf("Failed to allocate command buffers for rendering.\n");
			destroy_frame_queue(queue, device);
			return 1;
		}
		VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		if (vkCreateFence(device->device, &fence_info, NULL, &queue->workloads[i].drawing_finished_fence)) {
			printf("Failed to create a fence.\n");
			destroy_frame_queue(queue, device);
			return 1;
		}
	}
	return 0;
}


//! Cleans up intermediate objects for taking a screenshot. The object is ready
//! to be reused for the next screenshot afterwards.
void destroy_screenshot(screenshot_t* screenshot, const device_t* device) {
	free(screenshot->path_png);
	free(screenshot->path_jpg);
	free(screenshot->path_hdr);
	destroy_images(&screenshot->staging, device);
	free(screenshot->ldr_copy);
	free(screenshot->hdr_copy);
	memset(screenshot, 0, sizeof(*screenshot));
}


/*! Sets output paths for a screenshot and thus kicks off taking a screenshot.
	Can be called any time, except when taking a screenshot is already in
	progress. If *.hdr is non-NULL, the others must be NULL.*/
void take_screenshot(screenshot_t* screenshot, const char* path_png, const char* path_jpg, const char* path_hdr) {
	if (path_hdr && (path_png || path_jpg)) {
		printf("Cannot mix LDR and HDR screenshots.\n");
		return;
	}
	if (screenshot->path_png || screenshot->path_jpg || screenshot->path_hdr) {
		printf("Cannot take another screenshot while a screenshot is already being taken.\n");
		return;
	}
	if(path_png) screenshot->path_png = copy_string(path_png);
	if(path_jpg) screenshot->path_jpg = copy_string(path_jpg);
	if(path_hdr) {
		screenshot->path_hdr = copy_string(path_hdr);
		screenshot->frame_bits = frame_bits_hdr_low;
	}
}


//! Helper for implement_screenshot(). Allocates staging memory and
//! intermediate buffers.
int create_screenshot_staging_buffers(screenshot_t* screenshot, const swapchain_t* swapchain, const device_t* device) {
	VkBool32 hdr_mode = (screenshot->path_hdr != NULL);
	// Create a staging image
	VkFormat source_format = swapchain->format;
	image_request_t staging_request = {
		.image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = source_format,
			.extent = { swapchain->extent.width, swapchain->extent.height, 1 },
			.mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_LINEAR,
			.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		}
	};
	if (create_images(&screenshot->staging, device, &staging_request, 1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		printf("Failed to create a staging image for taking a screenshot.\n");
		return 1;
	}
	// Allocate buffers for stb to read from
	uint32_t pixel_count = swapchain->extent.width * swapchain->extent.height;
	screenshot->ldr_copy = malloc(sizeof(uint8_t) * 3 * pixel_count * (hdr_mode ? 2 : 1));
	if(hdr_mode)
		screenshot->hdr_copy = malloc(sizeof(float) * 3 * pixel_count);
	return 0;
}


//! Helper for implement_screenshot(). More precisely, it copies contents of
//! the swapchain image first to the staging image and then to the appropriate
//! part of the LDR buffer.
int grab_screenshot_ldr(screenshot_t* screenshot, const swapchain_t* swapchain, const device_t* device, uint32_t swapchain_index) {
	// Wait for all rendering to finish
	if (vkDeviceWaitIdle(device->device)) {
		printf("Failed to wait for rendering to finish to take a screenshot.\n");
		return 1;
	}
	// Copy the swapchain image
	VkImage source_image = swapchain->images[swapchain_index];
	VkImageCopy region = {
		.srcSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = 1
		},
		.dstSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = 1
		},
		.extent = { swapchain->extent.width, swapchain->extent.height, 1 },
	};
	if (copy_images(device, 1, &source_image, &screenshot->staging.images[0].image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &region)) {
		printf("Failed to copy the swapchain image to a staging image for taking a screenshot.\n");
		return 1;
	}
	// Map the memory of the staging image
	uint8_t* staging_data;
	if (vkMapMemory(device->device, screenshot->staging.memories[0], screenshot->staging.images[0].memory_offset,
		screenshot->staging.images[0].memory_size, 0, (void**) &staging_data))
	{
		printf("Failed to map the host memory holding the screenshot.\n");
		return 1;
	}
	// Figure out what to do with the source format
	VkBool32 source_10_bit_hdr = VK_FALSE;
	uint32_t channel_permutation[3] = { 0, 1, 2 };
	switch (swapchain->format) {
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
		channel_permutation[0] = 2;
		channel_permutation[2] = 0;
		break;
	case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
		source_10_bit_hdr = VK_TRUE;
		channel_permutation[0] = 2;
		channel_permutation[2] = 0;
		break;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		source_10_bit_hdr = VK_TRUE;
		break;
	default:
		break;
	};
	// Query the row pitch
	VkImageSubresource subresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT };
	VkSubresourceLayout subresource_layout;
	vkGetImageSubresourceLayout(device->device, screenshot->staging.images[0].image, &subresource, &subresource_layout);
	if (subresource_layout.rowPitch % 4) {
		printf("Unexpected row pitch. Failed to take a screenshot.\n");
		return 1;
	}
	VkDeviceSize pixel_row_pitch = subresource_layout.rowPitch / 4;
	// Convert to an appropriate format for stb
	VkExtent3D extent = region.extent;
	uint8_t* ldr_copy = screenshot->ldr_copy;
	if (screenshot->frame_bits == frame_bits_hdr_high)
		ldr_copy += 3 * extent.width * extent.height;
	int stride = extent.width * 3 * sizeof(uint8_t);
	if (!source_10_bit_hdr) {
		for (uint32_t y = 0; y != extent.height; ++y) {
			for (uint32_t x = 0; x != extent.width; ++x) {
				VkDeviceSize source_index = y * pixel_row_pitch + x;
				VkDeviceSize index = y * extent.width + x;
				ldr_copy[index * 3 + channel_permutation[0]] = staging_data[source_index * 4 + 0];
				ldr_copy[index * 3 + channel_permutation[1]] = staging_data[source_index * 4 + 1];
				ldr_copy[index * 3 + channel_permutation[2]] = staging_data[source_index * 4 + 2];
			}
		}
	}
	else {
		for (uint32_t y = 0; y != extent.height; ++y) {
			for (uint32_t x = 0; x != extent.width; ++x) {
				VkDeviceSize source_index = y * pixel_row_pitch + x;
				VkDeviceSize index = y * extent.width + x;
				uint32_t pixel = ((uint32_t*) staging_data)[source_index];
				uint32_t red = (pixel & 0x3FF) >> 2;
				uint32_t green = (pixel & 0xFFC00) >> 12;
				uint32_t blue = (pixel & 0x3FF00000) >> 22;
				ldr_copy[index * 3 + channel_permutation[0]] = (uint8_t) red;
				ldr_copy[index * 3 + channel_permutation[1]] = (uint8_t) green;
				ldr_copy[index * 3 + channel_permutation[2]] = (uint8_t) blue;
			}
		}
	}
	vkUnmapMemory(device->device, screenshot->staging.memories[0]);
	return 0;
}


//! Helper for implement_screenshot(). Takes two LDR screenshots and turns then
//! into a single HDR screenshot.
void combine_ldr_screenshots_into_hdr(screenshot_t* screenshot) {
	VkExtent3D extent = screenshot->staging.images->image_info.extent;
	uint32_t pixel_count = extent.width * extent.height;
	uint32_t entry_count = 3 * pixel_count;
	for (uint32_t i = 0; i != entry_count; ++i) {
		uint16_t low_bits = screenshot->ldr_copy[i];
		uint16_t high_bits = screenshot->ldr_copy[i + entry_count];
		uint16_t half_bits = low_bits | (high_bits << 8);
		screenshot->hdr_copy[i] = half_to_float(half_bits);
	}
}


/*! Invoked once per frame just after submitting all drawing commands. If
	requested, the swapchain image is copied to an LDR buffer and stored as a
	screenshot. HDR screenshots arise from the combination of two LDR
	screenshots. 10-bit HDR is converted to 8-bit LDR, the alpha chennel is
	removed.
	\return 0 on success. On failure, rendering can proceed normally.*/
int implement_screenshot(screenshot_t* screenshot, const swapchain_t* swapchain, const device_t* device, uint32_t swapchain_index) {
	VkBool32 hdr_mode = (screenshot->path_hdr != NULL);
	if (!screenshot->path_png && !screenshot->path_jpg && !hdr_mode)
		return 0;
	// If we are just getting started, allocate staging memory
	if (screenshot->frame_bits != frame_bits_hdr_high) {
		if (create_screenshot_staging_buffers(screenshot, swapchain, device)) {
			destroy_screenshot(screenshot, device);
			return 1;
		}
	}
	// Grab swapchain contents and convert to LDR
	if (grab_screenshot_ldr(screenshot, swapchain, device, swapchain_index)) {
		destroy_screenshot(screenshot, device);
		return 1;
	}
	// Store the image
	if (screenshot->path_png) {
		int stride = swapchain->extent.width * 3 * sizeof(uint8_t);
		if (!stbi_write_png(screenshot->path_png, (int) swapchain->extent.width, (int) swapchain->extent.height, 3, screenshot->ldr_copy, stride)) {
			printf("Failed to store a screenshot to the *.png file at %s. Please check path and permissions.\n", screenshot->path_png);
			destroy_screenshot(screenshot, device);
			return 1;
		}
		printf("Wrote screenshot to %s.\n", screenshot->path_png);
	}
	if (screenshot->path_jpg) {
		if (!stbi_write_jpg(screenshot->path_jpg, (int) swapchain->extent.width, (int) swapchain->extent.height, 3, screenshot->ldr_copy, 70)) {
			printf("Failed to store a screenshot to the *.jpg file at %s. Please check path and permissions.\n", screenshot->path_jpg);
			destroy_screenshot(screenshot, device);
			return 1;
		}
		printf("Wrote screenshot to %s.\n", screenshot->path_jpg);
	}
	if (screenshot->path_hdr && screenshot->frame_bits == frame_bits_hdr_high) {
		// Combine both LDR frames into an HDR frame
		combine_ldr_screenshots_into_hdr(screenshot);
		// Store the screenshot
		if (!stbi_write_hdr(screenshot->path_hdr, (int) swapchain->extent.width, (int) swapchain->extent.height, 3, screenshot->hdr_copy)) {
			printf("Failed to store a screenshot to the *.hdr file at %s. Please check path and permissions.\n", screenshot->path_hdr);
			destroy_screenshot(screenshot, device);
			return 1;
		}
		printf("Wrote screenshot to %s.\n", screenshot->path_hdr);
	}
	if (screenshot->frame_bits == frame_bits_hdr_low)
		// The screenshot will be completed in the next frame
		screenshot->frame_bits = frame_bits_hdr_high;
	else
		destroy_screenshot(screenshot, device);
	return 0;
}


//! Destroys all objects associated with this application. Probably the last
//! thing you invoke before shutdown.
void destroy_application(application_t* app) {
	if(app->device.device)
		vkDeviceWaitIdle(app->device.device);
	destroy_frame_queue(&app->frame_queue, &app->device);
	destroy_interface_pass(&app->interface_pass, &app->device);
	destroy_forward_pass(&app->forward_pass, &app->device);
	destroy_render_pass(&app->render_pass, &app->device);
	destroy_render_targets(&app->render_targets, &app->device);
	destroy_constant_buffers(&app->constant_buffers, &app->device);
	destroy_scene(&app->scene, &app->device);
	destroy_experiment_list(&app->experiment_list);
	destroy_scene_specification(&app->scene_specification);
	destroy_swapchain(&app->swapchain, &app->device);
	destroy_vulkan_device(&app->device);
	destroy_imgui(app->imgui);
}


//! Callback to respond to window size changes
void glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height);


/*! Repeats all initialization procedures that need to be performed to
	implement the given update.
	\return 0 on success.*/
int update_application(application_t* app, const application_updates_t* update_in) {
	application_updates_t update = *update_in;
	// Perform a quick save if requested
	if (update.quick_save) quick_save(&app->scene_specification);
	// Check if a window resize is requested
	uint32_t width = (update.window_width != 0) ? update.window_width : app->swapchain.extent.width;
	uint32_t height = (update.window_height != 0) ? update.window_height : app->swapchain.extent.height;
	if (app->swapchain.extent.width != width || app->swapchain.extent.height != height) {
		glfwSetWindowSize(app->swapchain.window, (int) width, (int) height);
		update.recreate_swapchain = VK_TRUE;
	}
	// Return early, if there is nothing to update
	if (!update.startup && !update.recreate_swapchain && !update.reload_shaders
		&& !update.quick_load && !update.reload_scene && !update.change_shading)
		return 0;
	// Perform a quick load
	if (update.quick_load)
		quick_load(&app->scene_specification, &update);
	// Flag objects that need to be rebuilt because something changed directly
	VkBool32 swapchain = update.recreate_swapchain;
	VkBool32 scene = update.startup | update.reload_scene;
	VkBool32 render_targets = update.startup;
	VkBool32 render_pass = update.startup;
	VkBool32 constant_buffers = update.startup | update.change_shading;
	VkBool32 forward_pass = update.startup | update.change_shading | update.reload_shaders;
	VkBool32 interface_pass = update.startup | update.reload_shaders;
	VkBool32 frame_queue = update.startup;
	// Now propagate dependencies (as indicated by the parameter lists of
	// create functions)
	uint32_t max_dependency_path_length = 16;
	for (uint32_t i = 0; i != max_dependency_path_length; ++i) {
		render_targets |= swapchain;
		render_pass |= swapchain | render_targets;
		constant_buffers |= swapchain;
		forward_pass |= swapchain | scene | constant_buffers | render_targets;
		interface_pass |= swapchain | render_targets;
		frame_queue |= swapchain;
	}
	// Tear down everything that needs to be reinitialized in reverse order
	vkDeviceWaitIdle(app->device.device);
	if (frame_queue) destroy_frame_queue(&app->frame_queue, &app->device);
	if (interface_pass) destroy_interface_pass(&app->interface_pass, &app->device);
	if (forward_pass) destroy_forward_pass(&app->forward_pass, &app->device);
	if (constant_buffers) destroy_constant_buffers(&app->constant_buffers, &app->device);
	if (render_pass) destroy_render_pass(&app->render_pass, &app->device);
	if (render_targets) destroy_render_targets(&app->render_targets, &app->device);
	if (scene) destroy_scene(&app->scene, &app->device);
	// Attempt to recreate the swapchain and finish early if the window is
	// minimized
	if (swapchain) {
		int swapchain_result = create_or_resize_swapchain(&app->swapchain, &app->device, VK_TRUE, "", 0, 0, app->render_settings.v_sync);
		if (swapchain_result == 2)
			return 0;
		else if (swapchain_result) {
			printf("Swapchain resize failed.\n");
			return 1;
		}
	}
	// Rebuild everything else
	if (   (scene && load_scene(&app->scene, &app->device, app->scene_specification.source.file_path, app->scene_specification.source.texture_path, &app->render_settings.compression_params, app->render_settings.error_display != error_display_none))
		|| (render_targets && create_render_targets(&app->render_targets, &app->device, &app->swapchain))
		|| (render_pass && create_render_pass(&app->render_pass, &app->device, &app->swapchain, &app->render_targets))
		|| (constant_buffers && create_constant_buffers(&app->constant_buffers, &app->device, &app->swapchain, &app->scene_specification, &app->render_settings))
		|| (forward_pass && create_forward_pass(&app->forward_pass, &app->device, &app->swapchain, &app->scene, &app->constant_buffers, &app->render_targets, &app->render_pass, &app->render_settings))
		|| (interface_pass && create_interface_pass(&app->interface_pass, &app->device, app->imgui, &app->swapchain, &app->render_targets, &app->render_pass))
		|| (frame_queue && create_frame_queue(&app->frame_queue, &app->device, &app->swapchain)))
		return 1;
	return 0;
}


/*! Creates all objects needed by this application at start up. Once the
	function returns, everything is ready for rendering the first frame.
	\param experiment_index An index of an experiment in the experiment list
		that should be used for the initial configuration instead of the
		default configuration. An invalid index implies the default.
	\param v_sync_override Lets you force v-sync on or off.
	\return 0 on success.*/
int startup_application(application_t* app, int experiment_index, bool_override_t v_sync_override) {
	memset(app, 0, sizeof(*app));
	g_glfw_application = app;
	const char application_display_name[] = "Vulkan renderer";
	const char application_internal_name[] = "vulkan_renderer";
	// Create the device
	if (create_vulkan_device(&app->device, application_internal_name, 0, VK_TRUE)) {
		destroy_application(app);
		return 1;
	}
	// Define available experiments
	create_experiment_list(&app->experiment_list);
	// Specify the scene, settings and experiments
	if (experiment_index >= 0 && experiment_index < app->experiment_list.count) {
		const experiment_t* experiment = &app->experiment_list.experiments[experiment_index];
		// Set up the scene
		destroy_scene_source(&app->scene_specification.source);
		copy_scene_source(&app->scene_specification.source, &g_scene_sources[experiment->scene_index]);
		const char* quicksave_path = experiment->quick_save_path;
		if (!quicksave_path) {
			free(app->scene_specification.source.quick_save_path);
			app->scene_specification.source.quick_save_path = copy_string(quicksave_path);
		}
		quick_load(&app->scene_specification, NULL);
		// Set render settings
		app->render_settings = experiment->render_settings;
		if (v_sync_override != bool_override_none) app->render_settings.v_sync = v_sync_override;
	}
	else {
		specify_default_scene(&app->scene_specification);
		specify_default_render_settings(&app->render_settings);
	}
	// Create the swapchain
	if (create_or_resize_swapchain(&app->swapchain, &app->device, VK_FALSE, application_display_name, 1280, 1024, app->render_settings.v_sync)) {
		destroy_application(app);
		return 1;
	}
	glfwSetFramebufferSizeCallback(app->swapchain.window, &glfw_framebuffer_size_callback);
	// Prepare imgui for being used
	app->imgui = init_imgui(app->swapchain.window);
	
	// Load and create everything else
	application_updates_t update = { .startup = VK_TRUE };
	if (update_application(app, &update)) {
		destroy_application(app);
		return 1;
	}
	return 0;
}


//!	Checks if it is time to complete an experiment and to prepare the next one
//! and updates settings accordingly
void advance_experiments(screenshot_t* screenshot, application_updates_t* updates, experiment_list_t* list, scene_specification_t* scene, render_settings_t* render_settings) {
	++list->frame_index;
	if (list->next > list->count) {
		// Experiments are not running
		list->state = experiment_state_rendering;
		return;
	}
	if (list->state == experiment_state_new_experiment) {
		// Define when this experiment will end (offsets in seconds and in
		// frames)
		list->next_setup_time = glfwGetTime() + 1.0;
		list->next_setup_frame = list->frame_index + 110;
		list->state = experiment_state_rendering;
	}
	else if (list->state == experiment_state_screenshot_frame_1) {
		// We took a screenshot just now. Either we need to end all experiments
		if (list->next >= list->count) {
			list->state = experiment_state_rendering;
			list->experiment = NULL;
			list->next = list->count + 1;
			return;
		}
		// Or prepare the next one
		list->experiment = &list->experiments[list->next];
		// Adjust the resolution as needed
		updates->window_width = list->experiment->width;
		updates->window_height = list->experiment->height;
		// Prepare the new scene
		if (strcmp(scene->source.file_path, g_scene_sources[list->experiment->scene_index].file_path) != 0) {
			destroy_scene_source(&scene->source);
			copy_scene_source(&scene->source, &g_scene_sources[list->experiment->scene_index]);
			updates->reload_scene = VK_TRUE;
		}
		// Prepare camera and lights
		if (list->experiment->quick_save_path) {
			free(scene->source.quick_save_path);
			scene->source.quick_save_path = copy_string(list->experiment->quick_save_path);
		}
		updates->quick_load = VK_TRUE;
		// Ensure that render settings are applied properly
		if (render_settings->v_sync != list->experiment->render_settings.v_sync)
			updates->recreate_swapchain = VK_TRUE;
		updates->reload_scene = VK_TRUE;
		updates->change_shading = VK_TRUE;
		(*render_settings) = list->experiment->render_settings;
		// Proceed
		list->state = experiment_state_new_experiment;
		++list->next;
	}
	else if (list->state == experiment_state_screenshot_frame_0) {
		// Taking a screenshot may take two frames, so we have two states for
		// that
		list->state = experiment_state_screenshot_frame_1;
	}
	else if (list->state == experiment_state_rendering && list->next_setup_time <= glfwGetTime() && list->next_setup_frame <= list->frame_index) {
		// Take a screenshot for the current experiment (if any)
		if (list->experiment) {
			char* full_path = format_float(list->experiment->screenshot_path, get_frame_time() * 1.0e3f);
			if (list->experiment->use_hdr)
				take_screenshot(screenshot, NULL, NULL, full_path);
			else
				take_screenshot(screenshot, full_path, NULL, NULL);
			free(full_path);
		}
		// End the current experiment
		list->state = experiment_state_screenshot_frame_0;
	}
}


void glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height) {
	application_t* app = g_glfw_application;
	swapchain_t* swapchain = app ? &app->swapchain : NULL;
	if (swapchain) {
	    // Query the new framebuffer size and do an early out if that is the
	    // swapchain size already
        int framebuffer_width = 0, framebuffer_height = 0;
        glfwGetFramebufferSize(swapchain->window, &framebuffer_width, &framebuffer_height);
        if (framebuffer_width == (int) swapchain->extent.width && framebuffer_height == (int) swapchain->extent.height)
            return;
        // Implement the update
		application_updates_t updates = {VK_FALSE};
		updates.recreate_swapchain = VK_TRUE;
		if (update_application(app, &updates)) {
			printf("Swapchain resize failed.\n");
			glfwSetWindowShouldClose(window, 1);
			return;
		}
	}
}


/*! Given a key code such as GLFW_KEY_F5, this function returns whether this
	key is currently pressed but was not pressed when this function was last
	invoked for this key (or this function was not invoked for this key 
	yet).*/
VkBool32 key_pressed(GLFWwindow* window, int key) {
	if (key < 0 || key >= GLFW_KEY_LAST)
		return VK_FALSE;
	static int key_state[GLFW_KEY_LAST + 1] = {GLFW_RELEASE};
	int current_state = glfwGetKey(window, key);
	VkBool32 result = (current_state == GLFW_PRESS && key_state[key] == GLFW_RELEASE);
	key_state[key] = current_state;
	return result;
}


/*! Implements user input and scene updates. Invoke this once per frame.
	\return 0 if the application should keep running, 1 if it needs to end.*/
int handle_frame_input(application_t* app) {
	record_frame_time();
	// Define the user interface for the current frame
	application_updates_t updates = { VK_FALSE };
	specify_user_interface(&updates, app, get_frame_time());
	// Pressing escape ends the application
	GLFWwindow* window = app->swapchain.window;
	if (key_pressed(window, GLFW_KEY_ESCAPE)) {
		printf("Escape pressed. Shutting down.\n");
		return 1;
	}
	// Reload shaders on request
	if (key_pressed(window, GLFW_KEY_F5)) {
		printf("Reloading all shaders.\n");
		updates.reload_shaders = VK_TRUE;
	}
	// Quick save and quick load
	if (key_pressed(window, GLFW_KEY_F3)) {
		printf("Quick save.\n");
		updates.quick_save = VK_TRUE;
	}
	if (key_pressed(window, GLFW_KEY_F4)) {
		printf("Quick load.\n");
		updates.quick_load = VK_TRUE;
	}
	// Take a screenshot
	if (key_pressed(app->swapchain.window, GLFW_KEY_F10) || key_pressed(app->swapchain.window, GLFW_KEY_F12))
		take_screenshot(&app->screenshot, "data/screenshot.png", "data/screenshot.jpg", NULL);
	// Toggle the user interface
	app->render_settings.show_gui ^= key_pressed(window, GLFW_KEY_F1);
	// Toggle v-sync
	if (key_pressed(window, GLFW_KEY_F2)) {
		app->render_settings.v_sync ^= 1;
		updates.recreate_swapchain = VK_TRUE;
	}
	// If things went badly in the previous frame, we may want to resize the
	// swapchain
	if (app->frame_queue.recreate_swapchain) {
		app->frame_queue.recreate_swapchain = VK_FALSE;
		updates.recreate_swapchain = VK_TRUE;
	}
	// Cycle through experiments (if they are ongoing)
	advance_experiments(&app->screenshot, &updates, &app->experiment_list, &app->scene_specification, &app->render_settings);
	// Handle updates
	if (update_application(app, &updates)) {
		printf("Failed to apply changed settings. Shutting down.\n");
		return 1;
	}
	// Update the camera
	float time_delta = control_camera(&app->scene_specification.camera, app->swapchain.window);
	// Play back the animation
	float new_time = app->scene_specification.time + time_delta * app->render_settings.playback_speed;
	float total_time = app->scene.animation.time_step * (app->scene.animation.time_sample_count - 1);
	new_time = (new_time - app->scene.animation.time_start) / total_time - floorf((new_time - app->scene.animation.time_start) / total_time);
	new_time = new_time * total_time + app->scene.animation.time_start;
	app->scene_specification.time = new_time;
	return 0;
}


//! Writes constants matching the current state of the application to the given
//! memory location
void write_constants(void* data, application_t* app) {
	const scene_t* scene = &app->scene;
	const animation_t* animation = &scene->animation;
	const first_person_camera_t* camera = &app->scene_specification.camera;
	float light_inclination = app->scene_specification.light_inclination;
	float light_azimuth = app->scene_specification.light_azimuth;
	float time = app->scene_specification.time;
	const float* light_irradiance = app->scene_specification.light_irradiance;
	double cursor_position[2];
	glfwGetCursorPos(app->swapchain.window, &cursor_position[0], &cursor_position[1]);
	per_frame_constants_t constants = {
		.mesh_dequantization_factor = {scene->mesh.dequantization_factor[0], scene->mesh.dequantization_factor[1], scene->mesh.dequantization_factor[2]},
		.mesh_dequantization_summand = {scene->mesh.dequantization_summand[0], scene->mesh.dequantization_summand[1], scene->mesh.dequantization_summand[2]},
		.camera_position_world_space = {camera->position_world_space[0], camera->position_world_space[1], camera->position_world_space[2]},
		.light_direction_world_space = {cosf(light_azimuth) * sinf(light_inclination), sinf(light_azimuth) * sinf(light_inclination), cosf(light_inclination)},
		.light_irradiance = {light_irradiance[0], light_irradiance[1], light_irradiance[2]},
		.viewport_size = app->swapchain.extent,
		.cursor_position = { (int32_t) cursor_position[0], (int32_t) cursor_position[1] },
		.error_factor = 1.0f / (log2(10.0f) * (app->render_settings.error_max_exponent - app->render_settings.error_min_exponent)),
		.error_summand = -app->render_settings.error_min_exponent / (app->render_settings.error_max_exponent - app->render_settings.error_min_exponent),
		.exposure_factor = app->render_settings.exposure_factor,
		.roughness = app->render_settings.roughness,
		.frame_bits = app->screenshot.frame_bits,
		.time_tex_coord = ((time - animation->time_start) / animation->time_step + 0.5f) / animation->time_sample_count,
		.inv_bone_count = 1.0f / animation->bone_count,
		.animation_column_spacing = 1.0f / (2 * animation->bone_count),
		.animation_half_column_spacing = 1.0f / (4 * animation->bone_count),
	};
	memcpy(constants.animation_dequantization, scene->animation.dequantization_constants, sizeof(constants.animation_dequantization));
	get_world_to_projection_space(constants.world_to_projection_space, camera, get_aspect_ratio(&app->swapchain));
	// Construct the transform that produces ray directions from pixel
	// coordinates
	float viewport_transform[4];
	viewport_transform[0] = 2.0f / app->swapchain.extent.width;
	viewport_transform[1] = 2.0f / app->swapchain.extent.height;
	viewport_transform[2] = 0.5f * viewport_transform[0] - 1.0f;
	viewport_transform[3] = 0.5f * viewport_transform[1] - 1.0f;
	float projection_to_world_space_no_translation[4][4];
	float world_to_projection_space_no_translation[4][4];
	memcpy(world_to_projection_space_no_translation, constants.world_to_projection_space, sizeof(world_to_projection_space_no_translation));
	world_to_projection_space_no_translation[0][3] = 0.0f;
	world_to_projection_space_no_translation[1][3] = 0.0f;
	world_to_projection_space_no_translation[2][3] = 0.0f;
	matrix_inverse(projection_to_world_space_no_translation, world_to_projection_space_no_translation);
	float pixel_to_ray_direction_projection_space[4][3] = {
		{viewport_transform[0], 0.0f,	viewport_transform[2]},
		{0.0f, viewport_transform[1],	viewport_transform[3]},
		{0.0f,					0.0f,	1.0f},
		{0.0f,					0.0f,	1.0f},
	};
	for (uint32_t i = 0; i != 3; ++i)
		for (uint32_t j = 0; j != 3; ++j)
			for (uint32_t k = 0; k != 4; ++k)
				constants.pixel_to_ray_direction_world_space[i][j] += projection_to_world_space_no_translation[i][k] * pixel_to_ray_direction_projection_space[k][j];
	memcpy(data, &constants, sizeof(constants));
}


/*! Renders a frame for the current state of the scene.
	\note This function mostly takes care of synchronization and submits
		previously created command buffers for rendering.
	\see record_render_frame_commands()
	\return 0 if the application should keep running, 1 if something went
		terribly wrong and it has to end.*/
int render_frame(application_t* app) {
	// Get synchronization objects
	frame_queue_t* queue = &app->frame_queue;
	queue->sync_index = (queue->sync_index + 1) % queue->frame_count;
	frame_sync_t* sync = &queue->syncs[queue->sync_index];
	// Acquire the next swapchain image
	uint32_t swapchain_index;
	if (vkAcquireNextImageKHR(app->device.device, app->swapchain.swapchain, UINT64_MAX, sync->image_acquired, NULL, &swapchain_index)) {
		printf("Failed to acquire the next image from the swapchain.\n");
		return 1;
	}
	frame_workload_t* workload = &queue->workloads[swapchain_index];
	// Perform GPU-CPU synchronization to be sure that resources that we are
	// going to overwrite now are no longer used for rendering
	if (workload->used) {
		VkResult fence_result;
		do {
			fence_result = vkWaitForFences(app->device.device, 1, &workload->drawing_finished_fence, VK_TRUE, 100000000);
		} while (fence_result == VK_TIMEOUT);
		if (fence_result != VK_SUCCESS) {
			printf("Failed to wait for rendering of a frame to finish.\n");
			return 1;
		}
		if (vkResetFences(app->device.device, 1, &workload->drawing_finished_fence)) {
			printf("Failed to reset a fence for reuse in upcoming frames.\n");
			return 1;
		}
	}
	workload->used = VK_TRUE;
	// Update the constant buffer
	write_constants((char*) app->constant_buffers.data + app->constant_buffers.buffers.buffers[swapchain_index].offset, app);
	VkMappedMemoryRange constant_range = {
		.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
		.memory = app->constant_buffers.buffers.memory,
		.size = get_mapped_memory_range_size(&app->device, &app->constant_buffers.buffers, swapchain_index),
		.offset = app->constant_buffers.buffers.buffers[swapchain_index].offset
	};
	vkFlushMappedMemoryRanges(app->device.device, 1, &constant_range);
	// Record the command buffer for rendering
	if (record_render_frame_commands(workload->command_buffer, app, swapchain_index)) {
		printf("Failed to record a command buffer for rendering the scene.\n");
		return 1;
	}
	// Queue the command buffer for rendering
	VkPipelineStageFlags destination_stage_masks[] = {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSubmitInfo render_submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &workload->command_buffer,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &sync->image_acquired,
		.pWaitDstStageMask = destination_stage_masks,
	};
	if (vkQueueSubmit(app->device.queue, 1, &render_submit_info, workload->drawing_finished_fence)) {
		printf("Failed to submit the command buffer for rendering a frame to the queue.\n");
		return 1;
	}
	// Take a screenshot if requested
	implement_screenshot(&app->screenshot, &app->swapchain, &app->device, swapchain_index);
	// Present the image in the window
	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.swapchainCount = 1,
		.pSwapchains = &app->swapchain.swapchain,
		.pImageIndices = &swapchain_index
	};
	VkResult present_result;
	if (present_result = vkQueuePresentKHR(app->device.queue, &present_info)) {
		printf("Failed to present the rendered frame to the window. Error code %d. Attempting a swapchain resize.\n", present_result);
		app->frame_queue.recreate_swapchain = VK_TRUE;
	}
	return 0;
}


int main(int argc, char** argv) {
	// Parse settings, e.g. which experiment should be shown
	int experiment = -1;
	bool_override_t v_sync_override = bool_override_none;
	bool_override_t gui_override = bool_override_none;
	for (int i = 1; i < argc; ++i) {
		const char* arg = argv[i];
		if (arg[0] == '-' && arg[1] == 'e') sscanf(arg + 2, "%d", &experiment);
		if (strcmp(arg, "-no_v_sync") == 0) v_sync_override = bool_override_false;
		if (strcmp(arg, "-v_sync") == 0) v_sync_override = bool_override_true;
		if (strcmp(arg, "-no_gui") == 0) gui_override = bool_override_false;
		if (strcmp(arg, "-gui") == 0) gui_override = bool_override_true;
	}
	// Start the application
	application_t app;
	if (startup_application(&app, experiment, v_sync_override)) {
		printf("Application startup has failed.\n");
		return 1;
	}
	if (gui_override != bool_override_none) app.render_settings.show_gui = gui_override;
	// Main loop
	while (!glfwWindowShouldClose(app.swapchain.window)) {
		glfwPollEvents();
		// Check whether the window is minimized
		if (app.swapchain.swapchain) {
			if (handle_frame_input(&app)) break;
			if (render_frame(&app)) break;
		}
	}
	// Clean up
	destroy_application(&app);
	return 0;
}
