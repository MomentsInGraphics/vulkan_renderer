// Copyright (C) 2022, Christoph Peters, Karlsruhe Institute of Technology
//
// This source code file is licensed under both the three-clause BSD license
// and the GPLv3. You may select, at your option, one of the two licenses. The
// corresponding license headers follow:
//
//
// Three-clause BSD license:
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
//  3. Neither the name of the copyright holder nor the names of its
//     contributors may be used to endorse or promote products derived from
//     this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  POSSIBILITY OF SUCH DAMAGE.
//
//
// GPLv3:
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


#include "blend_attribute_compression.h"
#include "optimal_simplex_sampling.h"
#include "blend_attribute_compression_related_work.h"
#include "permutation_coding.h"
#include <memory.h>
#include <stdint.h>

//! Lots of stack arrays use this size, so this is the largest number of bone
//! influences per vertex that this implementation supports
#define SUPPORTED_BONE_COUNT 13


//! \return Number of bits used for weights by optimal simplex sampling
static inline uint32_t get_optimal_simplex_sampling_bit_count(blend_attribute_compression_method_t method) {
	switch (method) {
	case blend_attribute_compression_optimal_simplex_sampling_19:
		return 19;
	case blend_attribute_compression_optimal_simplex_sampling_22:
		return 22;
	case blend_attribute_compression_optimal_simplex_sampling_35:
		return 35;
	default:
		return 0;
	}
}


//! Utility struct for sorting
typedef struct index_weight_pair_s {
	//! A bone index
	uint16_t index;
	//! The corresponding weight
	float weight;
} index_weight_pair_t;


//! Used in qsort() to sort weights in ascending order
static int compare_weights(const index_weight_pair_t* lhs, const index_weight_pair_t* rhs) {
	return (lhs->weight > rhs->weight) ? 1 : ((lhs->weight < rhs->weight) ? -1 : 0);
}


//! Used in qsort() to sort index tuples in descending order
static int compare_indices(const uint16_t* lhs, const uint16_t* rhs) {
	return (*lhs) == (*rhs) ? 0 : (((*lhs) < (*rhs)) ? 1 : -1);
}


/*! Used in qsort() to sort reverted tuples of bone indices in lexicographic
	order. The first two entries are ignored, the third is the tuple length. It
	is stored per tuple for the sake of a thread-safe implementation that does
	not rely on qsort_s() (introduced in C11).*/
static int compare_index_tuples(const uint16_t* lhs, const uint16_t* rhs) {
	uint16_t length = lhs[2];
	lhs += length + 2;
	rhs += length + 2;
	for (uint16_t i = 0; i != length; ++i, --lhs, --rhs)
		if ((*lhs) != (*rhs))
			return ((*lhs) < (*rhs)) ? -1 : 1;
	return 0;
}


//! Fills an array of pairs for a vertex from a strided array, computes the
//! last weight and sorts weights in ascending order
static inline void get_sorted_pairs(index_weight_pair_t out_pairs[SUPPORTED_BONE_COUNT], const uint16_t* indices, size_t index_stride, const float* weights, size_t weight_stride, uint32_t max_bone_count, uint64_t vertex_index) {
	const uint16_t* vertex_indices = (const uint16_t*) (((const char*) indices) + vertex_index * index_stride);
	const float* vertex_weights = (const float*) (((const char*) weights) + vertex_index * weight_stride);
	float last_weight = 1.0f;
	for (uint32_t i = 0; i != max_bone_count - 1; ++i) {
		out_pairs[i].index = vertex_indices[i];
		out_pairs[i].weight = vertex_weights[i];
		last_weight -= vertex_weights[i];
	}
	out_pairs[max_bone_count - 1].index = vertex_indices[max_bone_count - 1];
	out_pairs[max_bone_count - 1].weight = last_weight;
	qsort(out_pairs, max_bone_count, sizeof(index_weight_pair_t), (int (*)(const void*, const void*)) compare_weights);
}


/*! Compresses and decompresses the given sorted weights using the given method
	and determines which of them are zero.
	\return Bit i is set iff weight i is zero (or negative) after compression
		and decompression.*/
static inline uint32_t flag_zero_compressed_weights(const index_weight_pair_t* pairs, const blend_attribute_compression_parameters_t* params) {
	float weights[SUPPORTED_BONE_COUNT];
	for (uint32_t i = 0; i != params->max_bone_count; ++i)
		weights[i] = pairs[i].weight;
	// Compress and decompress the weights (or at least figure out which will
	// be zero)
	switch (params->method) {
	case blend_attribute_compression_unit_cube_sampling: {
		for (uint32_t i = 0; i != params->max_bone_count - 1; ++i)
			weights[i] = (float) quantize_weight(weights[i], params->weight_base_bit_count);
		break;
	}
	case blend_attribute_compression_power_of_two_aabb: {
		for (uint32_t i = 0; i != params->max_bone_count - 1; ++i)
			weights[i] = (float) quantize_half_weight(weights[i], params->weight_base_bit_count);
		break;
	}
	case blend_attribute_compression_optimal_simplex_sampling_19:
	case blend_attribute_compression_optimal_simplex_sampling_22:
	case blend_attribute_compression_optimal_simplex_sampling_35: {
		uint32_t weights_bit_count = get_optimal_simplex_sampling_bit_count(params->method);
		uint64_t code;
		float weights_reverted[4] = { weights[3], weights[2], weights[1], weights[0] };
		vbac_oss_info codec = vbac_oss_compress(weights_reverted, 1, weights_bit_count, &code);
		vbac_oss_decompress(&code, 1, codec, weights_reverted);
		weights[0] = weights_reverted[3];
		weights[1] = weights_reverted[2];
		weights[2] = weights_reverted[1];
		weights[3] = weights_reverted[0];
		break;
	}
	case blend_attribute_compression_permutation_coding: {
		uint64_t code = compress_blend_attributes(weights, 0, params->permutation_coding);
		int valid;
		decompress_blend_attributes(weights, &valid, code, params->permutation_coding);
		break;
	}
	default:
	case blend_attribute_compression_none:
		break;
	}
	uint32_t result = 0;
	for (uint32_t i = 0; i != params->max_bone_count; ++i)
		result += (weights[i] <= 0.0f) ? (1 << i) : 0;
	return result;
}


