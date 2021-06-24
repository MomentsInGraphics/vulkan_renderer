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


#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "float_to_half.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>


/*! A subset of the VkFormat enumeration in Vulkan holding formats that can be
	output by this program.
	\see https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkFormat.html */
typedef enum vk_format_e {
	VK_FORMAT_R16G16B16_SFLOAT = 90,
	VK_FORMAT_R16G16B16A16_SFLOAT = 97,
	VK_FORMAT_R32G32B32_SFLOAT = 106,
	VK_FORMAT_R32G32B32A32_SFLOAT = 109,
	VK_FORMAT_BC1_RGB_UNORM_BLOCK = 131,
	VK_FORMAT_BC1_RGB_SRGB_BLOCK = 132,
	VK_FORMAT_BC5_UNORM_BLOCK = 141,
} vk_format_t;


/*! The header for texture files created by this program.*/
typedef struct texture_file_header_s {
	//! The value 0xbc1bc1
	int32_t file_marker;
	//! The file format version
	int32_t version;
	//! The number of mipmaps and the image dimensions
	int32_t mipmap_count, width, height;
	//! The format, i.e. one of the values from the Vulkan enumeration VkFormat
	int32_t format;
	//! Combined size of all mipmaps, excluding headers in bytes
	size_t payload_size;
} texture_file_header_t;


/*! The header for each individual mipmap.*/
typedef struct mipmap_header_s {
	//! The extent of the mipmap in pixels
	int32_t width, height;
	//! The size of the mipmap in bytes, excluding headers
	size_t size;
	//! The offset from the beginning of the payload to the mipmap in bytes
	size_t offset;
} mipmap_header_t;


/*! Converts the given scalars (which should be normalized to the range from
	zero to one) from a linear scale to the sRGB scale (from 0 to 255).*/
static inline uint8_t linear_to_srgb(float linear) {
	linear = (linear < 0.0f) ? 0.0f : linear;
	float srgb = (linear <= 0.0031308f) ? (12.92f * linear) : (1.055f * powf(linear, 1.0f / 2.4f) - 0.055f);
	return (uint8_t) roundf(srgb * 255.0f);
}


/*! Quantizes a value between zero and one to the range from 0 to 255.*/
static inline uint8_t quantize_linear(float linear) {
	return (uint8_t) roundf(linear * 255.0f);
}


/*! Converts the given scalar from an sRGB scale to linear scale between zero
	and one.*/
static inline float srgb_to_linear(uint8_t srgb) {
	float srgb_float = srgb * (1.0f / 255.0f);
	return (srgb_float <= 0.04045f) ? (srgb_float * (1.0f / 12.92f)) : powf(srgb_float * (1.0f / 1.055f) + 0.055f / 1.055f, 2.4f);
}


/*! Returns the required mipmap count for a square image with the given edge
	length in pixels when producing a complete hierarchy.*/
static inline int32_t get_mipmap_count(int32_t extent) {
	int32_t padded_extent = 2 * extent - 1;
	int32_t mipmap_count = 0;
	while (padded_extent > 0) {
		padded_extent &= 0x7ffffffe;
		padded_extent >>= 1;
		++mipmap_count;
	}
	return mipmap_count;
}


