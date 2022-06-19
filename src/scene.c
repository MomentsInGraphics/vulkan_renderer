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


#include "scene.h"
#include "textures.h"
#include "string_utilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* get_material_texture_suffix(material_texture_type_t type) {
	switch (type) {
	case material_texture_type_base_color: return "BaseColor";
	case material_texture_type_specular: return "Specular";
	case material_texture_type_normal: return "Normal";
	default: return NULL;
	}
}


/*! Given a mesh with count variables set as appropriate, this function creates
	the required buffers and allocates and binds memory for them. It does not
	fill them with data.
	\param staging Pass 1 to indicate that the created buffers are used as
		staging buffer or 0 to indicate that they are device local.
	\return 0 on success.*/
int create_mesh(mesh_t* mesh, const device_t* device, VkBool32 staging) {
	VkMemoryPropertyFlags memory_properties = staging
		? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		: VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
	VkMemoryPropertyFlags positions_usage = staging
		? VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		: (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	VkMemoryPropertyFlags other_usage = staging
		? VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		: (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	VkMemoryPropertyFlags triangle_usage = staging
		? VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		: (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	// Create the buffers
	VkBufferCreateInfo buffer_infos[mesh_buffer_count_full];
	memset(buffer_infos, 0, sizeof(buffer_infos));
	mesh->positions.size = sizeof(uint32_t) * 2 * 3 * mesh->triangle_count;
	mesh->normals_and_tex_coords.size = sizeof(uint16_t) * 4 * 3 * mesh->triangle_count;
	mesh->material_indices.size = sizeof(uint8_t) * mesh->triangle_count;
	mesh->triangle.size = sizeof(int8_t) * 3 * 2;
	for (uint32_t i = 0; i != mesh_buffer_count_full; ++i) {
		buffer_infos[i].sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_infos[i].size = mesh->buffers[i].size;
		buffer_infos[i].usage = other_usage;
	}
	buffer_infos[mesh_buffer_type_positions].usage = positions_usage;
	buffer_infos[mesh_buffer_type_triangle].usage = triangle_usage;
	buffers_t buffers;
	if (create_buffers(&buffers, device, buffer_infos, mesh_buffer_count_full, memory_properties)) {
		printf("Failed to allocate %smemory for a mesh with %llu triangles.\n",
			staging ? "staging " : "", mesh->triangle_count);
		return 1;
	}
	memcpy(mesh->buffers, buffers.buffers, sizeof(mesh->buffers));
	free(buffers.buffers);
	mesh->memory = buffers.memory;
	mesh->size = buffers.size;
	// Create the views
	if (!staging) {
		VkFormat formats[mesh_buffer_count_full];
		formats[mesh_buffer_type_positions] = VK_FORMAT_R32G32_UINT;
		formats[mesh_buffer_type_normals_and_tex_coords] = VK_FORMAT_R16G16B16A16_UNORM;
		formats[mesh_buffer_type_material_indices] = VK_FORMAT_R8_UINT;
		formats[mesh_buffer_type_triangle] = VK_FORMAT_R8G8_SINT;
		for (uint32_t i = 0; i != mesh_buffer_count_full; ++i) {
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
	return 0;
}


//! Frees and nulls the given mesh
void destroy_mesh(mesh_t* mesh, const device_t* device) {
	for (uint32_t i = 0; i != mesh_buffer_count_full; ++i) {
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


//! Frees and nulls the given acceleration structure
void destroy_acceleration_structure(acceleration_structure_t* structure, const device_t* device) {
	VK_LOAD(vkDestroyAccelerationStructureKHR)
	if (structure->top_level) pvkDestroyAccelerationStructureKHR(device->device, structure->top_level, NULL);
	if (structure->bottom_level) pvkDestroyAccelerationStructureKHR(device->device, structure->bottom_level, NULL);
	destroy_buffers(&structure->buffers, device);
	memset(structure, 0, sizeof(*structure));
}


/*! Constructs top- and bottom-level acceleration structures for the given mesh
	\param structure The output structure. Cleaned up by destroy_scene().
	\param device A device that has to support ray tracing. Otherwise this
		method fails.
	\param mesh The staging version of the mesh.
	\param mesh_data Pointer to the already mapped memory of the staging mesh.
	\return 0 on success.*/
int create_acceleration_structure(acceleration_structure_t* structure, const device_t* device, const mesh_t* mesh, const char* mesh_data) {
	memset(structure, 0, sizeof(*structure));
	if (!device->ray_tracing_supported) {
		printf("Cannot create an acceleration structure without ray tracing support.\n");
		return 1;
	}
	VK_LOAD(vkGetAccelerationStructureBuildSizesKHR)
	VK_LOAD(vkCreateAccelerationStructureKHR)
	VK_LOAD(vkGetAccelerationStructureDeviceAddressKHR)
	VK_LOAD(vkCmdBuildAccelerationStructuresKHR)
	// Create a buffer for the dequantized triangle mesh
	VkBufferCreateInfo staging_infos[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = mesh->triangle_count * sizeof(float) * 3 * 3,
			.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizeof(VkAccelerationStructureInstanceKHR),
			.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		}
	};
	buffers_t staging;
	uint8_t* staging_data;
	if (create_aligned_buffers(&staging, device, staging_infos, 2, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 16)
		|| vkMapMemory(device->device, staging.memory, 0, staging.size, 0, (void**) &staging_data))
	{
		printf("Failed to allocate and map a buffer for dequantized mesh data (%llu triangles) to create an acceleration structure.\n", mesh->triangle_count);
		destroy_buffers(&staging, device);
		destroy_acceleration_structure(structure, device);
		return 1;
	}
	// Dequantize the mesh data
	const uint32_t* quantized_positions = (const uint32_t*) (mesh_data + mesh->positions.offset);
	float* vertices = (float*) (staging_data + staging.buffers[0].offset);
	for (uint32_t i = 0; i != mesh->triangle_count * 3; ++i) {
		uint32_t quantized_position[2] = {quantized_positions[2 * i + 0], quantized_positions[2 * i + 1]};
		float position[3] = {
			(float) (quantized_position[0] & 0x1FFFFF),
			(float) (((quantized_position[0] & 0xFFE00000) >> 21) | ((quantized_position[1] & 0x3FF) << 11)),
			(float) ((quantized_position[1] & 0x7FFFFC00) >> 10)
		};
		for (uint32_t j = 0; j != 3; ++j)
			vertices[3 * i + j] = position[j] * mesh->dequantization_factor[j] + mesh->dequantization_summand[j];
	}
	// Figure out how big the buffers for the bottom-level need to be
	uint32_t primitive_count = (uint32_t) mesh->triangle_count;
	VkAccelerationStructureBuildSizesInfoKHR bottom_sizes = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};
	VkBufferDeviceAddressInfo vertices_address = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = staging.buffers[0].buffer
	};
	VkAccelerationStructureGeometryKHR bottom_geometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.geometry = {
			.triangles = {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
				.vertexData = { .deviceAddress = vkGetBufferDeviceAddress(device->device, &vertices_address) },
				.maxVertex = primitive_count * 3 - 1,
				.vertexStride = 3 * sizeof(float),
				.indexType = VK_INDEX_TYPE_NONE_KHR,
				.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
			},
		},
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
	};
	VkAccelerationStructureBuildGeometryInfoKHR bottom_build_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = 1, .pGeometries = &bottom_geometry,
	};
	pvkGetAccelerationStructureBuildSizesKHR(
		device->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&bottom_build_info, &primitive_count, &bottom_sizes);

	// Figure out how big the buffers for the top-level need to be
	VkAccelerationStructureBuildSizesInfoKHR top_sizes = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};
	VkBufferDeviceAddressInfo instances_address = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = staging.buffers[1].buffer
	};
	VkAccelerationStructureGeometryKHR top_geometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.geometry = {
			.instances = {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
				.arrayOfPointers = VK_FALSE,
				.data = { .deviceAddress = vkGetBufferDeviceAddress(device->device, &instances_address) },
			},
		},
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
	};
	VkAccelerationStructureBuildGeometryInfoKHR top_build_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = 1, .pGeometries = &top_geometry,
	};
	pvkGetAccelerationStructureBuildSizesKHR(
		device->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&top_build_info, &top_build_info.geometryCount, &top_sizes);
	
	// Create buffers for the acceleration structures
	VkAccelerationStructureBuildSizesInfoKHR sizes[2] = { bottom_sizes, top_sizes };
	VkBufferCreateInfo buffer_requests[] = {
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizes[0].accelerationStructureSize,
			.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizes[1].accelerationStructureSize,
			.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		},
	};
	if (create_buffers(&structure->buffers, device, buffer_requests, 2, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
		printf("Failed to create buffers to hold acceleration structures.\n");
		destroy_buffers(&staging, device);
		destroy_acceleration_structure(structure, device);
		return 1;
	}

	// Create the acceleration structures
	for (uint32_t i = 0; i != 2; ++i) {
		VkAccelerationStructureCreateInfoKHR create_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.buffer = structure->buffers.buffers[i].buffer,
			.offset = 0, .size = sizes[i].accelerationStructureSize,
			.type = (i == 0) ? VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		};
		if (pvkCreateAccelerationStructureKHR(device->device, &create_info, NULL, &structure->levels[i])) {
			printf("Failed to create a %s-level acceleration structure.\n", (i == 0) ? "bottom" : "top");
			destroy_buffers(&staging, device);
			destroy_acceleration_structure(structure, device);
			return 1;
		}
	}

	// Allocate scratch memory for the build
	VkBufferCreateInfo scratch_infos[] = {
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizes[0].buildScratchSize,
			.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizes[1].buildScratchSize,
			.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		},
	};
	buffers_t scratch;
	if (create_aligned_buffers(&scratch, device, scratch_infos, 2, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT, device->acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment)) {
		printf("Failed to allocate scratch memory for building acceleration structures.\n");
		destroy_buffers(&staging, device);
		destroy_acceleration_structure(structure, device);
		return 1;
	}
	
	// Specify the only instance
	VkAccelerationStructureDeviceAddressInfoKHR address_request = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = structure->bottom_level,
	};
	VkAccelerationStructureInstanceKHR instance = {
		.transform = { .matrix = {
				{1.0f, 0.0f, 0.0f, 0.0f},
				{0.0f, 1.0f, 0.0f, 0.0f},
				{0.0f, 0.0f, 1.0f, 0.0f},
			}
		},
		.mask = 0xFF,
		.flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
		.accelerationStructureReference = pvkGetAccelerationStructureDeviceAddressKHR(device->device, &address_request),
	};
	memcpy(staging_data + staging.buffers[1].offset, &instance, sizeof(instance));

	// Get ready to record commands
	VkCommandBufferAllocateInfo cmd_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = device->command_pool,
		.commandBufferCount =  1
	};
	VkCommandBuffer cmd;
	if (vkAllocateCommandBuffers(device->device, &cmd_info, &cmd)) {
		printf("Failed to allocate a command buffer for building an acceleration structure.\n");
		destroy_buffers(&scratch, device);
		destroy_buffers(&staging, device);
		destroy_acceleration_structure(structure, device);
		return 1;
	}
	VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	if (vkBeginCommandBuffer(cmd, &begin_info)) {
		printf("Failed to begin command buffer recording for building acceleration structures.\n");
		vkFreeCommandBuffers(device->device, device->command_pool, 1, &cmd);
		destroy_buffers(&scratch, device);
		destroy_buffers(&staging, device);
		destroy_acceleration_structure(structure, device);
		return 1;
	}
	// Build bottom- and top-level acceleration structures in this order
	VkAccelerationStructureBuildRangeInfoKHR build_ranges[] = {
		{ .primitiveCount = (uint32_t) mesh->triangle_count },
		{ .primitiveCount = 1 },
	};
	for (uint32_t i = 0; i != 2; ++i) {
		const char* level_name = (i == 0) ? "bottom" : "top";
		VkAccelerationStructureBuildGeometryInfoKHR build_info = (i == 0) ? bottom_build_info : top_build_info;
		VkBufferDeviceAddressInfo scratch_adress_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = scratch.buffers[i].buffer
		};
		build_info.scratchData.deviceAddress = vkGetBufferDeviceAddress(device->device, &scratch_adress_info);
		build_info.dstAccelerationStructure = structure->levels[i];
		const VkAccelerationStructureBuildRangeInfoKHR* build_range = &build_ranges[i];
		pvkCmdBuildAccelerationStructuresKHR(cmd, 1, &build_info, &build_range);
		// Enforce synchronization
		VkMemoryBarrier after_build_barrier = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
			.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
		};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
				VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0,
				1, &after_build_barrier, 0, NULL, 0, NULL);
	}
	// Submit the command buffer
	VkSubmitInfo cmd_submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1, .pCommandBuffers = &cmd
	};
	if (vkEndCommandBuffer(cmd) || vkQueueSubmit(device->queue, 1, &cmd_submit, NULL)) {
		printf("Failed to end and submit the command buffer for building acceleration structures.\n");
		vkFreeCommandBuffers(device->device, device->command_pool, 1, &cmd);
		destroy_buffers(&scratch, device);
		destroy_buffers(&staging, device);
		destroy_acceleration_structure(structure, device);
		return 1;
	}
	// Clean up once the build is finished
	VkResult result;
	if (result = vkQueueWaitIdle(device->queue)) {
		printf("Failed to wait for the construction of the acceleration structure to finish. Error code %d.\n", result);
		vkFreeCommandBuffers(device->device, device->command_pool, 1, &cmd);
		destroy_buffers(&scratch, device);
		destroy_buffers(&staging, device);
		destroy_acceleration_structure(structure, device);
		return 1;
	}
	vkFreeCommandBuffers(device->device, device->command_pool, 1, &cmd);
	destroy_buffers(&scratch, device);
	destroy_buffers(&staging, device);
	return 0;
}