/*! Writes binary data for the compressed representation of the given weights.
	\param compressed Pointer to the compressed output. params->vertex_size
		bytes are written.
	\param pairs Output of get_sorted_pairs().
	\param tuple_index The index that gets stored alongside the weights.
	\param params Parameters for the compression scheme.*/
static inline void compress_vertex_blend_attributes(void* compressed, const index_weight_pair_t pairs[SUPPORTED_BONE_COUNT], uint32_t tuple_index, const blend_attribute_compression_parameters_t* params) {
	float weights[SUPPORTED_BONE_COUNT];
	for (uint32_t i = 0; i != SUPPORTED_BONE_COUNT; ++i)
		weights[i] = pairs[i].weight;
	switch (params->method) {
	case blend_attribute_compression_unit_cube_sampling: {
		for (uint32_t i = 0; i != params->max_bone_count - 1; ++i) {
			uint32_t quantized = quantize_weight(weights[i], params->weight_base_bit_count);
			long_bitfield_insert(compressed, quantized, i * params->weight_base_bit_count, params->weight_base_bit_count);
		}
		long_bitfield_insert(compressed, tuple_index, (params->max_bone_count - 1) * params->weight_base_bit_count, params->tuple_index_bit_count);
		break;
	}
	case blend_attribute_compression_power_of_two_aabb: {
		uint32_t next_bit = 0;
		for (uint32_t i = 0; i != params->max_bone_count - 1; ++i) {
			uint32_t quantized = quantize_half_weight(weights[params->max_bone_count - 2 - i], params->weight_base_bit_count);
			uint32_t bit_count = params->weight_base_bit_count - power_of_two_weight_savings[i];
			long_bitfield_insert(compressed, quantized, next_bit, bit_count);
			next_bit += bit_count;
		}
		long_bitfield_insert(compressed, tuple_index, next_bit, params->tuple_index_bit_count);
		break;
	}
	case blend_attribute_compression_optimal_simplex_sampling_19:
	case blend_attribute_compression_optimal_simplex_sampling_22:
	case blend_attribute_compression_optimal_simplex_sampling_35: {
		uint32_t weights_bit_count = get_optimal_simplex_sampling_bit_count(params->method);
		uint64_t code;
		float weights_reverted[4] = { weights[3], weights[2], weights[1], weights[0] };
		vbac_oss_compress(weights_reverted, 1, weights_bit_count, &code);
		code |= (((uint64_t) tuple_index) << weights_bit_count);
		memcpy(compressed, &code, params->vertex_size);
		break;
	}
	case blend_attribute_compression_permutation_coding: {
		uint64_t code = compress_blend_attributes(weights, tuple_index, params->permutation_coding);
		memcpy(compressed, &code, params->vertex_size);
		break;
	}
	default:
		break;
	}
}


