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


#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes : enable
#include "mesh_quantization.glsl"
#include "shared_constants.glsl"

#if BLEND_ATTRIBUTE_COMPRESSION_OPTIMAL_SIMPLEX_SAMPLING_19
#include "optimal_simplex_sampling_19.glsl"
#elif BLEND_ATTRIBUTE_COMPRESSION_OPTIMAL_SIMPLEX_SAMPLING_22
#include "optimal_simplex_sampling_22.glsl"
#elif BLEND_ATTRIBUTE_COMPRESSION_OPTIMAL_SIMPLEX_SAMPLING_35
#include "optimal_simplex_sampling_35.glsl"
#elif BLEND_ATTRIBUTE_COMPRESSION_PERMUTATION_CODING
#include "blend_attribute_compression.glsl"
#endif


//! Texture holding transforms for all bones at all times
layout (binding = 2) uniform usampler2D g_animation_texture;
//! Maps triangle indices to material indices
layout (binding = 3) uniform utextureBuffer g_material_indices;
//! At index i * MAX_BONE_COUNT + j, this buffer holds the j-th bone index for
//! the i-th tuple
layout (binding = 4) uniform utextureBuffer g_bone_index_table;

//! All vertex data is fed using a bunch of uvec vertex attributes.
//! MAKE_vertex_data_ARRAY turns that into an array of uints, each (except the
//! last) holding 32 bits.
DECLARE_vertex_data

#if ERROR_DISPLAY_NONE
//! The world space vertex position
layout (location = 0) out vec3 g_out_vertex_position;
//! The world space vertex normal
layout (location = 1) out vec3 g_out_vertex_normal;
//! The texture coordinate (used for the diffuse albedo map)
layout (location = 2) out vec2 g_out_tex_coord;
//! The index of the used material
layout (location = 3) out uint g_out_material_index;

#elif ERROR_DISPLAY_POSITIONS_LOGARITHMIC
//! The difference vector between the world space position computed with either
//! ground truth blend weights or decompressed blend weights
layout (location = 0) out vec3 g_out_vertex_position_error_world_space;

#endif

/*! This function extracts up to 32 bits from an array.
	\param dwords Array of 32-bit unsigned integers providing some binary data
	\param offset The first bit to extract. Subject to 
		offset + bit_count <= ((COMPRESSED_SIZE + 3) / 4) * 8.
	\param bit_count The number of bits to extract. At most 32.
	\return The least significant bit_count bits provide the requested data.*/
uint long_bitfield_extract(uint dwords[(COMPRESSED_SIZE + 3) / 4], uint offset, uint bit_count) {
	// If offset and bit_count are compile time constants, almost everything in
	// this function is free
	uint start_dword = offset >> 5;
	uint start_offset = offset & 0x1f;
	if (start_offset + bit_count <= 32)
		return bitfieldExtract(dwords[start_dword], int(start_offset), int(bit_count));
	else {
		// Assemble the result based on two consecutive dwords
		uint start_bit_count = 32 - start_offset;
		return bitfieldExtract(dwords[start_dword], int(start_offset), int(start_bit_count))
			+ (bitfieldExtract(dwords[start_dword + 1], 0, int(bit_count - start_bit_count)) << start_bit_count);
	}
}


//! Describes a bone transformation dequantized from the animation texture
struct bone_transform_t {
	//! A unit quaternion for the rotation. The last entry is the real part.
	vec4 quaternion;
	//! The translation. Applied last.
	vec3 translation;
	//! An isotropic scaling factor
	float scaling;
};


//! Loads and dequantizes the object to world space transform of the bone with
//! the given index in the currently displayed pose
bone_transform_t load_bone_transform(uint bone_index) {
	// Access the animation texture
	vec2 tex_coord = vec2(
		float(bone_index) * g_inv_bone_count + g_animation_half_column_spacing,
		g_time_tex_coord
	);
	uvec4 data_0 = textureLod(g_animation_texture, tex_coord, 0.0f);
	uvec4 data_1 = textureLod(g_animation_texture, tex_coord + vec2(g_animation_column_spacing, 0.0f), 0.0f);
	// Dequantize from 16-bit uint to float
	uint data[8] = { data_0[0], data_0[1], data_0[2], data_0[3], data_1[0], data_1[1], data_1[2], data_1[3] };
	float dequantized[8];
	[[unroll]]
	for (uint i = 0; i != 8; ++i)
		dequantized[i] = float(data[i]) * g_animation_dequantization[i/4][i%4] + g_animation_dequantization[i/4+2][i%4];
	// Construct the transformation
	bone_transform_t result;
	result.quaternion = vec4(dequantized[0], dequantized[1], dequantized[2], dequantized[3]);
	result.translation = vec3(dequantized[4], dequantized[5], dequantized[6]);
	result.scaling = exp2(dequantized[7]);
	return result;
}