int load_scene(scene_t* scene, const device_t* device, const char* file_path, const char* texture_path, VkBool32 request_acceleration_structure) {
	// Clear the output object
	memset(scene, 0, sizeof(*scene));
	// Open the source file
	FILE* file = fopen(file_path, "rb");
	if (!file) {
		printf("Failed to open the scene file at %s.\n", file_path);
		destroy_scene(scene, device);
		return 1;
	}
	// Read the header
	uint32_t file_marker, version;
	fread(&file_marker, sizeof(file_marker), 1, file);
	fread(&version, sizeof(version), 1, file);
	if (file_marker != 0xabcabc || version != 1) {
		printf("The scene file at path %s is invalid or unsupported. The format marker is 0x%x, the version is %d.\n", file_path, file_marker, version);
		fclose(file);
		destroy_scene(scene, device);
		return 1;
	}
	fread(&scene->materials.material_count, sizeof(uint64_t), 1, file);
	fread(&scene->mesh.triangle_count, sizeof(uint64_t), 1, file);
	fread(scene->mesh.dequantization_factor, sizeof(float), 3, file);
	fread(scene->mesh.dequantization_summand, sizeof(float), 3, file);
	printf("Triangle count: %llu\n", scene->mesh.triangle_count);
	// If there are no triangles, abort
	if (scene->mesh.triangle_count == 0) {
		printf("The scene file at path %s is completely empty, i.e. it holds 0 triangles.\n", file_path);
		fclose(file);
		destroy_scene(scene, device);
		return 1;
	}
	// Read material names
	scene->materials.material_names = malloc(sizeof(char*) * scene->materials.material_count);
	memset(scene->materials.material_names, 0, sizeof(char*) * scene->materials.material_count);
	for (uint64_t i = 0; i != scene->materials.material_count; ++i) {
		uint64_t name_length;
		fread(&name_length, sizeof(name_length), 1, file);
		scene->materials.material_names[i] = malloc(sizeof(char) * (name_length + 1));
		fread(scene->materials.material_names[i], sizeof(char), name_length + 1, file);
	}

	// Allocate staging buffers for the mesh
	mesh_t empty_mesh = scene->mesh;
	if (create_mesh(&scene->mesh, device, VK_TRUE)) {
		printf("Failed to create staging buffers and allocate memory for meshes of the scene file at path %s. It has %llu triangles.\n",
			file_path, scene->mesh.triangle_count);
		fclose(file);
		destroy_scene(scene, device);
		return 1;
	}
	// Map memory for the staging buffers
	char* staging_data;
	if (vkMapMemory(device->device, scene->mesh.memory, 0, scene->mesh.size, 0, (void**) &staging_data)) {
		printf("Failed to map memory of the staging buffer for meshes of the scene file at path %s.\n", file_path);
		fclose(file);
		destroy_scene(scene, device);
		return 1;
	}
	// Read the binary mesh data. The file has it exactly in the format in
	// which it goes onto the GPU.
	for (uint32_t i = 0; i != mesh_buffer_count; ++i)
		fread(staging_data + scene->mesh.buffers[i].offset, scene->mesh.buffers[i].size, 1, file);
	// Write the screen-filling triangle
	int8_t triangle_vertices[3][2] = { {-1, -1}, {3, -1}, {-1, 3} };
	memcpy(staging_data + scene->mesh.triangle.offset, triangle_vertices, sizeof(triangle_vertices));
	// If everything went well, we have reached an end-of-file marker
	uint32_t eof_marker = 0;
	fread(&eof_marker, sizeof(eof_marker), 1, file);
	fclose(file);
	if (eof_marker != 0xE0FE0F) {
		printf("The scene file at path %s seems to be invalid. The geometry data is not followed by the expected end of file marker.\n", file_path);
		destroy_scene(scene, device);
		return 1;
	}
	// Create an acceleration structure now that the mesh data is available
	if (request_acceleration_structure && device->ray_tracing_supported) {
		if (create_acceleration_structure(&scene->acceleration_structure, device, &scene->mesh, staging_data)) {
			printf("Failed to construct an acceleration structure for the scene file at path %s.\n", file_path);
			destroy_scene(scene, device);
			return 1;
		}
	}
	// Unmap staging memory
	vkUnmapMemory(device->device, scene->mesh.memory);
	// Allocate device local mesh buffers
	mesh_t staging_mesh = scene->mesh;
	scene->mesh = empty_mesh;
	if (create_mesh(&scene->mesh, device, VK_FALSE)) {
		printf("Failed to create device buffers and allocate memory for meshes of the scene file at path %s. It has %llu triangles.\n",
			file_path, scene->mesh.triangle_count);
		destroy_mesh(&staging_mesh, device);
		destroy_scene(scene, device);
		return 1;
	}
	// Perform the mesh copy
	VkBuffer staging_mesh_buffers[COUNT_OF(staging_mesh.buffers)];
	VkBuffer mesh_buffers[COUNT_OF(staging_mesh.buffers)];
	VkBufferCopy buffer_regions[mesh_buffer_count_full];
	memset(buffer_regions, 0, sizeof(buffer_regions));
	for (uint32_t i = 0; i != mesh_buffer_count_full; ++i) {
		staging_mesh_buffers[i] = staging_mesh.buffers[i].buffer;
		mesh_buffers[i] = scene->mesh.buffers[i].buffer;
		buffer_regions[i].size = staging_mesh.buffers[i].size;
	}
	int result = copy_buffers(device, mesh_buffer_count_full, staging_mesh_buffers, mesh_buffers, buffer_regions);
	destroy_mesh(&staging_mesh, device);
	if (result) {
		printf("Failed to copy mesh and texture data of the scene file at path %s from staging buffers to the device. It has %llu triangles.\n",
			file_path, scene->mesh.triangle_count);
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
		destroy_scene(scene, device);
		return 1;
	}
	return 0;
}


void destroy_scene(scene_t* scene, const device_t* device) {
	destroy_mesh(&scene->mesh, device);
	destroy_materials(&scene->materials, device);
	destroy_acceleration_structure(&scene->acceleration_structure, device);
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