void complete_blend_attribute_compression_parameters(blend_attribute_compression_parameters_t* params) {
	// Clamp the maximal bone count
	if (params->max_bone_count < 2) params->max_bone_count = 2;
	if (params->max_bone_count > 13) params->max_bone_count = 13;
	// Figure out how many bits are needed for the tuple index
	uint32_t tuple_index_bit_count = 0;
	while ((1 << tuple_index_bit_count) < params->max_tuple_count)
		++tuple_index_bit_count;
	// Lookup table to implement this method. The first index gives the
	// supported tuple count (see permutation_tuple_counts), the second is the
	// bone count - 2, the third is the number of bytes - 1. Supported tuple
	// counts are fixed. This table comes out of optimal_coding.py.
	static const blend_attribute_codec_t permutation_codecs[5][12][8] = {
		{
			{ {1, 2, {1,}, 128}, {1, 512, {1,}, 128}, {0}, {0}, {0}, {0}, {0}, {0}, },
			{ {0}, {2, 32, {1, 1}, 64}, {2, 362, {1, 2}, 128}, {2, 5792, {1, 2}, 128}, {0}, {0}, {0}, {0}, },
			{ {0}, {3, 11, {1, 1, 2}, 43}, {3, 73, {1, 1, 2}, 43}, {3, 463, {1, 1, 2}, 43}, {3, 812, {3, 4, 8}, 2048}, {3, 5160, {3, 4, 8}, 2048}, {3, 32767, {3, 4, 8}, 2048}, {0}, },
			{ {0}, {4, 8, {1, 1, 1, 3}, 16}, {4, 32, {1, 1, 1, 3}, 16}, {4, 107, {1, 1, 2, 3}, 32}, {4, 256, {2, 2, 3, 4}, 256}, {4, 1024, {2, 2, 3, 4}, 256}, {4, 2048, {4, 4, 6, 8}, 4096}, {4, 16384, {2, 2, 3, 4}, 256}, },
			{ {0}, {5, 8, {1, 1, 1, 1, 1}, 2}, {5, 20, {1, 1, 1, 2, 2}, 5}, {5, 57, {1, 1, 1, 2, 3}, 7}, {5, 128, {1, 1, 2, 3, 5}, 32}, {5, 128, {4, 4, 5, 8, 12}, 8192}, {5, 1024, {1, 2, 2, 3, 5}, 64}, {5, 1024, {4, 5, 6, 8, 16}, 16384}, },
			{ {0}, {0}, {6, 14, {1, 1, 1, 1, 2, 5}, 2}, {6, 33, {1, 1, 1, 2, 2, 4}, 3}, {6, 64, {1, 1, 2, 2, 3, 7}, 15}, {6, 128, {1, 2, 2, 2, 4, 11}, 63}, {6, 128, {4, 4, 5, 7, 10, 16}, 15929}, {6, 256, {4, 8, 8, 8, 11, 16}, 64080}, },
			{ {0}, {0}, {7, 10, {1, 1, 1, 2, 2, 2, 4}, 1}, {7, 23, {1, 1, 1, 2, 2, 2, 4}, 1}, {7, 52, {1, 1, 1, 2, 2, 2, 4}, 1}, {7, 64, {2, 2, 2, 3, 3, 4, 8}, 59}, {7, 128, {2, 2, 2, 3, 4, 5, 10}, 122}, {7, 256, {2, 2, 2, 4, 4, 7, 11}, 251}, },
			{ {0}, {0}, {0}, {8, 16, {1, 1, 1, 2, 2, 3, 4, 6}, 1}, {8, 32, {1, 1, 1, 2, 2, 3, 4, 6}, 1}, {8, 64, {1, 1, 1, 2, 2, 3, 4, 6}, 1}, {8, 128, {1, 1, 1, 2, 2, 3, 4, 6}, 1}, {8, 256, {1, 1, 1, 2, 2, 3, 4, 6}, 1}, },
			{ {0}, {0}, {0}, {9, 11, {1, 1, 2, 2, 2, 2, 4, 5, 8}, 1}, {9, 21, {1, 1, 2, 2, 2, 2, 4, 5, 8}, 1}, {9, 32, {2, 2, 2, 2, 2, 3, 4, 5, 10}, 7}, {9, 74, {1, 1, 2, 2, 2, 2, 4, 5, 8}, 1}, {9, 64, {2, 3, 4, 4, 4, 5, 8, 11, 16}, 954}, },
			{ {0}, {0}, {0}, {0}, {10, 16, {1, 2, 2, 2, 2, 2, 4, 4, 5, 11}, 1}, {10, 27, {1, 2, 2, 2, 2, 2, 4, 4, 5, 11}, 1}, {10, 32, {2, 2, 3, 3, 4, 4, 4, 6, 8, 16}, 63}, {10, 64, {2, 2, 2, 2, 3, 4, 4, 6, 8, 12}, 16}, },
			{ {0}, {0}, {0}, {0}, {11, 12, {2, 2, 2, 2, 2, 2, 3, 4, 4, 8, 12}, 1}, {11, 20, {2, 2, 2, 2, 2, 2, 3, 4, 4, 8, 12}, 1}, {11, 32, {2, 2, 2, 2, 2, 3, 4, 4, 4, 8, 12}, 2}, {11, 56, {2, 2, 2, 2, 2, 2, 3, 4, 4, 8, 12}, 1}, },
			{ {0}, {0}, {0}, {0}, {0}, {12, 15, {2, 2, 2, 2, 2, 4, 4, 4, 4, 7, 8, 16}, 2}, {12, 25, {2, 2, 2, 2, 2, 2, 4, 4, 4, 7, 8, 16}, 1}, {12, 40, {2, 2, 2, 2, 2, 2, 4, 4, 4, 7, 8, 16}, 1}, },
		},
		{
			{ {0}, {1, 128, {1,}, 512}, {1, 32768, {1,}, 512}, {0}, {0}, {0}, {0}, {0}, },
			{ {0}, {2, 16, {1, 1}, 256}, {2, 181, {1, 2}, 512}, {2, 2896, {1, 2}, 512}, {2, 46340, {1, 2}, 512}, {0}, {0}, {0}, },
			{ {0}, {3, 9, {1, 1, 1}, 86}, {3, 46, {1, 1, 2}, 171}, {3, 292, {1, 1, 2}, 171}, {3, 511, {3, 4, 8}, 8192}, {3, 3250, {3, 4, 8}, 8192}, {3, 20642, {3, 4, 8}, 8192}, {0}, },
			{ {0}, {4, 7, {1, 1, 1, 1}, 22}, {4, 21, {1, 1, 2, 2}, 86}, {4, 64, {1, 2, 2, 3}, 256}, {4, 128, {2, 3, 4, 8}, 4096}, {4, 512, {2, 3, 4, 8}, 4096}, {4, 2048, {2, 3, 4, 8}, 4096}, {4, 8192, {2, 3, 4, 8}, 4096}, },
			{ {0}, {5, 6, {1, 1, 1, 1, 1}, 5}, {5, 16, {1, 1, 1, 1, 3}, 13}, {5, 44, {1, 1, 1, 2, 3}, 26}, {5, 64, {2, 2, 3, 4, 5}, 1024}, {5, 256, {1, 2, 2, 3, 5}, 256}, {5, 512, {2, 2, 3, 5, 8}, 2048}, {5, 1024, {3, 4, 4, 8, 10}, 16384}, },
			{ {0}, {0}, {6, 13, {1, 1, 1, 1, 2, 2}, 3}, {6, 33, {1, 1, 1, 1, 2, 2}, 3}, {6, 64, {1, 1, 1, 2, 2, 5}, 15}, {6, 64, {2, 3, 3, 4, 7, 11}, 3943}, {6, 128, {3, 3, 4, 6, 8, 13}, 15975}, {6, 256, {4, 4, 4, 8, 11, 16}, 64080}, },
			{ {0}, {0}, {7, 10, {1, 1, 1, 1, 1, 2, 4}, 1}, {7, 23, {1, 1, 1, 1, 1, 2, 4}, 1}, {7, 32, {1, 1, 2, 2, 3, 4, 6}, 30}, {7, 64, {1, 2, 2, 2, 3, 4, 6}, 59}, {7, 128, {1, 2, 2, 2, 3, 5, 10}, 122}, {7, 128, {3, 4, 4, 5, 8, 10, 16}, 31208}, },
			{ {0}, {0}, {0}, {8, 16, {1, 1, 1, 1, 2, 2, 3, 6}, 1}, {8, 32, {1, 1, 1, 1, 2, 2, 3, 6}, 1}, {8, 64, {1, 1, 1, 1, 2, 2, 3, 6}, 1}, {8, 128, {1, 1, 1, 1, 2, 2, 3, 6}, 1}, {8, 256, {1, 1, 1, 1, 2, 2, 3, 6}, 1}, },
			{ {0}, {0}, {0}, {9, 11, {1, 1, 1, 2, 2, 2, 3, 4, 7}, 1}, {9, 21, {1, 1, 1, 2, 2, 2, 3, 4, 7}, 1}, {9, 32, {1, 2, 2, 2, 2, 3, 3, 4, 8}, 7}, {9, 74, {1, 1, 1, 2, 2, 2, 3, 4, 7}, 1}, {9, 64, {2, 2, 3, 4, 4, 4, 7, 8, 16}, 971}, },
			{ {0}, {0}, {0}, {0}, {10, 16, {1, 1, 2, 2, 2, 2, 3, 4, 4, 9}, 1}, {10, 27, {1, 1, 2, 2, 2, 2, 3, 4, 4, 9}, 1}, {10, 32, {2, 2, 2, 2, 3, 4, 4, 6, 8, 12}, 63}, {10, 64, {2, 2, 2, 2, 2, 3, 4, 4, 6, 12}, 16}, },
			{ {0}, {0}, {0}, {0}, {11, 12, {1, 2, 2, 2, 2, 2, 3, 4, 4, 5, 10}, 1}, {11, 20, {1, 2, 2, 2, 2, 2, 3, 4, 4, 5, 10}, 1}, {11, 32, {1, 2, 2, 2, 2, 2, 3, 4, 4, 8, 12}, 2}, {11, 56, {1, 2, 2, 2, 2, 2, 3, 4, 4, 5, 10}, 1}, },
			{ {0}, {0}, {0}, {0}, {0}, {12, 15, {2, 2, 2, 2, 2, 2, 3, 4, 4, 5, 8, 15}, 2}, {12, 25, {2, 2, 2, 2, 2, 2, 2, 4, 4, 4, 8, 14}, 1}, {12, 40, {2, 2, 2, 2, 2, 2, 2, 4, 4, 4, 8, 14}, 1}, },
		},
		{
			{ {0}, {1, 32, {1,}, 2048}, {1, 8192, {1,}, 2048}, {0}, {0}, {0}, {0}, {0}, },
			{ {0}, {2, 8, {1, 1}, 1024}, {2, 90, {1, 2}, 2048}, {2, 1448, {1, 2}, 2048}, {2, 23170, {1, 2}, 2048}, {0}, {0}, {0}, },
			{ {0}, {3, 5, {1, 1, 1}, 342}, {3, 29, {1, 1, 2}, 683}, {3, 184, {1, 1, 2}, 683}, {3, 322, {3, 4, 8}, 32768}, {3, 2047, {3, 4, 8}, 32768}, {3, 13003, {3, 4, 8}, 32768}, {0}, },
			{ {0}, {4, 5, {1, 1, 1, 1}, 86}, {4, 16, {1, 1, 1, 3}, 256}, {4, 64, {1, 1, 1, 3}, 256}, {4, 128, {2, 2, 3, 4}, 4096}, {4, 512, {2, 2, 3, 4}, 4096}, {4, 2048, {2, 2, 3, 4}, 4096}, {4, 8192, {2, 2, 3, 4}, 4096}, },
			{ {0}, {0}, {5, 13, {1, 1, 1, 1, 2}, 35}, {5, 33, {1, 1, 1, 2, 3}, 103}, {5, 64, {1, 2, 2, 3, 5}, 1024}, {5, 128, {2, 2, 3, 5, 8}, 8192}, {5, 256, {3, 4, 4, 8, 10}, 65536}, {5, 1024, {2, 3, 4, 5, 8}, 16384}, },
			{ {0}, {0}, {6, 11, {1, 1, 1, 1, 1, 3}, 9}, {6, 23, {1, 1, 1, 1, 2, 5}, 29}, {6, 60, {1, 1, 1, 1, 2, 4}, 23}, {6, 64, {2, 2, 3, 4, 4, 7}, 3823}, {6, 128, {2, 3, 3, 4, 7, 11}, 15770}, {6, 256, {3, 3, 4, 6, 7, 15}, 64512}, },
			{ {0}, {0}, {7, 10, {1, 1, 1, 1, 1, 1, 2}, 1}, {7, 18, {1, 1, 1, 1, 2, 2, 4}, 7}, {7, 32, {1, 1, 1, 2, 2, 3, 6}, 30}, {7, 64, {1, 1, 2, 2, 2, 3, 6}, 59}, {7, 128, {1, 1, 2, 2, 3, 5, 5}, 122}, {7, 128, {2, 4, 4, 4, 6, 8, 13}, 32456}, },
			{ {0}, {0}, {0}, {8, 16, {1, 1, 1, 1, 1, 2, 2, 4}, 1}, {8, 32, {1, 1, 1, 1, 1, 2, 2, 4}, 1}, {8, 64, {1, 1, 1, 1, 1, 2, 2, 4}, 1}, {8, 128, {1, 1, 1, 1, 1, 2, 2, 4}, 1}, {8, 256, {1, 1, 1, 1, 1, 2, 2, 4}, 1}, },
			{ {0}, {0}, {0}, {9, 11, {1, 1, 1, 1, 2, 2, 2, 4, 5}, 1}, {9, 21, {1, 1, 1, 1, 2, 2, 2, 4, 5}, 1}, {9, 32, {1, 1, 2, 2, 2, 2, 3, 4, 6}, 7}, {9, 74, {1, 1, 1, 1, 2, 2, 2, 4, 5}, 1}, {9, 64, {2, 2, 2, 3, 3, 4, 6, 8, 13}, 1015}, },
			{ {0}, {0}, {0}, {0}, {10, 16, {1, 1, 1, 2, 2, 2, 2, 3, 4, 9}, 1}, {10, 27, {1, 1, 1, 2, 2, 2, 2, 3, 4, 9}, 1}, {10, 32, {2, 2, 2, 2, 2, 3, 4, 4, 6, 12}, 63}, {10, 64, {1, 2, 2, 2, 2, 2, 4, 4, 5, 11}, 16}, },
			{ {0}, {0}, {0}, {0}, {11, 12, {1, 1, 2, 2, 2, 2, 2, 3, 4, 6, 8}, 1}, {11, 20, {1, 1, 2, 2, 2, 2, 2, 3, 4, 6, 8}, 1}, {11, 32, {1, 1, 2, 2, 2, 2, 3, 4, 4, 6, 8}, 2}, {11, 56, {1, 1, 2, 2, 2, 2, 2, 3, 4, 6, 8}, 1}, },
			{ {0}, {0}, {0}, {0}, {0}, {12, 15, {1, 2, 2, 2, 2, 2, 2, 4, 4, 4, 8, 14}, 2}, {12, 25, {1, 1, 2, 2, 2, 2, 2, 4, 4, 4, 8, 14}, 1}, {12, 40, {1, 1, 2, 2, 2, 2, 2, 4, 4, 4, 8, 14}, 1}, },
		},
		{
			{ {0}, {1, 16, {1,}, 4096}, {1, 4096, {1,}, 4096}, {0}, {0}, {0}, {0}, {0}, },
			{ {0}, {2, 4, {1, 2}, 4096}, {2, 64, {1, 2}, 4096}, {2, 1024, {1, 2}, 4096}, {2, 16384, {1, 2}, 4096}, {2, 32768, {8, 16}, 262144}, {0}, {0}, },
			{ {0}, {3, 4, {1, 1, 1}, 683}, {3, 23, {1, 1, 2}, 1366}, {3, 146, {1, 1, 2}, 1366}, {3, 128, {6, 9, 14}, 516096}, {3, 1625, {3, 4, 8}, 65536}, {3, 10321, {3, 4, 8}, 65536}, {3, 65535, {3, 4, 8}, 65536}, },
			{ {0}, {0}, {4, 14, {1, 1, 1, 2}, 342}, {4, 45, {1, 1, 2, 3}, 1024}, {4, 128, {1, 2, 3, 4}, 4096}, {4, 256, {3, 4, 4, 8}, 65536}, {4, 1024, {3, 4, 4, 8}, 65536}, {4, 4096, {3, 4, 4, 8}, 65536}, },
			{ {0}, {0}, {5, 11, {1, 1, 1, 1, 3}, 103}, {5, 29, {1, 1, 1, 2, 3}, 205}, {5, 64, {1, 1, 2, 3, 5}, 1024}, {5, 128, {2, 2, 3, 4, 5}, 8192}, {5, 512, {1, 2, 2, 3, 5}, 2048}, {5, 512, {4, 5, 6, 8, 16}, 524288}, },
			{ {0}, {0}, {6, 10, {1, 1, 1, 1, 1, 2}, 12}, {6, 22, {1, 1, 1, 1, 2, 3}, 35}, {6, 56, {1, 1, 1, 1, 2, 3}, 35}, {6, 64, {2, 2, 2, 3, 4, 7}, 3823}, {6, 128, {2, 2, 4, 4, 4, 11}, 16020}, {6, 256, {2, 4, 4, 4, 8, 11}, 64080}, },
			{ {0}, {0}, {7, 10, {1, 1, 1, 1, 1, 1, 1}, 1}, {7, 18, {1, 1, 1, 1, 1, 2, 4}, 7}, {7, 32, {1, 1, 1, 2, 2, 2, 4}, 27}, {7, 64, {1, 1, 1, 2, 2, 3, 6}, 59}, {7, 128, {1, 1, 1, 2, 3, 5, 5}, 122}, {7, 128, {2, 3, 4, 4, 4, 8, 13}, 32456}, },
			{ {0}, {0}, {0}, {8, 16, {1, 1, 1, 1, 1, 1, 2, 4}, 1}, {8, 32, {1, 1, 1, 1, 1, 1, 2, 4}, 1}, {8, 64, {1, 1, 1, 1, 1, 1, 2, 4}, 1}, {8, 128, {1, 1, 1, 1, 1, 1, 2, 4}, 1}, {8, 128, {1, 2, 2, 2, 3, 3, 5, 7}, 256}, },
			{ {0}, {0}, {0}, {9, 11, {1, 1, 1, 1, 1, 2, 2, 4, 5}, 1}, {9, 21, {1, 1, 1, 1, 1, 2, 2, 4, 5}, 1}, {9, 32, {1, 1, 1, 2, 2, 2, 3, 4, 6}, 7}, {9, 66, {1, 1, 1, 1, 2, 2, 2, 4, 8}, 3}, {9, 64, {2, 2, 2, 2, 4, 4, 4, 8, 11}, 1018}, },
			{ {0}, {0}, {0}, {0}, {10, 16, {1, 1, 1, 1, 2, 2, 2, 3, 4, 9}, 1}, {10, 27, {1, 1, 1, 1, 2, 2, 2, 3, 4, 9}, 1}, {10, 48, {1, 1, 1, 1, 2, 2, 2, 3, 4, 9}, 1}, {10, 64, {1, 2, 2, 2, 2, 2, 3, 4, 4, 9}, 16}, },
			{ {0}, {0}, {0}, {0}, {11, 12, {1, 1, 1, 2, 2, 2, 2, 3, 4, 6, 8}, 1}, {11, 20, {1, 1, 1, 2, 2, 2, 2, 3, 4, 6, 8}, 1}, {11, 32, {1, 1, 2, 2, 2, 2, 2, 3, 4, 6, 8}, 2}, {11, 56, {1, 1, 1, 2, 2, 2, 2, 3, 4, 6, 8}, 1}, },
			{ {0}, {0}, {0}, {0}, {0}, {12, 15, {1, 1, 2, 2, 2, 2, 2, 4, 4, 4, 8, 14}, 2}, {12, 25, {1, 1, 2, 2, 2, 2, 2, 3, 4, 4, 8, 9}, 1}, {12, 40, {1, 1, 2, 2, 2, 2, 2, 3, 4, 4, 8, 9}, 1}, },
		},
		{
			{ {0}, {1, 9, {1,}, 7000}, {1, 2396, {1,}, 7000}, {0}, {0}, {0}, {0}, {0}, },
			{ {0}, {2, 4, {1, 1}, 3500}, {2, 48, {1, 2}, 7000}, {2, 783, {1, 2}, 7000}, {2, 12532, {1, 2}, 7000}, {0}, {0}, {0}, },
			{ {0}, {0}, {3, 19, {1, 1, 2}, 2334}, {3, 122, {1, 1, 2}, 2334}, {3, 128, {5, 8, 11}, 513334}, {3, 512, {8, 14, 16}, 2090667}, {3, 8632, {3, 4, 8}, 112000}, {3, 54815, {3, 4, 8}, 112000}, },
			{ {0}, {0}, {4, 13, {1, 1, 1, 2}, 584}, {4, 52, {1, 1, 1, 2}, 584}, {4, 64, {2, 4, 4, 7}, 65334}, {4, 128, {4, 7, 8, 16}, 1045334}, {4, 512, {4, 7, 8, 16}, 1045334}, {4, 4260, {2, 3, 4, 8}, 56000}, },
			{ {0}, {0}, {5, 12, {1, 1, 1, 1, 1}, 59}, {5, 32, {1, 1, 1, 1, 2}, 117}, {5, 79, {1, 1, 1, 2, 3}, 350}, {5, 64, {3, 4, 4, 7, 13}, 254800}, {5, 128, {4, 7, 8, 10, 16}, 2090667}, {5, 512, {4, 4, 5, 8, 14}, 522667}, },
			{ {0}, {0}, {6, 9, {1, 1, 1, 1, 1, 3}, 30}, {6, 24, {1, 1, 1, 1, 1, 2}, 20}, {6, 49, {1, 1, 1, 1, 2, 4}, 78}, {6, 64, {2, 2, 2, 2, 4, 6}, 3734}, {6, 128, {2, 2, 3, 4, 5, 7}, 16334}, {6, 256, {2, 3, 4, 4, 7, 10}, 65334}, },
			{ {0}, {0}, {7, 9, {1, 1, 1, 1, 1, 1, 2}, 3}, {7, 20, {1, 1, 1, 1, 1, 1, 2}, 3}, {7, 32, {1, 1, 1, 1, 2, 2, 5}, 28}, {7, 64, {1, 1, 1, 1, 2, 3, 7}, 59}, {7, 128, {1, 1, 1, 2, 3, 3, 5}, 125}, {7, 128, {2, 2, 3, 4, 5, 8, 12}, 32000}, },
			{ {0}, {0}, {0}, {8, 16, {1, 1, 1, 1, 1, 1, 2, 2}, 1}, {8, 32, {1, 1, 1, 1, 1, 1, 2, 2}, 1}, {8, 64, {1, 1, 1, 1, 1, 1, 2, 2}, 1}, {8, 64, {1, 2, 2, 2, 2, 3, 4, 7}, 234}, {8, 128, {1, 2, 2, 2, 2, 3, 5, 6}, 250}, },
			{ {0}, {0}, {0}, {9, 11, {1, 1, 1, 1, 1, 2, 2, 3, 4}, 1}, {9, 21, {1, 1, 1, 1, 1, 2, 2, 3, 4}, 1}, {9, 32, {1, 1, 1, 1, 2, 2, 3, 4, 7}, 7}, {9, 74, {1, 1, 1, 1, 1, 2, 2, 3, 4}, 1}, {9, 64, {2, 2, 2, 2, 3, 4, 4, 6, 11}, 978}, },
			{ {0}, {0}, {0}, {0}, {10, 16, {1, 1, 1, 1, 2, 2, 2, 2, 4, 8}, 1}, {10, 27, {1, 1, 1, 1, 2, 2, 2, 2, 4, 8}, 1}, {10, 48, {1, 1, 1, 1, 2, 2, 2, 2, 4, 8}, 1}, {10, 64, {1, 1, 2, 2, 2, 2, 4, 4, 4, 8}, 16}, },
			{ {0}, {0}, {0}, {0}, {11, 12, {1, 1, 1, 2, 2, 2, 2, 2, 4, 5, 8}, 1}, {11, 20, {1, 1, 1, 2, 2, 2, 2, 2, 4, 5, 8}, 1}, {11, 32, {1, 1, 1, 2, 2, 2, 2, 4, 4, 5, 8}, 2}, {11, 56, {1, 1, 1, 2, 2, 2, 2, 2, 4, 5, 8}, 1}, },
			{ {0}, {0}, {0}, {0}, {0}, {12, 15, {1, 1, 2, 2, 2, 2, 2, 3, 4, 4, 8, 11}, 2}, {12, 25, {1, 1, 2, 2, 2, 2, 2, 2, 4, 4, 6, 11}, 1}, {12, 40, {1, 1, 2, 2, 2, 2, 2, 2, 4, 4, 6, 11}, 1}, },
		},
	};
	static const size_t permutation_tuple_counts[] = { 128, 512, 2048, 4096, 7000 };
	switch (params->method) {
	case blend_attribute_compression_unit_cube_sampling: {
		// We want at least one byte for weights
		if (params->vertex_size * 8 <= tuple_index_bit_count)
			params->vertex_size = (tuple_index_bit_count + 15) / 8;
		uint32_t total_weight_bit_count = params->vertex_size * 8 - tuple_index_bit_count;
		params->weight_base_bit_count = total_weight_bit_count / (params->max_bone_count - 1);
		if (params->weight_base_bit_count < 2) params->weight_base_bit_count = 2;
		if (params->weight_base_bit_count > 23) params->weight_base_bit_count = 23;
		uint32_t total_bit_count = params->weight_base_bit_count * (params->max_bone_count - 1) + tuple_index_bit_count;
		params->vertex_size = (total_bit_count + 7) / 8;
		params->tuple_index_bit_count = tuple_index_bit_count;
		params->max_tuple_count = (1 << tuple_index_bit_count);
		break;
	}
	case blend_attribute_compression_power_of_two_aabb: {
		// We want at least one byte for weights
		if (params->vertex_size * 8 <= tuple_index_bit_count)
			params->vertex_size = (tuple_index_bit_count + 15) / 8;
		uint32_t total_weight_bit_count = params->vertex_size * 8 - tuple_index_bit_count;
		uint32_t saved_bit_count = 0;
		for (uint32_t i = 0; i != params->max_bone_count - 1; ++i)
			saved_bit_count += power_of_two_weight_savings[i];
		params->weight_base_bit_count = (total_weight_bit_count + saved_bit_count) / (params->max_bone_count - 1);
		if (params->weight_base_bit_count < 2) params->weight_base_bit_count = 2;
		if (params->weight_base_bit_count > 22) params->weight_base_bit_count = 22;
		uint32_t total_bit_count = params->weight_base_bit_count * (params->max_bone_count - 1) - saved_bit_count + tuple_index_bit_count;
		params->vertex_size = (total_bit_count + 7) / 8;
		params->tuple_index_bit_count = tuple_index_bit_count;
		params->max_tuple_count = (1 << tuple_index_bit_count);
		break;
	}
	case blend_attribute_compression_optimal_simplex_sampling_19:
	case blend_attribute_compression_optimal_simplex_sampling_22:
	case blend_attribute_compression_optimal_simplex_sampling_35:
		params->max_bone_count = 4;
		params->vertex_size = (get_optimal_simplex_sampling_bit_count(params->method) + tuple_index_bit_count + 7) / 8;
		params->tuple_index_bit_count = tuple_index_bit_count;
		params->max_tuple_count = (1 << tuple_index_bit_count);
		break;
	case blend_attribute_compression_permutation_coding:
		if (params->vertex_size > 8) params->vertex_size = 8;
		// Find a matching tuple count
		uint32_t tuple_count_index = 0xffffffff;
		for (uint32_t i = 0; i != sizeof(permutation_tuple_counts) / sizeof(permutation_tuple_counts[0]); ++i) {
			if (permutation_tuple_counts[i] >= params->max_tuple_count) {
				params->max_tuple_count = permutation_tuple_counts[i];
				tuple_count_index = i;
				break;
			}
		}
		if (tuple_count_index == 0xffffffff) {
			// Last resort: Use the ground truth. For the scenes shipping with
			// this demo, this never happens and it can be avoided by running
			// optimal_coding.py for still greater tuple index counts.
			params->method = blend_attribute_compression_none;
			params->vertex_size = params->max_bone_count * (sizeof(float) + sizeof(uint16_t));
			break;
		}
		// Adjust the vertex size to get a meaningful parameter set
		while (permutation_codecs[tuple_count_index][params->max_bone_count - 2][params->vertex_size - 1].entry_count == 0 && params->vertex_size < 8)
			++params->vertex_size;
		while (permutation_codecs[tuple_count_index][params->max_bone_count - 2][params->vertex_size - 1].entry_count == 0)
			--params->vertex_size;
		params->permutation_coding = permutation_codecs[tuple_count_index][params->max_bone_count - 2][params->vertex_size - 1];
		break;
	case blend_attribute_compression_none:
		params->vertex_size = params->max_bone_count * (sizeof(float) + sizeof(uint16_t));
		break;
	default:
		params->method = blend_attribute_compression_none;
		params->vertex_size = params->max_bone_count * (sizeof(float) + sizeof(uint16_t));
		break;
	}
}