int main(int argc, char** argv) {
	// Grab and validate input arguments
	int32_t format_int = 0;
	if (argc >= 4)
		sscanf(argv[1], "%d", &format_int);
	vk_format_t format = (vk_format_t) format_int;
	vk_format_t known_formats[] = {
		VK_FORMAT_R16G16B16_SFLOAT,
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_FORMAT_R32G32B32_SFLOAT,
		VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_FORMAT_BC1_RGB_UNORM_BLOCK,
		VK_FORMAT_BC1_RGB_SRGB_BLOCK,
		VK_FORMAT_BC5_UNORM_BLOCK,
	};
	int32_t format_known = 0;
	for (int32_t i = 0; i != sizeof(known_formats) / sizeof(known_formats[0]); ++i)
		format_known |= (known_formats[i] == format);
	if (argc < 4 || !format_known) {
		printf("Usage: texture_compression <vk_format> <input_file_path> <output_file_path>\n");
		printf("vk_format can be one of the following integer values from the VkFormat enumeration in Vulkan:\n\
VK_FORMAT_R16G16B16_SFLOAT = 90\n\
VK_FORMAT_R16G16B16A16_SFLOAT = 97\n\
VK_FORMAT_R32G32B32_SFLOAT = 106\n\
VK_FORMAT_R32G32B32A32_SFLOAT = 109\n\
VK_FORMAT_BC1_RGB_UNORM_BLOCK = 131\n\
VK_FORMAT_BC1_RGB_SRGB_BLOCK = 132\n\
VK_FORMAT_BC5_UNORM_BLOCK = 141\n");
		printf("For a list of supported input file formats, see:\n");
		printf("https://github.com/nothings/stb/blob/master/stb_image.h\n");
		printf("The output format is *.vkt, which is a renderer specific format with mipmaps (similar to *.dds).\n");
		return 1;
	}
	const char* input_file_path = argv[argc - 2];
	const char* output_file_path = argv[argc - 1];

	// Prepare some information about the output format
	int32_t is_hdr = 0, is_half = 0, is_srgb = 0, is_bc1 = 0;
	size_t block_size = 0;
	size_t bits_per_pixel;
	int32_t channel_count = 3;
	switch (format) {
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		channel_count = 4;
		bits_per_pixel = 64;
		is_hdr = 1;
		is_half = 1;
		break;
	case VK_FORMAT_R16G16B16_SFLOAT:
		channel_count = 3;
		bits_per_pixel = 48;
		is_hdr = 1;
		is_half = 1;
		break;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		channel_count = 4;
		bits_per_pixel = 128;
		is_hdr = 1;
		break;
	case VK_FORMAT_R32G32B32_SFLOAT:
		channel_count = 3;
		bits_per_pixel = 96;
		is_hdr = 1;
		break;
	case VK_FORMAT_BC5_UNORM_BLOCK:
		block_size = 16;
		bits_per_pixel = 8;
		channel_count = 2;
		break;
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		is_srgb = 1;
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		bits_per_pixel = 4;
		block_size = 8;
		is_bc1 = 1;
		break;
	default:
		break;
	}

	// Prepare the image
	int32_t width, height, input_channel_count, pixel_count;
	float* linear_image;
	if (is_hdr) {
		// Open the image
		linear_image = stbi_loadf(input_file_path, &width, &height, &input_channel_count, channel_count);
		input_channel_count = channel_count;
		if (!linear_image) {
			printf("Failed to load the HDR image at path %s.\n", input_file_path);
			return 1;
		}
		pixel_count = width * height;
	}
	else {
		// Open the image
		uint8_t* loaded_image = stbi_load(input_file_path, &width, &height, &input_channel_count, 0);
		if (!loaded_image) {
			printf("Failed to load the image at path %s.\n", input_file_path);
			return 1;
		}
		// Convert to linear RGB and discard superfluous channels
		pixel_count = width * height;
		linear_image = malloc(pixel_count * channel_count * sizeof(float));
		if (is_srgb)
			for (int32_t i = 0; i != pixel_count; ++i)
				for (int32_t j = 0; j != channel_count; ++j)
					linear_image[i * channel_count + j] = srgb_to_linear(loaded_image[i * input_channel_count + j]);
		else
			for (int32_t i = 0; i != pixel_count; ++i)
				for (int32_t j = 0; j != channel_count; ++j)
					linear_image[i * channel_count + j] = loaded_image[i * input_channel_count + j] * (1.0f / 255.0f);
		// We no longer need the LDR image
		stbi_image_free(loaded_image);
		loaded_image = NULL;
	}

	// Check the channel count
	if (input_channel_count < channel_count) {
		printf("The image at path %s has %d channels but needs to have at least %d.\n", input_file_path, input_channel_count, channel_count);
		free(linear_image);
		return 1;
	}
	// Determine how many mipmaps we need (we do not go all the way to 1x1 if
	// the aspect ratio is not one)
	int32_t mipmap_count_width = get_mipmap_count(width);
	int32_t mipmap_count_height = get_mipmap_count(height);
	int32_t mipmap_count = (mipmap_count_width < mipmap_count_height) ? mipmap_count_width : mipmap_count_height;
	// The image must have power of two size
	if (width != (1 << (mipmap_count_width - 1)) || height != (1 << (mipmap_count_height - 1))) {
		printf("The image at path %s has extent %dx%d but it must be a power of two for both dimensions.\n", input_file_path, width, height);
		free(linear_image);
		return 1;
	}

	if (block_size) {
		// Block compression only goes down to 4x4 blocks
		mipmap_count -= 2;
		// 1x1 images are useful and easy enough to fix
		if (width == 1 && height == 1) {
			mipmap_count = 1;
			width = height = 4;
			pixel_count = 16;
			float color[4];
			for (int32_t j = 0; j != channel_count; ++j)
				color[j] = linear_image[j];
			free(linear_image);
			linear_image = malloc(pixel_count * channel_count * sizeof(float));
			for (int32_t i = 0; i != pixel_count; ++i)
				for (int32_t j = 0; j != channel_count; ++j)
				linear_image[i * channel_count + j] = color[j];
		}
		if (width < 4 || height < 4) {
			printf("The image at path %s has extent %dx%d but it must be at least 4x4 for block compression to work.\n", input_file_path, width, height);
			free(linear_image);
			return 1;
		}
	}

	// Open the output file for writing
	FILE* file = fopen(output_file_path, "wb");
	if (!file) {
		printf("Failed to open the output file: %s\n", output_file_path);
		free(linear_image);
		return 1;
	}
	// Write the header
	texture_file_header_t header = {
		.file_marker = 0xbc1bc1,
		.version = 1,
		.mipmap_count = mipmap_count,
		.width = width, .height = height,
		.format = (int32_t) format
	};
	for (int32_t i = 0; i != mipmap_count; ++i) {
		mipmap_header_t mipmap_header = { .width = width >> i, .height = height >> i };
		header.payload_size += (mipmap_header.width * mipmap_header.height * bits_per_pixel) / 8;
	}
	fwrite((void*) &header, sizeof(int32_t), 8, file);
	// Write meta data about each mipmap (even though it is redundant)
	size_t mipmap_offset = 0;
	for (int32_t i = 0; i != mipmap_count; ++i) {
		mipmap_header_t mipmap_header = { .width = width >> i, .height = height >> i };
		mipmap_header.size = (mipmap_header.width * mipmap_header.height * bits_per_pixel) / 8;
		mipmap_header.offset = mipmap_offset;
		mipmap_offset += mipmap_header.size;
		fwrite((void*) &mipmap_header, sizeof(mipmap_header), 1, file);
	}

	// Allocate scratch memory for the largest mipmap (it will be used for all
	// of them)
	float* linear_mipmap = malloc(((sizeof(float) * width * height) / 4) * channel_count);
	// Generate mipmaps
	for (int32_t i = 0; i != mipmap_count; ++i) {
		int32_t mipmap_width = width >> i;
		int32_t mipmap_height = height >> i;
		// For the highest resolution mipmap, we skip filtering
		float* mipmap;
		if (mipmap_width == width)
			mipmap = linear_image;
		else {
			mipmap = linear_mipmap;
			// Prepare the normalized Gaussian filter
			int32_t filter_scale = (1 << i);
			int32_t stride = filter_scale;
			float standard_deviation = 0.4f * filter_scale;
			float gaussian_factor = -0.5f / (standard_deviation * standard_deviation);
			int32_t filter_extent = (int32_t) ceilf(3.0f * standard_deviation);
			float filter_center = filter_extent - 0.5f;
			float* filter_weights = malloc(2 * filter_extent * sizeof(float));
			float total_weight = 0.0f;
			for (int32_t j = 0; j != 2 * filter_extent; ++j)
				total_weight += filter_weights[j] = expf(gaussian_factor * (j - filter_center) * (j - filter_center));
			float normalization = 1.0f / total_weight;
			for (int32_t j = 0; j != 2 * filter_extent; ++j)
				filter_weights[j] *= normalization;
			int32_t offset = stride / 2 - filter_extent;
			// Iterate over output pixels
			int32_t mask_x = width - 1;
			int32_t mask_y = height - 1;
			for (int32_t y = 0; y != mipmap_height; ++y) {
				for (int32_t x = 0; x != mipmap_width; ++x) {
					float* pixel = mipmap + channel_count * (y * mipmap_width + x);
					for (int32_t l = 0; l != channel_count; ++l)
						pixel[l] = 0.0f;
					// Iterate over the filter footprint
					for (int32_t k = 0; k != 2 * filter_extent; ++k) {
						for (int32_t j = 0; j != 2 * filter_extent; ++j) {
							int32_t source_x = x * stride + offset + j;
							source_x &= mask_x;
							int32_t source_y = y * stride + offset + k;
							source_y &= mask_y;
							int32_t pixel_start = channel_count * (source_y * width + source_x);
							float weight = filter_weights[j] * filter_weights[k];
							for (int32_t l = 0; l != channel_count; ++l)
								pixel[l] += weight * linear_image[pixel_start + l];
						}
					}
				}
			}
			free(filter_weights);
		}

		// Quantize, apply block compression and store
		if (is_bc1) {
			uint8_t block[4 * 4 * 4] = {0}, compressed[8];
			// Iterate over blocks
			for (int32_t y = 0; y != mipmap_height; y += 4) {
				for (int32_t x = 0; x != mipmap_width; x += 4) {
					// Quantize the block
					for (int32_t k = 0; k != 4; ++k)
						for (int32_t j = 0; j != 4; ++j)
							for (int32_t l = 0; l != 3; ++l)
								block[(k * 4 + j) * 4 + l] = is_srgb ? linear_to_srgb(mipmap[3 * ((y + k) * mipmap_width + x + j) + l])
																	: quantize_linear(mipmap[3 * ((y + k) * mipmap_width + x + j) + l]);
					// Apply block compression
					stb_compress_dxt_block(compressed, block, 0, STB_DXT_HIGHQUAL);
					// Store the block
					fwrite((void*) compressed, sizeof(uint8_t), sizeof(compressed), file);
				}
			}
		}
		else if(format == VK_FORMAT_BC5_UNORM_BLOCK) {
			uint8_t block[4 * 4 * 2], compressed[16];
			// Iterate over blocks
			for (int32_t y = 0; y != mipmap_height; y += 4) {
				for (int32_t x = 0; x != mipmap_width; x += 4) {
					// Quantize the block
					for (int32_t k = 0; k != 4; ++k)
						for (int32_t j = 0; j != 4; ++j)
							for (int32_t l = 0; l != 2; ++l)
								block[(k * 4 + j) * 2 + l] = quantize_linear(mipmap[2 * ((y + k) * mipmap_width + x + j) + l]);
					// Apply block compression
					stb_compress_bc5_block(compressed, block);
					// Store the block
					fwrite((void*) compressed, sizeof(uint8_t), sizeof(compressed), file);
				}
			}
		}
		else if (is_half) {
			uint16_t pixel[4] = {0};
			for (int32_t y = 0; y != mipmap_height; ++y) {
				for (int32_t x = 0; x != mipmap_width; ++x) {
					for (int32_t l = 0; l != channel_count; ++l)
						pixel[l] = float_to_half(mipmap[(y * mipmap_width + x) * channel_count + l]);
					fwrite((void*) pixel, sizeof(uint16_t), channel_count, file);
				}
			}
		}
		else if (is_hdr)
			fwrite(mipmap, sizeof(float), mipmap_width * mipmap_height * channel_count, file);
	}

	// Write an end of file marker
	int32_t eof = 0xe0fe0f;
	fwrite((void*) &eof, sizeof(eof), 1, file);
	// Clean up
	fclose(file);
	free(linear_image);
	free(linear_mipmap);
	return 0;
}
