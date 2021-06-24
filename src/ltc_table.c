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


#include "ltc_table.h"
#include "math_utilities.h"
#include "string_utilities.h"
#include <stdio.h>
#include <math.h>

int load_ltc_table(ltc_table_t* table, const device_t* device, const char* directory, uint32_t fresnel_count) {
	memset(table, 0, sizeof(*table));
	table->fresnel_count = fresnel_count;
	buffers_t staging;
	uint16_t* staging_data[2];
	uint32_t channel_counts[2] = { 4, 2 };
	uint32_t slice_sizes[2];
	// Iterate over the slices
	for (uint32_t i = 0; i != fresnel_count; ++i) {
		// Open the file
		char index_string[16];
		sprintf(index_string, "%u", i);
		const char* path_pieces[] = {directory, "/fit", index_string, ".dat"};
		char* file_path = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
		FILE* file = fopen(file_path, "rb");
		if (!file) {
			printf("Failed to open the linearly transformed cosine table at %s.\n", file_path);
			destroy_buffers(&staging, device);
			free(file_path);
			return 1;
		}
		free(file_path);
		// Read the resolution
		uint64_t resolution;
		fread(&resolution, sizeof(resolution), 1, file);
		if (table->roughness_count == 0) {
			// Allocate memory
			table->roughness_count = table->inclination_count = (uint32_t) resolution;
			VkBufferCreateInfo buffer_infos[2] = {0};
			for (uint32_t j = 0; j != 2; ++j) {
				slice_sizes[j] = (uint32_t) (resolution * resolution * channel_counts[j]);
				buffer_infos[j].sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				buffer_infos[j].size = sizeof(uint16_t) * slice_sizes[j] * fresnel_count;
				buffer_infos[j].usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			}
			if (create_buffers(&staging, device, buffer_infos, 2, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				printf("Failed to allocate staging buffers for linearly transformed cosine tables.\n");
				fclose(file);
				return 1;
			}
			uint16_t* data;
			if (vkMapMemory(device->device, staging.memory, 0, staging.size, 0, (void**) &data)) {
				printf("Failed to map staging memory for linearly transformed cosine tables.\n");
				destroy_buffers(&staging, device);
				fclose(file);
				return 1;
			}
			staging_data[0] = data;
			staging_data[1] = data + slice_sizes[0] * fresnel_count;
		}
		// Verify consistent resolutions
		else if (resolution != table->roughness_count) {
			printf("The linearly transformed cosine tables in directory %s have inconsistent resolutions. One has resolution %llux%llu, another %ux%u.\n",
				directory, resolution, resolution, table->fresnel_count, table->fresnel_count);
			destroy_buffers(&staging, device);
			fclose(file);
			return 1;
		}
		// Load one matrix after the other and quantize
		for (uint32_t j = 0; j != resolution * resolution; ++j) {
			float data[5];
			fread((char*)data, sizeof(float), 5, file);
			// Invert the matrix (disregarding a constant factor)
			float inverse[3][3] = {
				{data[2], 0.0f, -data[1] * data[2]},
				{0.0f, data[0] - data[1] * data[3], 0.0f},
				{-data[2] * data[3], 0.0f, data[0] * data[2]}
			};
			// Normalize such that the entry of maximal magnitude has magnitude
			// one. This way, 16-bit UNORM quantization is applicable but we
			// need to store five entries per matrix.
			float max_magnitude = fabsf(inverse[0][0]);
			for (uint32_t k = 0; k != 3; ++k)
				for (uint32_t l = 0; l != 3; ++l)
					if (max_magnitude < fabsf(inverse[k][l]))
						max_magnitude = fabsf(inverse[k][l]);
			for (uint32_t k = 0; k != 3; ++k)
				for (uint32_t l = 0; l != 3; ++l)
					inverse[k][l] /= max_magnitude;
			// Quantize and store
			float processed_data[6] = {inverse[0][0], inverse[0][2], inverse[1][1], inverse[2][0], inverse[2][2], data[4]};
			uint32_t data_index = 0;
			for (uint32_t k = 0; k != 2; ++k) {
				for (uint32_t l = 0; l != channel_counts[k]; ++l) {
					float data = processed_data[data_index];
					data *= (data_index == 1) ? -1.0f : 1.0f;
					if (data < 0.0f) data = 0.0f;
					if (data > 1.0f) data = 1.0f;
					uint16_t quantized_data = (uint16_t) (data * 65535.0f + 0.5f);
					staging_data[k][slice_sizes[k] * i + channel_counts[k] * j + l] = quantized_data;
					++data_index;
				}
			}
		}
	}
	vkUnmapMemory(device->device, staging.memory);
	// Construct device local texture arrays
	image_request_t requests[2] = {
		{
			.image_info = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				.imageType = VK_IMAGE_TYPE_2D,
				.format = VK_FORMAT_R16G16B16A16_UNORM,
				.extent = {table->roughness_count, table->inclination_count, 1},
				.mipLevels = 1,
				.arrayLayers = fresnel_count,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
			},
			.view_info = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
				.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT}
			}
		}
	};
	requests[1] = requests[0];
	requests[1].image_info.format = VK_FORMAT_R16G16_UNORM;
	if (create_images(&table->texture_arrays, device, requests, 2, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) {
		printf("Failed to create device local textures for LTC tables.");
		destroy_buffers(&staging, device);
		return 1;
	}
	// Copy from the staging buffers to the texture arrays
	VkBufferImageCopy copies[2] = {
		{
			.imageExtent = {table->roughness_count, table->inclination_count, 1},
			.imageSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.layerCount = fresnel_count
			}
		}
	};
	copies[1] = copies[0];
	VkImage images[] = {table->texture_arrays.images[0].image, table->texture_arrays.images[1].image};
	VkBuffer staging_buffers[] = {staging.buffers[0].buffer, staging.buffers[1].buffer};
	if (copy_buffers_to_images(device, COUNT_OF(copies), staging_buffers, images,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, copies))
	{
		printf("Failed to copy linearly transformed cosine coefficients from the staging buffer to device local memory.\n");
		destroy_ltc_table(table, device);
		destroy_buffers(&staging, device);
		return 1;
	}
	destroy_buffers(&staging, device);
	// Create the sampler
	VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	};
	if (vkCreateSampler(device->device, &sampler_info, NULL, &table->sampler)) {
		printf("Failed to create a sampler for reading from linearly transformed cosine tables.\n");
		destroy_ltc_table(table, device);
		return 1;
	}
	// Prepare constants
	ltc_constants_t constants = {
		.fresnel_index_factor = (float) (table->fresnel_count - 1),
		.fresnel_index_summand = 0.0f,
		.roughness_factor = (float) (table->roughness_count - 1)/ (float) table->roughness_count,
		.roughness_summand = 0.5f / (float) table->roughness_count,
		.inclination_factor = (float) (table->inclination_count - 1) / (0.5f * M_PI_F * table->inclination_count),
		.inclination_summand = 0.5f / (float) table->inclination_count
	};
	table->constants = constants;
	return 0;
}


void destroy_ltc_table(ltc_table_t* table, const device_t* device) {
	destroy_images(&table->texture_arrays, device);
	if(table->sampler) vkDestroySampler(device->device, table->sampler, NULL);
}