//! Applies the rotational part of a bone transformation to the given vector
vec3 apply_rotation(bone_transform_t transform, vec3 direction) {
	// Formula courtesy of Fabian Giessen:
	// https://fgiesen.wordpress.com/2019/02/09/rotating-a-single-vector-using-a-quaternion/
	vec3 temp = 2.0f * cross(transform.quaternion.xyz, direction);
	return direction + transform.quaternion.w * temp + cross(transform.quaternion.xyz, temp);
}


//! Applies the given bone transform to the given position in its entirety
vec3 apply_transform(bone_transform_t transform, vec3 position) {
	return transform.scaling * apply_rotation(transform, position) + transform.translation;
}


//! Writes ground truth bone weights and indices into the given arrays. They
//! are fed into the shader as floats and 16-bit uints. If they are not
//! available, bogus values are written.
void get_ground_truth_blend_attributes(out uint out_bone_indices[MAX_BONE_COUNT], out float out_bone_weights[MAX_BONE_COUNT]) {
	// Copy inputs from global vertex attributes to arrays
	MAKE_vertex_data_ARRAY  // uint vertex_data[];
	[[unroll]]
	for (uint i = 0; i != MAX_BONE_COUNT; ++i) {
#if GROUND_TRUTH_AVAILABLE
		out_bone_indices[i] = bitfieldExtract(vertex_data[4 + MAX_BONE_COUNT + i / 2], int((i % 2) * 16), 16);
		out_bone_weights[i] = uintBitsToFloat(vertex_data[4 + i]);
#else
		out_bone_indices[i] = 0;
		out_bone_weights[i] = (i == MAX_BONE_COUNT - 1) ? 1.0f : 0.0f;
#endif
	}
}


