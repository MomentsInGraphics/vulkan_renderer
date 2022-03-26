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


#pragma once
#include "blend_attribute_codec.h"
#include <stdint.h>
#include <stdlib.h>

//! The available methods for compression of blend attributes
typedef enum blend_attribute_compression_method_e {
	//! Blend attributes are fed to the shader as uint16_t and float
	blend_attribute_compression_none,
	/*! The most naive quantization approach: Each weight except for one is
		stored with the same number of bits using common fixed-point
		quantization. The tuple table is used nonetheless.*/
	blend_attribute_compression_unit_cube_sampling,
	/*! The simplex of sorted weights is enclosed by an axis-aligned bounding
		box and coordinates in this box are stored with a number of bits that
		depends on the extent along this axis. Supports any number of bones.
		Proposed in: Bastian Kuth, Quirin Meyer 2021, Vertex-Blend Attribute
		Compression, High-Performance Graphics - Symposium Papers,
		https://doi.org/10.2312/hpg.20211282 */
	blend_attribute_compression_power_of_two_aabb,
	/*! Method for vertices with up to four bone influences using 19 bits for
		weights and additional bits for the tuple index. All coordinates within
		the suitable simplex are numerated and this number is used as code. The
		implementation used here is the one published with the original paper:
		Bastian Kuth, Quirin Meyer 2021, Vertex-Blend Attribute Compression,
		High-Performance Graphics - Symposium Papers,
		https://doi.org/10.2312/hpg.20211282 */
	blend_attribute_compression_optimal_simplex_sampling_19,
	//! Like blend_attribute_compression_optimal_simplex_sampling_19 but uses
	//! 22 bits for the weights
	blend_attribute_compression_optimal_simplex_sampling_22,
	//! Like blend_attribute_compression_optimal_simplex_sampling_19 but uses
	//! 35 bits for the weights
	blend_attribute_compression_optimal_simplex_sampling_35,
	//! Our permutation coding. Supports up to 13 bone influences per vertex
	//! stored in up to 64 bits per vertex.
	blend_attribute_compression_permutation_coding,
	//! Number of entries in this enumeration
	blend_attribute_compression_count,
} blend_attribute_compression_method_t;


//! Gathers parameters defining how vertex blend attributes are compressed
//! within vertex buffers
typedef struct blend_attribute_compression_parameters_s {
	//! The used compression method
	blend_attribute_compression_method_t method;
	//! The maximal supported number of bone influences per vertex
	uint32_t max_bone_count;
	//! The number of different tuple indices that are supported (the actual
	//! supported number may be greater)
	uint32_t max_tuple_count;
	//! The size of vertex blend attributes per vertex in bytes
	size_t vertex_size;
	/*! In methods that store individual weights in separate bit ranges, this
		is the number of bits used for the largest stored weight. Power-of-two
		AABB reduces this number per weight. */
	uint32_t weight_base_bit_count;
	//! In techniques that store the tuple index explicitly, this is the number
	//! of bits dedicated to it
	uint32_t tuple_index_bit_count;
	//! Parameters for permutation coding (if it is used)
	blend_attribute_codec_t permutation_coding;
} blend_attribute_compression_parameters_t;


/*! Given a parameter set where only method, max_bone_count, max_tuple_count
	and verte_size are set, this method fills in the other parameters
	accordingly and modifies the given parameters as necessary for success. The
	goal is to make a minimal change that results in a supported parameter set.
	The method is only touched if max_tuple_count is too big otherwise.*/
void complete_blend_attribute_compression_parameters(blend_attribute_compression_parameters_t* params);


/*! Reduces the number of bones influencing each vertex. To do so, it discards
	the influences with the smallest weights and renormalizes the remaining
	weights to sum to one. Weights become sorted. All strides are in bytes.
	\param out_indices Pointer to the out_max_bone_count remaining indices for
		vertex 0. Can be equal to indices if out_index_stride <= index_stride.
	\param out_index_stride Stride between output indices for two vertices.
	\param out_weights Pointer to the out_max_bone_count - 1 (or
		out_max_bone_count if write_last_weight is 1) remaining weights for
		vertex 0.
	\param out_weight_stride Stride between output weights for two vertices.
	\param indices Pointer to the max_bone_count bone indices for vertex 0.
	\param index_stride Stride between indices for two vertices.
	\param weights Pointer to the max_bone_count -1 weights for vertex 0. The
		last weight is inferred from the fact that weights sum to one. The
		order is arbitrary but must be consistent with indices.
	\param weight_stride Stride between weights for two vertices. 
	\param out_max_bone_count The number of bone influences per vertex to
		retain. At most max_bone_count.
	\param max_bone_count The number of available bone influences per vertex.
	\param vertex_count The number of vertices to process.
	\param write_last_weight Whether the smallest weight should be written to
		the output explicitly (1) or is inferred by the calling side via the
		requirement that weights sum to 1.0 (0).
	\return 0 on success.*/
int reduce_bone_count(
	uint16_t* out_indices, size_t out_index_stride, float* out_weights, size_t out_weight_stride,
	const uint16_t* indices, size_t index_stride, const float* weights, size_t weight_stride,
	uint32_t out_max_bone_count, uint32_t max_bone_count, uint64_t vertex_count, int write_last_weight
);


/*! Compresses buffers of blend attributes. All strides are in bytes.
	\param out_table Pointer to the tuple index table. Unique tuples of
		params->max_bone_count bone indices are written into this table. Can be
		NULL if you only want to determine out_table_size.
	\param out_table_size Set to the number of tuples written to out_table. If
		this exceeds max_table_size, writing stops but the required size is
		still determined correctly.
	\param out_compressed Pointer to an array of size compressed_stride *
		vertex_count to which the compressed blend attributes for each vertex
		are written. Can be NULL if you only want to determine out_table_size.
	\param compressed_stride Stride between entries for two vertices that are
		written to compressed. At least params->vertex_size.
	\param indices Pointer to the params->max_bone_count bone indices for
		vertex 0.
	\param index_stride Stride between indices for two vertices.
	\param weights Pointer to the params->max_bone_count - 1 weights for vertex
		0. The last weight is inferred from the fact that weights sum to one.
		The order is arbitrary but must be consistent with indices. Weights
		must not be negative (even the last weight).
	\param weight_stride Stride between weights for two vertices. 
	\param params A description of the used compression method (must not be
		none).
	\param vertex_count Number of vertices to process.
	\param max_table_size The number of tuples which fit into table.
	\return 0 upon success. Failure occurs e.g. if (*table_size) >
		max_table_size.*/
int compress_blend_attribute_buffers(
	uint16_t* out_table, uint64_t* out_table_size, void* out_compressed, size_t compressed_stride,
	const uint16_t* indices, size_t index_stride, const float* weights, size_t weight_stride,
	const blend_attribute_compression_parameters_t* params, uint64_t vertex_count, uint64_t max_table_size
);
