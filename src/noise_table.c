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


#include "noise_table.h"
#include "string_utilities.h"
#include "math_utilities.h"
#include <stdio.h>


VkExtent3D get_default_noise_resolution(noise_type_t noise_type) {
	VkExtent3D result;
	switch (noise_type) {
	case noise_type_blue:
		result.width = result.height = result.depth = 64;
		break;
	case noise_type_blue_noise_dithered:
		result.width = result.height = 128;
		result.depth = 1;
		break;
	case noise_type_ahmed:
		result.width = result.height = 256;
		result.depth = 64;
		break;
	default:
		result.width = result.height = 256;
		result.depth = 64;
		break;
	}
	return result;
}


int load_noise_table(noise_table_t* noise, const device_t* device, VkExtent3D resolution, noise_type_t noise_type) {
	memset(noise, 0, sizeof(*noise));
	noise->random_seed = 3124705;
	if (resolution.width > 9999 || resolution.height > 9999 || resolution.depth > 9999) {
		printf("Invalid noise resolution or slice count.\n");
		return 1;
	}
	// Create a staging buffer
	uint32_t cell_count = resolution.width * resolution.height * resolution.depth * 4;
	VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof(uint16_t) * cell_count,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
	};
	buffers_t staging;
	if (create_buffers(&staging, device, &buffer_info, 1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		printf("Failed to create a %llu byte staging buffer for noise.\n", buffer_info.size);
		return 1;
	}
	// Map it
	uint16_t* data;
	if (vkMapMemory(device->device, staging.memory, 0, staging.size, 0, (void**) &data)) {
		printf("Failed to map %llu bytes of staging memory for noise.\n", buffer_info.size);
		destroy_buffers(&staging, device);
		return 1;
	}
	// Create or load the requested type of noise
	if (noise_type == noise_type_white)
		for(uint32_t i = 0; i != cell_count; ++i)
			data[i] = (uint16_t) (wang_random_number(i + 243708) & 0xFFFF);
	else {
		char* file_path;
		if (noise_type == noise_type_blue)
			file_path = copy_string("data/noise/blue_noise_rgba_%02dx%02d_%02d.blob");
		else if (noise_type == noise_type_sobol)
			file_path = copy_string("data/noise/sobol_2d_rgba_%02dx%02d_%02d.blob");
		else if (noise_type == noise_type_owen)
			file_path = copy_string("data/noise/owen_2d_rgba_%02dx%02d_%02d.blob");
		else if (noise_type == noise_type_burley_owen)
			file_path = copy_string("data/noise/burley_owen_2d_rgba_%02dx%02d_%02d.blob");
		else if (noise_type == noise_type_ahmed)
			file_path = copy_string("data/noise/ahmed_2d_rgba_%02dx%02d_%02d.blob");
		else if (noise_type == noise_type_blue_noise_dithered)
			file_path = copy_string("data/noise/dithered_2d_rgba_%02dx%02d_%02d.blob");
		else {
			printf("Failed to load a noise table. The given type is unknown.\n");
			destroy_buffers(&staging, device);
			destroy_noise_table(noise, device);
			return 1;
		}
		sprintf(file_path, file_path, resolution.width, resolution.height, resolution.depth);
		FILE* noise_file = fopen(file_path, "rb");
		if (!noise_file) {
			printf("Failed to open the noise file at path %s. Please check path and permissions?\n", file_path);
			destroy_buffers(&staging, device);
			free(file_path);
			return 1;
		}
		free(file_path);
		fread(data, sizeof(uint16_t), cell_count, noise_file);
		fclose(noise_file);
	}
	// Create the texture array
	image_request_t request = {
		.image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = VK_FORMAT_R16G16B16A16_UNORM,
			.extent = {resolution.width, resolution.height, 1},
			.mipLevels = 1,
			.arrayLayers = resolution.depth,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
		},
		.view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT}
		}
	};
	if (create_images(&noise->noise_array, device, &request, 1, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) {
		printf("Failed to create a noise texture of resolution %ux%u with %u layers.\n",
			resolution.width, resolution.height, resolution.depth);
		destroy_buffers(&staging, device);
		destroy_noise_table(noise, device);
		return 1;
	}
	// Copy
	VkBufferImageCopy copy = {
		.imageExtent = {resolution.width, resolution.height, 1},
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = resolution.depth
		}
	};
	if (copy_buffers_to_images(device, 1, &staging.buffers[0].buffer, &noise->noise_array.images[0].image,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &copy))
	{
		printf("Failed to copy noise from the staging buffer to the texture array.\n");
		destroy_buffers(&staging, device);
		destroy_noise_table(noise, device);
		return 1;
	}
	// Clean up
	destroy_buffers(&staging, device);
	return 0;
}


void destroy_noise_table(noise_table_t* noise, const device_t* device) {
	destroy_images(&noise->noise_array, device);
}


void set_noise_constants(uint32_t resolution_mask[2], uint32_t* texture_index_mask, uint32_t random_numbers[4], noise_table_t* noise, VkBool32 animate_noise) {
	resolution_mask[0] = noise->noise_array.images[0].image_info.extent.width - 1;
	resolution_mask[1] = noise->noise_array.images[0].image_info.extent.height - 1;
	(*texture_index_mask) = noise->noise_array.images[0].image_info.arrayLayers - 1;
	for (uint32_t i = 0; i != 4; ++i)
		random_numbers[i] = animate_noise ? wang_random_number(noise->random_seed * 4 + i) : (i * 0x123456);
	if (animate_noise) ++noise->random_seed;
}