int reduce_bone_count(
	uint16_t* out_indices, size_t out_index_stride, float* out_weights, size_t out_weight_stride,
	const uint16_t* indices, size_t index_stride, const float* weights, size_t weight_stride,
	uint32_t out_max_bone_count, uint32_t max_bone_count, uint64_t vertex_count, int write_last_weight)
{
	if (out_max_bone_count > max_bone_count || out_max_bone_count < 2 || max_bone_count > SUPPORTED_BONE_COUNT)
		return 1;
	index_weight_pair_t pairs[SUPPORTED_BONE_COUNT];
	uint32_t new_begin = max_bone_count - out_max_bone_count;
	uint32_t skip_last_weight = (write_last_weight ? 0 : 1);
	for (uint64_t i = 0; i != vertex_count; ++i) {
		get_sorted_pairs(pairs, indices, index_stride, weights, weight_stride, max_bone_count, i);
		// Renormalize
		float weight_sum = 0.0f;
		for (uint32_t j = 0; j != out_max_bone_count; ++j)
			weight_sum += pairs[new_begin + j].weight;
		float factor = 1.0f / weight_sum;
		// Write out the result
		uint16_t* vertex_indices = (uint16_t*) (((char*) out_indices) + i * out_index_stride);
		for (uint32_t j = 0; j != out_max_bone_count; ++j)
			vertex_indices[j] = pairs[new_begin + j].index;
		float* vertex_weights = (float*) (((char*) out_weights) + i * out_weight_stride);
		for (uint32_t j = 0; j + skip_last_weight < out_max_bone_count; ++j)
			vertex_weights[j] = pairs[new_begin + j].weight * factor;
	}
	return 0;
}


