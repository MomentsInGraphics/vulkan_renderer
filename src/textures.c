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


#include "textures.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

//! Stores meta-data about a single mipmap of a texture. Matches binary data in
//! the file format.
typedef struct texture_2d_mipmap_header_s {
	//! The resolution of this mipmap in pixels
	VkExtent2D resolution;
	//! The size in bytes of this mipmap without padding
	VkDeviceSize size;
	//! The offset in bytes from the beginning of the texture data stored in
	//! the file. In general, the offset in device local memory will be
	//! different.
	VkDeviceSize offset;
} texture_2d_mipmap_header_t;


//! Meta-information about a single 2D texture
typedef struct texture_2d_header_s {
	//! The number of mipmaps stored for this texture
	uint32_t mipmap_count;
	//! The resolution of the highest miplevel of the texture in pixels
	VkExtent2D resolution;
	//! The format of this texture
	VkFormat format;
	//! The size in bytes of all mipmaps taken together without padding. This
	//! quantity pertains to storage in file, not in device memory.
	VkDeviceSize size;
	//! Meta-data about each mipmap of this texture
	texture_2d_mipmap_header_t* mipmaps;
	//! While the texture is being loaded, this this is a file handle for the
	//! texture being loaded. Otherwise it is NULL.
	FILE* file;
} texture_2d_header_t;


//! Struct holding intermediate data for texture loading
typedef struct texture_2d_loading_s {
	//! GPU objects for textures
	images_t textures;
	//! Staging memory for textures
	buffers_t staging;
	//! Number of array entries for the subsequent members
	uint32_t texture_count;
	texture_2d_header_t* headers;
	VkBufferCreateInfo* buffer_requests;
	image_request_t* image_requests;
	//! Number of array entries for the subsequent members
	uint32_t total_mipmap_count;
	VkBufferImageCopy* buffer_to_image_regions;
	VkBuffer* source_texture_buffers;
	VkImage* destination_images;
} texture_2d_loading_t;


//! Frees intermediate objects for texture loading
void destroy_texture_loading(texture_2d_loading_t* loading, const device_t* device) {
	destroy_images(&loading->textures, device);
	destroy_buffers(&loading->staging, device);
	if (loading->headers) {
		for (uint32_t i = 0; i != loading->texture_count; ++i) {
			if (loading->headers[i].file)
				fclose(loading->headers[i].file);
			free(loading->headers[i].mipmaps);
		}
		free(loading->headers);
	}
	free(loading->buffer_requests);
	free(loading->image_requests);
	free(loading->buffer_to_image_regions);
	free(loading->source_texture_buffers);
	free(loading->destination_images);
	memset((void*) loading, 0, sizeof(*loading));
}