//! Writes decompressed bone weights and indices into the given arrays. If no
//! compressed data is available, ground truth data is written.
void get_decompressed_blend_attributes(out uint out_bone_indices[MAX_BONE_COUNT], out float out_bone_weights[MAX_BONE_COUNT]) {
	// Copy inputs from global vertex attributes to arrays
	MAKE_vertex_data_ARRAY  // uint vertex_data[];
	uint bones_compressed[(COMPRESSED_SIZE + 3) / 4];
	[[unroll]]
	for (uint i = 0; i != (COMPRESSED_SIZE + 3) / 4; ++i)
		bones_compressed[i] = vertex_data[i + COMPRESSED_OFFSET];

#if BLEND_ATTRIBUTE_COMPRESSION_NONE
	get_ground_truth_blend_attributes(out_bone_indices, out_bone_weights);

#elif BLEND_ATTRIBUTE_COMPRESSION_UNIT_CUBE_SAMPLING
	float largest_weight = 1.0f;
	[[unroll]]
	for (uint i = 0; i != MAX_BONE_COUNT - 1; ++i) {
		out_bone_weights[i] = float(long_bitfield_extract(bones_compressed, i * WEIGHT_BASE_BIT_COUNT, WEIGHT_BASE_BIT_COUNT)) * (1.0f / (pow(2.0f, WEIGHT_BASE_BIT_COUNT) - 1));
		largest_weight -= out_bone_weights[i];
	}
	out_bone_weights[MAX_BONE_COUNT - 1] = largest_weight;
	uint tuple_index = long_bitfield_extract(bones_compressed, (MAX_BONE_COUNT - 1) * WEIGHT_BASE_BIT_COUNT, TUPLE_INDEX_BIT_COUNT);

#elif BLEND_ATTRIBUTE_COMPRESSION_POWER_OF_TWO_AABB
	// How many bits can be saved per weight, starting with the second largest
	// one. Denominators are:       2  3  4  5  6  7  8  9 10 11 12 13
	const uint weight_savings[] = { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2 };
	uint next_bit = 0;
	float largest_weight = 1.0f;
	[[unroll]]
	for (uint i = 0; i != MAX_BONE_COUNT - 1; ++i) {
		uint bit_count = WEIGHT_BASE_BIT_COUNT - weight_savings[i];
		out_bone_weights[MAX_BONE_COUNT - 2 - i] = float(long_bitfield_extract(bones_compressed, next_bit, bit_count)) * (0.5f / (pow(2.0f, WEIGHT_BASE_BIT_COUNT) - 1));
		largest_weight -= out_bone_weights[MAX_BONE_COUNT - 2 - i];
		next_bit += bit_count;
	}
	out_bone_weights[MAX_BONE_COUNT - 1] = largest_weight;
	uint tuple_index = long_bitfield_extract(bones_compressed, next_bit, TUPLE_INDEX_BIT_COUNT);

#elif BLEND_ATTRIBUTE_COMPRESSION_OPTIMAL_SIMPLEX_SAMPLING_19
	vec4 weights = decompress_optimal_simplex_sampling(bitfieldExtract(bones_compressed[0], 0, 19));
	out_bone_weights[0] = weights[3];
	out_bone_weights[1] = weights[2];
	out_bone_weights[2] = weights[1];
	out_bone_weights[3] = weights[0];
	uint tuple_index = long_bitfield_extract(bones_compressed, 19, TUPLE_INDEX_BIT_COUNT);

#elif BLEND_ATTRIBUTE_COMPRESSION_OPTIMAL_SIMPLEX_SAMPLING_22
	vec4 weights = decompress_optimal_simplex_sampling(bitfieldExtract(bones_compressed[0], 0, 22));
	out_bone_weights[0] = weights[3];
	out_bone_weights[1] = weights[2];
	out_bone_weights[2] = weights[1];
	out_bone_weights[3] = weights[0];
	uint tuple_index = long_bitfield_extract(bones_compressed, 22, TUPLE_INDEX_BIT_COUNT);

#elif BLEND_ATTRIBUTE_COMPRESSION_OPTIMAL_SIMPLEX_SAMPLING_35
	vec4 weights = decompress_optimal_simplex_sampling(uvec2(bitfieldExtract(bones_compressed[1], 0, 3), bones_compressed[0]));
	out_bone_weights[0] = weights[3];
	out_bone_weights[1] = weights[2];
	out_bone_weights[2] = weights[1];
	out_bone_weights[3] = weights[0];
	uint tuple_index = long_bitfield_extract(bones_compressed, 35, TUPLE_INDEX_BIT_COUNT);

#elif BLEND_ATTRIBUTE_COMPRESSION_PERMUTATION_CODING
	blend_attribute_codec_t codec = PERMUTATION_CODEC;
	bool valid;
#if COMPRESSED_SIZE <= 4
	uvec2 code = uvec2(bones_compressed[0], 0);
#else
	uvec2 code = uvec2(bones_compressed[0], bones_compressed[1]);
#endif
	// See blend_attribute_compression.glsl
	uint tuple_index = decompress_blend_attributes(out_bone_weights, valid, code, codec);
#endif

	// Getting the bone indices based on the tuple index works the same for all
	// compression techniques
#if BLEND_ATTRIBUTE_COMPRESSION_NONE == 0
	// If only a single bone is used, the tuple index is the bone index
	if (out_bone_weights[MAX_BONE_COUNT - 1] >= 1.0f)
		out_bone_indices[MAX_BONE_COUNT - 1] = tuple_index;
	// Otherwise, we access the table, which holds vectors of TUPLE_VECTOR_SIZE
	// entries (largest of 1, 2 or 4, which divides MAX_BONE_COUNT)
	else {
		[[unroll]]
		for (uint i = 0; i != MAX_BONE_COUNT / TUPLE_VECTOR_SIZE; ++i) {
			bool weight_non_zero = false;
			[[unroll]]
			for (uint j = 0; j != TUPLE_VECTOR_SIZE; ++j)
				weight_non_zero = (weight_non_zero || out_bone_weights[i * TUPLE_VECTOR_SIZE + j] > 0.0f);
			if (weight_non_zero) {
				uvec4 indices = texelFetch(g_bone_index_table, int(tuple_index * (MAX_BONE_COUNT / TUPLE_VECTOR_SIZE) + i));
				[[unroll]]
				for (uint j = 0; j != TUPLE_VECTOR_SIZE; ++j)
					out_bone_indices[TUPLE_VECTOR_SIZE * i + j] = indices[j];
			}
		}
	}
#endif
}