int compress_blend_attribute_buffers(
	uint16_t* out_table, uint64_t* out_table_size, void* out_compressed, size_t compressed_stride,
	const uint16_t* indices, size_t index_stride, const float* weights, size_t weight_stride,
	const blend_attribute_compression_parameters_t* params, uint64_t vertex_count, uint64_t max_table_size)
{
	if (params->method == blend_attribute_compression_none || params->max_bone_count < 2 || params->max_bone_count > SUPPORTED_BONE_COUNT)
		return 1;
	uint32_t max_bone_count = params->max_bone_count;
	// Construct a list of all tuple indices (after sorting by weight) where
	// zero weights (after compression) have 0xffff as index
	const size_t tuple_size = sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) * max_bone_count;
	const size_t word_stride = tuple_size / sizeof(uint16_t);
	const uint16_t irrelevant = 0xffff;
	uint16_t* long_table = malloc(tuple_size * vertex_count);
	index_weight_pair_t pairs[SUPPORTED_BONE_COUNT];
	for (uint64_t i = 0; i != vertex_count; ++i) {
		get_sorted_pairs(pairs, indices, index_stride, weights, weight_stride, max_bone_count, i);
		uint32_t irrelevant_mask = flag_zero_compressed_weights(pairs, params);
		// Each tuple additionally stores the corresponding vertex index and
		// max_bone_count (to work around limitations of qsort() without
		// violating thread safety or using C11's qsort_s())
		uint16_t* tuple = long_table + i * word_stride;
		uint32_t* vertex_index = (uint32_t*) tuple;
		(*vertex_index) = (uint32_t) i;
		tuple[2] = (uint16_t) max_bone_count;
		tuple += 3;
		for (uint32_t j = 0; j != max_bone_count; ++j)
			tuple[j] = ((irrelevant_mask & (1 << j)) != 0) ? irrelevant : pairs[j].index;
		// Sort the tuple (does not work but it is interesting to see the
		// impact on the table size)
		//qsort(tuple, max_bone_count, sizeof(uint16_t), (int (*)(const void*, const void*)) compare_indices);
	}
	// Sort the tuple indices lexicographically taking the index for the
	// largest weight to be most significant
	qsort(long_table, vertex_count, tuple_size, (int (*)(const void*, const void*)) compare_index_tuples);
	// Now identify runs of compatible tuples and write the first
	// representative (which is the longest one due to the sorting) to the
	// output table. Remember the used table entry for each vertex.
	uint32_t* vertex_tuple_index = malloc(sizeof(uint32_t) * vertex_count);
	uint8_t representative_bone_count = 0;
	uint16_t representative[SUPPORTED_BONE_COUNT];
	uint32_t table_size = 0;
	for (uint64_t i = 0; i != vertex_count; ++i) {
		const uint16_t* tuple = long_table + i * word_stride;
		const uint32_t* vertex_index = (uint32_t*) tuple;
		tuple += 3;
		// If only a single weight is non-zero after compression, the tuple
		// index is repurposed as bone index
		int singleton = 1;
		for (uint32_t j = 0; j != max_bone_count - 1; ++j)
			singleton &= (tuple[j] == irrelevant);
		if (singleton) {
			vertex_tuple_index[*vertex_index] = tuple[max_bone_count - 1];
			continue;
		}
		// Check if all relevant entries of the current tuple match the newest
		// representative
		int matching_suffix = 1;
		for (uint32_t j = 0; j != max_bone_count; ++j)
			matching_suffix &= (representative[j] == tuple[j] || tuple[j] == irrelevant);
		if (!matching_suffix) {
			// Remember the new representative and write it to the table
			for (uint32_t j = 0; j != max_bone_count; ++j)
				representative[j] = tuple[j];
			if (out_table && table_size < max_table_size) {
				uint16_t* dest = out_table + table_size * max_bone_count;
				for (uint32_t j = 0; j != max_bone_count; ++j)
					dest[j] = representative[j];
			}
			++table_size;
		}
		vertex_tuple_index[*vertex_index] = table_size - 1;
	}
	free(long_table);
	// Write compressed vertex data
	if (out_compressed) {
		for (uint64_t i = 0; i != vertex_count; ++i) {
			get_sorted_pairs(pairs, indices, index_stride, weights, weight_stride, max_bone_count, i);
			compress_vertex_blend_attributes(((char*) out_compressed) + i * compressed_stride, pairs, vertex_tuple_index[i], params);
		}
	}
	free(vertex_tuple_index);
	if (out_table_size)
		(*out_table_size) = table_size;
	return table_size > max_table_size;
}