int load_2d_textures(images_t* textures, const device_t* device, uint32_t texture_count, const char* const* file_paths, VkBufferUsageFlags usage) {
	memset(textures, 0, sizeof(*textures));
	texture_2d_loading_t loading = { .texture_count = texture_count };
	// Open all the texture files and read their headers
	loading.headers = malloc(sizeof(texture_2d_header_t) * texture_count);
	memset(loading.headers, 0, sizeof(texture_2d_header_t) * texture_count);
	for (uint32_t i = 0; i != texture_count; ++i) {
		texture_2d_header_t* header = &loading.headers[i];
		// Open the file
		FILE* file = loading.headers[i].file = fopen(file_paths[i], "rb");
		if (!file) {
			printf("Failed to open the texture file at path %s.\n", file_paths[i]);
			destroy_texture_loading(&loading, device);
			return 1;
		}
		// Check the file format marker
		uint32_t marker, version;
		fread(&marker, sizeof(marker), 1, file);
		fread(&version, sizeof(version), 1, file);
		if (marker != 0xbc1bc1 || version != 1) {
			printf("The texture at path %s does not seem to have the correct format. It is supposed to be converted to a custom format for the renderer using the texture conversion utility. Aborting.\n", file_paths[i]);
			destroy_texture_loading(&loading, device);
			return 1;
		}
		// Load meta-data about the texture
		fread(&header->mipmap_count, sizeof(uint32_t), 1, file);
		loading.total_mipmap_count += header->mipmap_count;
		fread(&header->resolution, sizeof(uint32_t), 2, file);
		fread(&header->format, sizeof(uint32_t), 1, file);
		fread(&header->size, sizeof(uint64_t), 1, file);
		// Load meta-data about mipmaps
		header->mipmaps = malloc(sizeof(texture_2d_mipmap_header_t) * header->mipmap_count);
		memset(header->mipmaps, 0, sizeof(texture_2d_mipmap_header_t) * header->mipmap_count);
		for (uint32_t k = 0; k != header->mipmap_count; ++k) {
			fread(&header->mipmaps[k].resolution, sizeof(uint32_t), 2, file);
			fread(&header->mipmaps[k].size, sizeof(uint64_t), 1, file);
			fread(&header->mipmaps[k].offset, sizeof(uint64_t), 1, file);
		}
	}

	// Allocate staging buffers for textures
	loading.buffer_requests = malloc(sizeof(VkBufferCreateInfo) * texture_count);
	for (uint32_t i = 0; i != texture_count; ++i) {
		VkBufferCreateInfo request = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = loading.headers[i].size,
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		};
		loading.buffer_requests[i] = request;
	}
	if (create_buffers(&loading.staging, device, loading.buffer_requests, texture_count, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		printf("Failed to create %d staging buffers for textures.\n", texture_count);
		destroy_texture_loading(&loading, device);
		return 1;
	};
	// Read the data of each texture into the staging buffers
	for (uint32_t i = 0; i != texture_count; ++i) {
		texture_2d_header_t* header = &loading.headers[i];
		VkDeviceMemory memory = loading.staging.memory;
		char* texture_data;
		if (vkMapMemory(device->device, memory, loading.staging.buffers[i].offset, header->size, 0, (void**) &texture_data)) {
			printf("Failed to map memory of the staging buffer for the texture at path %s.\n", file_paths[i]);
			destroy_texture_loading(&loading, device);
			return 1;
		}
		fread(texture_data, 1, header->size, header->file);
		vkUnmapMemory(device->device, memory);
		// We should have arrived at the end of the file
		uint32_t texture_eof_marker = 0;
		fread(&texture_eof_marker, 1, sizeof(texture_eof_marker), header->file);
		if (texture_eof_marker != 0xE0FE0F) {
			printf("The texture file at path %s seems to be invalid. The texture data is not followed by the expected end of file marker.\n", file_paths[i]);
			destroy_texture_loading(&loading, device);
			return 1;
		}
		fclose(header->file);
		header->file = NULL;
	}

	// Create the GPU-resident texture objects
	loading.image_requests = malloc(sizeof(image_request_t) * texture_count);
	for (uint32_t i = 0; i != texture_count; ++i) {
		texture_2d_header_t* header = &loading.headers[i];
		image_request_t request = {
			.image_info = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				.imageType = VK_IMAGE_TYPE_2D,
				.format = header->format,
				.extent = { header->resolution.width, header->resolution.height, 1 },
				.mipLevels = header->mipmap_count,
				.arrayLayers = 1,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | usage
			},
			.view_info = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT }
			}
		};
		loading.image_requests[i] = request;
	}
	if (create_images(&loading.textures, device, loading.image_requests, texture_count, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) {
		printf("Failed to create texture objects on GPU for %d textures.\n", texture_count);
		destroy_texture_loading(&loading, device);
		return 1;
	}

	// Copy textures from the staging buffers to the GPU-resident images
	loading.buffer_to_image_regions = malloc(sizeof(VkBufferImageCopy) * loading.total_mipmap_count);
	loading.source_texture_buffers = malloc(sizeof(VkBuffer) * loading.total_mipmap_count);
	loading.destination_images = malloc(sizeof(VkImage) * loading.total_mipmap_count);
	uint32_t region_index = 0;
	for (uint32_t i = 0; i != texture_count; ++i) {
		texture_2d_header_t* header = &loading.headers[i];
		for (uint32_t j = 0; j != header->mipmap_count; ++j) {
			VkBufferImageCopy region = {
				.imageExtent = { header->mipmaps[j].resolution.width, header->mipmaps[j].resolution.height, 1 },
				.bufferOffset = header->mipmaps[j].offset,
				.imageSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = j,
					.layerCount = 1
				}
			};
			loading.buffer_to_image_regions[region_index] = region;
			loading.source_texture_buffers[region_index] = loading.staging.buffers[i].buffer;
			loading.destination_images[region_index] = loading.textures.images[i].image;
			++region_index;
		}
	}
	if (copy_buffers_to_images(device,
		loading.total_mipmap_count, loading.source_texture_buffers, loading.destination_images,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, loading.buffer_to_image_regions))
	{
		printf("Failed to copy %d textures from staging buffers to GPU images.\n", texture_count);
		destroy_texture_loading(&loading, device);
		return 1;
	}

	// Hand over the result and clean up
	(*textures) = loading.textures;
	memset(&loading.textures, 0, sizeof(loading.textures));
	destroy_texture_loading(&loading, device);
	return 0;
}
