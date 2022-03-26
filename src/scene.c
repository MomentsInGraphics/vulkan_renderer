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


#include "scene.h"
#include "textures.h"
#include "string_utilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* get_material_texture_suffix(material_texture_type_t type) {
	switch (type) {
	case material_texture_type_base_color: return "diffuse";
	default: return NULL;
	}
}


/*! Given a mesh with count variables set as appropriate, this function creates
	the required buffers and allocates and binds memory for them. It does not
	fill them with data.
	\param staging Pass 1 to indicate that the created buffers are used as
		staging buffer or 0 to indicate that they are device local.
	\return 0 on success.*/
int create_mesh(mesh_t* mesh, const device_t* device, VkBool32 staging, VkBool32 force_ground_truth_blend_attributes) {
	VkMemoryPropertyFlags memory_properties = staging
		? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		: VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
	VkMemoryPropertyFlags usage = staging
		? VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		: (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	VkMemoryPropertyFlags table_usage = staging
		? VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		: (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	// Figure out if ground truth blend attributes should be available
	mesh->store_ground_truth = force_ground_truth_blend_attributes;
	if (mesh->compression_params.method == blend_attribute_compression_none)
		mesh->store_ground_truth = VK_TRUE;
	// Create the buffers
	VkBufferCreateInfo buffer_infos[mesh_buffer_count] = { 0 };
	VkDeviceSize vertex_size = 16 + mesh->compression_params.vertex_size;
	if (mesh->store_ground_truth && mesh->compression_params.method != blend_attribute_compression_none)
		vertex_size += (sizeof(float) + sizeof(uint16_t)) * mesh->compression_params.max_bone_count;
	mesh->vertices.size = vertex_size * 3 * mesh->triangle_count;
	mesh->bone_index_table.size = sizeof(uint16_t) * mesh->max_tuple_count * mesh->compression_params.max_bone_count;
	mesh->material_indices.size = sizeof(uint8_t) * mesh->triangle_count;
	for (uint32_t i = 0; i != mesh_buffer_count; ++i) {
		buffer_infos[i].sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		if (mesh->buffers[i].size == 0)
			mesh->buffers[i].size = 1;
		buffer_infos[i].size = mesh->buffers[i].size;
		buffer_infos[i].usage = usage;
	}
	buffer_infos[mesh_buffer_type_bone_index_table].usage = table_usage;
	buffer_infos[mesh_buffer_type_material_indices].usage = table_usage;
	buffers_t buffers;
	if (create_buffers(&buffers, device, buffer_infos, mesh_buffer_count, memory_properties)) {
		printf("Failed to allocate %smemory for a mesh with %llu triangles.\n", staging ? "staging " : "", mesh->triangle_count);
		return 1;
	}
	memcpy(mesh->buffers, buffers.buffers, sizeof(mesh->buffers));
	free(buffers.buffers);
	mesh->memory = buffers.memory;
	mesh->size = buffers.size;
	// Create the views
	if (!staging) {
		VkFormat formats[mesh_buffer_count] = { VK_FORMAT_UNDEFINED };
		uint32_t bone_count_remainder = mesh->compression_params.max_bone_count % 4;
		if (bone_count_remainder == 0) {
			formats[mesh_buffer_type_bone_index_table] = VK_FORMAT_R16G16B16A16_UINT;
			mesh->tuple_vector_size = 4;
		}
		else if (bone_count_remainder == 2) {
			formats[mesh_buffer_type_bone_index_table] = VK_FORMAT_R16G16_UINT;
			mesh->tuple_vector_size = 2;
		}
		else {
			formats[mesh_buffer_type_bone_index_table] = VK_FORMAT_R16_UINT;
			mesh->tuple_vector_size = 1;
		}
		formats[mesh_buffer_type_material_indices] = VK_FORMAT_R8_UINT;
		for (uint32_t i = 0; i != mesh_buffer_count; ++i) {
			if (formats[i] != VK_FORMAT_UNDEFINED) {
				VkBufferViewCreateInfo view_info = {
					.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
					.buffer = mesh->buffers[i].buffer,
					.format = formats[i],
					.range = mesh->buffers[i].size
				};
				if (vkCreateBufferView(device->device, &view_info, NULL, &mesh->buffer_views[i])) {
					printf("Failed to create a view for buffer %u of a mesh.\n", i);
					return 1;
				}
			}
		}
	}
	return 0;
}


//! Frees and nulls the given mesh
void destroy_mesh(mesh_t* mesh, const device_t* device) {
	for (uint32_t i = 0; i != mesh_buffer_count; ++i) {
		if (mesh->buffers[i].buffer) vkDestroyBuffer(device->device, mesh->buffers[i].buffer, NULL);
		if (mesh->buffer_views[i]) vkDestroyBufferView(device->device, mesh->buffer_views[i], NULL);
	}
	if (mesh->memory) vkFreeMemory(device->device, mesh->memory, NULL);
	memset(mesh, 0, sizeof(*mesh));
}


//! Frees and nulls the given materials
void destroy_materials(materials_t* materials, const device_t* device) {
	if (materials->material_names) {
		for (uint64_t i = 0; i != materials->material_count; ++i)
			free(materials->material_names[i]);
		free(materials->material_names);
	}
	destroy_images(&materials->textures, device);
	if (materials->sampler) vkDestroySampler(device->device, materials->sampler, NULL);
	memset(materials, 0, sizeof(*materials));
}


//! Frees and nulls the given animation
void destroy_animation(animation_t* animation, const device_t* device) {
	if (animation->sampler) vkDestroySampler(device->device, animation->sampler, NULL);
	destroy_images(&animation->texture, device);
	memset(animation, 0, sizeof(*animation));
}


//! Objects that are needed during scene loading and destroyed before that
//! method finishes
typedef struct scene_temporary_s {
	//! The file handle for the *.vks file
	FILE* file;
	//! Holds vertex positions from the scene file
	uint64_t* positions;
	//! Holds vertex normal vectors and texture coordinates from the scene file
	uint16_t* normals_and_tex_coords;
	//! Uncompressed vertex bone indices from the scene file (possibly already
	//! post-processed
	uint16_t* bone_indices;
	//! Uncompressed vertex bone weights (including the largest one) from the
	//! scene file (possibly already post-processed)
	float* bone_weights;
} scene_temporary_t;


//! Frees and nulls the given temporary data
void destroy_scene_temporary(scene_temporary_t* temp , const device_t* device) {
	if (temp->file) fclose(temp->file);
	free(temp->positions);
	free(temp->normals_and_tex_coords);
	free(temp->bone_indices);
	free(temp->bone_weights);
	memset(temp, 0, sizeof(*temp));
}


int load_scene(scene_t* scene, const device_t* device, const char* file_path, const char* texture_path, const blend_attribute_compression_parameters_t* compression_params, VkBool32 force_ground_truth_blend_attributes) {
	// Clear the output object
	memset(scene, 0, sizeof(*scene));
	// Open the source file
	scene_temporary_t temp = { 0 };
	temp.file = fopen(file_path, "rb");
	if (!temp.file) {
		printf("Failed to open the scene file at %s.\n", file_path);
		destroy_scene(scene, device);
		return 1;
	}
	// Read the header
	uint32_t file_marker, version;
	fread(&file_marker, sizeof(file_marker), 1, temp.file);
	fread(&version, sizeof(version), 1, temp.file);
	if (file_marker != 0xabcabc || version != 2) {
		printf("The scene file at path %s is invalid or unsupported. The format marker is 0x%x, the version is %d.\n", file_path, file_marker, version);
		destroy_scene_temporary(&temp, device);
		destroy_scene(scene, device);
		return 1;
	}
	fread(&scene->materials.material_count, sizeof(uint64_t), 1, temp.file);
	fread(&scene->mesh.triangle_count, sizeof(uint64_t), 1, temp.file);
	fread(scene->mesh.dequantization_factor, sizeof(float), 3, temp.file);
	fread(scene->mesh.dequantization_summand, sizeof(float), 3, temp.file);
	uint64_t max_bone_count;
	fread(&max_bone_count, sizeof(uint64_t), 1, temp.file);
	scene->mesh.compression_params = *compression_params;
	scene->mesh.compression_params.max_bone_count = (uint32_t) max_bone_count;
	fread(&scene->mesh.max_tuple_count, sizeof(uint64_t), 1, temp.file);
	fread(&scene->animation.time_start, sizeof(float), 1, temp.file);
	fread(&scene->animation.time_step, sizeof(float), 1, temp.file);
	fread(&scene->animation.time_sample_count, sizeof(uint64_t), 1, temp.file);
	fread(&scene->animation.bone_count, sizeof(uint64_t), 1, temp.file);
	printf("Triangle count: %llu\n", scene->mesh.triangle_count);
	printf("Max bone influence count: %llu\n", max_bone_count);
	printf("Maximal bone index tuple count: %llu\n", scene->mesh.max_tuple_count);
	printf("Frame count: %llu\n", scene->animation.time_sample_count);
	printf("Bone count: %llu\n", scene->animation.bone_count);
	// If there are no triangles, abort
	if (scene->mesh.triangle_count == 0) {
		printf("The scene file at path %s is completely empty, i.e. it holds 0 triangles.\n", file_path);
		destroy_scene_temporary(&temp, device);
		destroy_scene(scene, device);
		return 1;
	}
	// Read material names
	scene->materials.material_names = malloc(sizeof(char*) * scene->materials.material_count);
	memset(scene->materials.material_names, 0, sizeof(char*) * scene->materials.material_count);
	for (uint64_t i = 0; i != scene->materials.material_count; ++i) {
		uint64_t name_length;
		fread(&name_length, sizeof(name_length), 1, temp.file);
		scene->materials.material_names[i] = malloc(sizeof(char) * (name_length + 1));
		fread(scene->materials.material_names[i], sizeof(char), name_length + 1, temp.file);
	}

	// Allocate some space for vertex data
	uint64_t vertex_count = scene->mesh.triangle_count * 3;
	VkDeviceSize position_size = sizeof(uint64_t);
	temp.positions = (uint64_t*) malloc(sizeof(uint64_t) * vertex_count);
	VkDeviceSize normal_and_tex_coord_size = sizeof(uint16_t) * 4;
	temp.normals_and_tex_coords = (uint16_t*) malloc(normal_and_tex_coord_size * vertex_count);
	VkDeviceSize bone_indices_size = sizeof(uint16_t) * scene->mesh.compression_params.max_bone_count;
	temp.bone_indices = (uint16_t*) malloc(bone_indices_size * vertex_count);
	VkDeviceSize bone_weights_size = sizeof(float) * scene->mesh.compression_params.max_bone_count;
	temp.bone_weights = (float*) malloc(bone_weights_size * vertex_count);
	// Read the vertex data
	fread(temp.positions, position_size, vertex_count, temp.file);
	fread(temp.normals_and_tex_coords, normal_and_tex_coord_size, vertex_count, temp.file);
	fread(temp.bone_indices, bone_indices_size, vertex_count, temp.file);
	fread(temp.bone_weights, bone_weights_size, vertex_count, temp.file);

	// Allocate staging buffers for the mesh
	mesh_t empty_mesh = scene->mesh;
	uint32_t old_max_bone_count = scene->mesh.compression_params.max_bone_count;
	scene->mesh.compression_params.max_bone_count = compression_params->max_bone_count;
	if (create_mesh(&scene->mesh, device, VK_TRUE, force_ground_truth_blend_attributes)) {
		printf("Failed to create staging buffers and allocate memory for meshes of the scene file at path %s. It has %llu triangles.\n",
			file_path, scene->mesh.triangle_count);
		destroy_scene_temporary(&temp, device);
		destroy_scene(scene, device);
		return 1;
	}
	// Map memory for the staging buffers
	char* staging_data;
	if (vkMapMemory(device->device, scene->mesh.memory, 0, scene->mesh.size, 0, (void**) &staging_data)) {
		printf("Failed to map memory of the staging buffer for meshes of the scene file at path %s.\n", file_path);
		destroy_scene_temporary(&temp, device);
		destroy_scene(scene, device);
		return 1;
	}
	// Read material indices for each triangle
	fread(staging_data + scene->mesh.buffers[mesh_buffer_type_material_indices].offset, scene->mesh.buffers[mesh_buffer_type_material_indices].size, 1, temp.file);

	// Reduce the bone count per vertex as requested
	if (old_max_bone_count != compression_params->max_bone_count) {
		double reduction_start_time = glfwGetTime();
		if (reduce_bone_count(
			temp.bone_indices, sizeof(uint16_t) * compression_params->max_bone_count, temp.bone_weights, sizeof(float) * compression_params->max_bone_count,
			temp.bone_indices, bone_indices_size, temp.bone_weights, bone_weights_size,
			compression_params->max_bone_count, old_max_bone_count, scene->mesh.triangle_count * 3, VK_TRUE))
		{
			printf("Failed to reduce the number of bone influences per vertex in the scene file at path %s from %u to %u.\n", file_path, old_max_bone_count, compression_params->max_bone_count);
			vkUnmapMemory(device->device, scene->mesh.memory);
			destroy_scene_temporary(&temp, device);
			destroy_scene(scene, device);
			return 1;
		}
		printf("%.3f seconds to reduce the bone count to %u bones per vertex.\n", (float) (glfwGetTime() - reduction_start_time), compression_params->max_bone_count);
	}
	bone_indices_size = sizeof(uint16_t) * compression_params->max_bone_count;
	bone_weights_size = sizeof(float) * compression_params->max_bone_count;
	// Perform compression of blend attributes
	uint64_t table_size = 1;
	VkDeviceSize total_vertex_size = scene->mesh.vertices.size / vertex_count;
	if (compression_params->method != blend_attribute_compression_none) {
		double compression_start_time = glfwGetTime();
		VkDeviceSize compressed_offset = position_size + normal_and_tex_coord_size;
		if (scene->mesh.store_ground_truth)
			compressed_offset += bone_indices_size + bone_weights_size;
		if (compress_blend_attribute_buffers(
			(uint16_t*) (staging_data + scene->mesh.bone_index_table.offset), &table_size, staging_data + scene->mesh.vertices.offset + compressed_offset, total_vertex_size,
			temp.bone_indices, bone_indices_size, temp.bone_weights, bone_weights_size,
			compression_params, scene->mesh.triangle_count * 3, scene->mesh.max_tuple_count))
		{
			if (table_size > scene->mesh.max_tuple_count)
				printf("Failed to compress blend attributes for the scene file at path %s. The table of tuple indices needs to have %llu entries but only offers space for %llu entries.\n", file_path, table_size, scene->mesh.max_tuple_count);
			else
				printf("Failed to compress blend attributes for the scene file at path %s. Please check the parameters.\n", file_path);
			vkUnmapMemory(device->device, scene->mesh.memory);
			destroy_scene_temporary(&temp, device);
			destroy_scene(scene, device);
			return 1;
		}
		printf("Remaining bone index tuple count: %llu\n", table_size);
		printf("%.3f seconds to compress blend attributes for %llu vertices.\n", (float) (glfwGetTime() - compression_start_time), scene->mesh.triangle_count * 3);
	}
	// Copy over positions, normals and tex coords
	char* vertex = staging_data + scene->mesh.vertices.offset;
	for (uint64_t i = 0; i != vertex_count; ++i, vertex += total_vertex_size) {
		memcpy(vertex, temp.positions + i, position_size);
		memcpy(vertex + position_size, temp.normals_and_tex_coords + 4 * i, normal_and_tex_coord_size);
	}
	// Copy (reduced) ground truth blend attributes to staging buffers
	if (scene->mesh.store_ground_truth) {
		uint32_t max_bone_count = compression_params->max_bone_count;
		char* ground_truth = staging_data + scene->mesh.vertices.offset + position_size + normal_and_tex_coord_size;
		for (uint64_t i = 0; i != vertex_count; ++i, ground_truth += total_vertex_size) {
			memcpy(ground_truth, temp.bone_weights + max_bone_count * i, bone_weights_size);
			memcpy(ground_truth + bone_weights_size, temp.bone_indices + max_bone_count * i, bone_indices_size);
		}
	}
	// Unmap staging memory
	vkUnmapMemory(device->device, scene->mesh.memory);
	// Allocate device local mesh buffers
	mesh_t staging_mesh = scene->mesh;
	scene->mesh = empty_mesh;
	scene->mesh.compression_params = *compression_params;
	scene->mesh.max_tuple_count = table_size;
	if (create_mesh(&scene->mesh, device, VK_FALSE, force_ground_truth_blend_attributes)) {
		printf("Failed to create device buffers and allocate memory for meshes of the scene file at path %s. It has %llu triangles.\n",
			file_path, scene->mesh.triangle_count);
		destroy_scene_temporary(&temp, device);
		destroy_mesh(&staging_mesh, device);
		destroy_scene(scene, device);
		return 1;
	}
	// Perform the mesh copy
	VkBuffer staging_mesh_buffers[COUNT_OF(staging_mesh.buffers)];
	VkBuffer mesh_buffers[COUNT_OF(staging_mesh.buffers)];
	VkBufferCopy buffer_regions[mesh_buffer_count];
	memset(buffer_regions, 0, sizeof(buffer_regions));
	for (uint32_t i = 0; i != mesh_buffer_count; ++i) {
		staging_mesh_buffers[i] = staging_mesh.buffers[i].buffer;
		mesh_buffers[i] = scene->mesh.buffers[i].buffer;
		buffer_regions[i].size = scene->mesh.buffers[i].size;
	}
	int result = copy_buffers(device, mesh_buffer_count, staging_mesh_buffers, mesh_buffers, buffer_regions);
	destroy_mesh(&staging_mesh, device);
	if (result) {
		printf("Failed to copy mesh data of the scene file at path %s from staging buffers to the device. It has %llu triangles.\n",
			file_path, scene->mesh.triangle_count);
		destroy_scene_temporary(&temp, device);
		destroy_scene(scene, device);
		return 1;
	}

	// Now load all textures
	uint32_t texture_count = (uint32_t) (scene->materials.material_count * material_texture_count);
	char** texture_file_paths = malloc(sizeof(char*) * texture_count);
	memset(texture_file_paths, 0, sizeof(char*) * texture_count);
	for (uint32_t i = 0; i != scene->materials.material_count; ++i) {
		for (uint32_t j = 0; j != material_texture_count; ++j) {
			const char* path_pieces[] = {
				texture_path, "/", scene->materials.material_names[i], "_",
				get_material_texture_suffix((material_texture_type_t) j), ".vkt"
			};
			texture_file_paths[i * material_texture_count + j] = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
		}
	}
	result = load_2d_textures(&scene->materials.textures, device, texture_count, (const char* const*) texture_file_paths, VK_IMAGE_USAGE_SAMPLED_BIT);
	for (uint32_t i = 0; i != texture_count; ++i)
		free(texture_file_paths[i]);
	if (result) {
		printf("Failed to load material textures for the scene file at path %s using texture path %s.\n", file_path, texture_path);
		destroy_scene(scene, device);
		return 1;
	}

	// Create a sampler for material textures
	VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.anisotropyEnable = VK_TRUE, .maxAnisotropy = 16,
		.minLod = 0.0f, .maxLod = 3.4e38f
	};
	if (vkCreateSampler(device->device, &sampler_info, NULL, &scene->materials.sampler)) {
		printf("Failed to create a sampler for materials of the scene %s.\n", file_path);
		destroy_scene_temporary(&temp, device);
		destroy_scene(scene, device);
		return 1;
	}
	
	// Read meta data on the quantization of animations
	fread(scene->animation.dequantization_constants, sizeof(scene->animation.dequantization_constants), 1, temp.file);
	// Load animations into a staging buffer
	VkBufferCreateInfo animation_staging_request = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = scene->animation.time_sample_count * scene->animation.bone_count * 8 * sizeof(uint16_t),
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
	};
	buffers_t animation_staging;
	float* animation_data;
	if (create_buffers(&animation_staging, device, &animation_staging_request, 1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	 || vkMapMemory(device->device, animation_staging.memory, 0, VK_WHOLE_SIZE, 0, (void**) &animation_data)) {
		printf("Failed to create or map a staging buffer for the animation texture of scene %s.\n", file_path);
		destroy_scene_temporary(&temp, device);
		destroy_buffers(&animation_staging, device);
		destroy_scene(scene, device);
		return 1;
	};
	fread(animation_data, 1, animation_staging_request.size, temp.file);
	vkUnmapMemory(device->device, animation_staging.memory);
	// Now create an animation texture and fill it
	image_request_t animation_request = {
		.image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = VK_FORMAT_R16G16B16A16_UINT,
			.extent = { (uint32_t) scene->animation.bone_count * 2, (uint32_t) scene->animation.time_sample_count, 1 },
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		},
		.view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT }
		},
	};
	if (create_images(&scene->animation.texture, device, &animation_request, 1, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
		printf("Failed to create a texture to hold animation data for the scene file at path %s. Its size is supposed to be animation %dx%d.\n",
			file_path, animation_request.image_info.extent.width, animation_request.image_info.extent.height);
		destroy_scene_temporary(&temp, device);
		destroy_buffers(&animation_staging, device);
		destroy_scene(scene, device);
		return 1;
	}
	VkBufferImageCopy region = {
		.imageExtent = animation_request.image_info.extent,
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = 1
		}
	};
	if (copy_buffers_to_images(device,
		1, &animation_staging.buffers[0].buffer, &scene->animation.texture.images[0].image,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &region))
	{
		printf("Failed to copy an animation texture from the staging buffer to a GPU image for the scene at path %s.\n", file_path);
		destroy_scene_temporary(&temp, device);
		destroy_buffers(&animation_staging, device);
		destroy_scene(scene, device);
		return 1;
	}
	destroy_buffers(&animation_staging, device);
	// Create a sampler for the animation textures
	VkSamplerCreateInfo animation_sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.anisotropyEnable = VK_FALSE, .maxAnisotropy = 1,
		.minLod = 0.0f, .maxLod = 0.0f
	};
	if (vkCreateSampler(device->device, &animation_sampler_info, NULL, &scene->animation.sampler)) {
		printf("Failed to create a sampler for the animation texture of the scene %s.\n", file_path);
		destroy_scene_temporary(&temp, device);
		destroy_scene(scene, device);
		return 1;
	}
	
	// If everything went well, we have reached an end-of-file marker
	uint32_t eof_marker = 0;
	fread(&eof_marker, sizeof(eof_marker), 1, temp.file);
	destroy_scene_temporary(&temp, device);
	if (eof_marker != 0xE0FE0F) {
		printf("The scene file at path %s seems to be invalid. The animation data is not followed by the expected end of file marker.\n", file_path);
		destroy_scene(scene, device);
		return 1;
	}
	return 0;
}


void destroy_scene(scene_t* scene, const device_t* device) {
	destroy_mesh(&scene->mesh, device);
	destroy_materials(&scene->materials, device);
	destroy_animation(&scene->animation, device);
}


void get_materials_descriptor_layout(VkDescriptorSetLayoutBinding* layout_binding, uint32_t binding_index, const materials_t* materials) {
	uint32_t texture_count = (uint32_t) materials->material_count * material_texture_count;
	VkDescriptorSetLayoutBinding binding = {
		.binding = binding_index,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_FRAGMENT_BIT,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = texture_count
	};
	(*layout_binding) = binding;
}

VkDescriptorImageInfo* get_materials_descriptor_infos(uint32_t* texture_count, const materials_t* materials) {
	(*texture_count) = (uint32_t) materials->material_count * material_texture_count;
	VkDescriptorImageInfo* texture_infos = malloc(sizeof(VkDescriptorImageInfo) * *texture_count);
	for (uint32_t i = 0; i != *texture_count; ++i) {
		texture_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		texture_infos[i].imageView = materials->textures.images[i].view;
		texture_infos[i].sampler = materials->sampler;
	}
	return texture_infos;
}