//! Computes the world space position and normal based on the given bone
//! weights and indices (and the globally defined object space attributes)
void apply_vertex_blending(out vec3 out_position_world_space, out vec3 out_normal_world_space, uint bone_indices[MAX_BONE_COUNT], float bone_weights[MAX_BONE_COUNT]) {
	MAKE_vertex_data_ARRAY // uint vertex_data[];
	// Dequantize the vertex position and the normal
	vec3 position_object_space = decode_position_64_bit(uvec2(vertex_data[0], vertex_data[1]), g_mesh_dequantization_factor, g_mesh_dequantization_summand);
	vec2 octahedral = vec2(bitfieldExtract(vertex_data[2], 0, 16) * (1.0f / 0xffff), bitfieldExtract(vertex_data[2], 16, 16) * (1.0f / 0xffff));
	vec3 normal_object_space = decode_normal_32_bit(octahedral);
	// Apply skinning
	out_position_world_space = out_normal_world_space = vec3(0.0f);
	[[unroll]]
	for (uint i = 0; i != MAX_BONE_COUNT; ++i) {
		if (bone_weights[i] > 0.0f) {
			bone_transform_t transform = load_bone_transform(bone_indices[i]);
			out_position_world_space += bone_weights[i] * apply_transform(transform, position_object_space);
			out_normal_world_space += bone_weights[i] * apply_rotation(transform, normal_object_space);
		}
	}
	// Different instances get different offsets
	out_position_world_space.y += gl_InstanceIndex * 10.0f;
}


void main() {
#if ERROR_DISPLAY_NONE
	MAKE_vertex_data_ARRAY // uint vertex_data[];
	// Decompress bone weights and indices
	uint bone_indices[MAX_BONE_COUNT];
	float bone_weights[MAX_BONE_COUNT];
	get_decompressed_blend_attributes(bone_indices, bone_weights);
	// Compute the position and normal using blending
	apply_vertex_blending(g_out_vertex_position, g_out_vertex_normal, bone_indices, bone_weights);
	gl_Position = g_world_to_projection_space * vec4(g_out_vertex_position, 1.0f);
	// Forward additional attributes
	g_out_tex_coord = vec2(bitfieldExtract(vertex_data[3], 0, 16) * (8.0f / 0xffff), bitfieldExtract(vertex_data[3], 16, 16) * (-8.0f / 0xffff));
	g_out_material_index = texelFetch(g_material_indices, gl_VertexIndex / 3).r;

#elif ERROR_DISPLAY_POSITIONS_LOGARITHMIC
	// Use ground truth and decompression to obtain bone weights and indices
	uint ground_truth_bone_indices[MAX_BONE_COUNT], decompressed_bone_indices[MAX_BONE_COUNT];
	float ground_truth_bone_weights[MAX_BONE_COUNT], decompressed_bone_weights[MAX_BONE_COUNT];
	get_ground_truth_blend_attributes(ground_truth_bone_indices, ground_truth_bone_weights);
	get_decompressed_blend_attributes(decompressed_bone_indices, decompressed_bone_weights);
	// Use both to compute the world space vertex position
	vec3 ground_truth_vertex_position, decompressed_vertex_position;
	vec3 normal;
	apply_vertex_blending(ground_truth_vertex_position, normal, ground_truth_bone_indices, ground_truth_bone_weights);
	apply_vertex_blending(decompressed_vertex_position, normal, decompressed_bone_indices, decompressed_bone_weights);
	// Let the fragment shader visualize the difference
	g_out_vertex_position_error_world_space = decompressed_vertex_position - ground_truth_vertex_position;
	// We still need to output the position
	gl_Position = g_world_to_projection_space * vec4(ground_truth_vertex_position, 1.0f);
#endif
}
