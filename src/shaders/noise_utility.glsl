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


#extension GL_EXT_samplerless_texture_functions : enable

//! This structure holds all information needed to retrieve a large number of
//! noise values
struct noise_accessor_t {
	//! The first available_noise_count entries of this vector are the noise
	//! values that will be returned next
	vec4 noise;
	//! The number of scalar noise values that are readily available. If there
	//! are not enough, a texture read is necessary.
	uint available_noise_count;
	//! The index of the pixel on screen, used to compute the lookup location
	//! in noise textures
	uvec2 pixel;
	//! The next index to use to access a noise texture
	uint sample_index;
	//! Resolution of textures in g_noise_table, which must be a power of two,
	//! minus one
	uvec2 resolution_mask;
	//! Number of textures in g_noise_table, which must be a power of two, minus
	//! one
	uint texture_index_mask;
	//! A bunch of random bits used to randomize results across frames
	uvec4 random_numbers;
};

//! A texture array with precomputed RGBA noise textures. Various types of
//! noise are, e.g. blue noise dither arrays. See noise_type_t.
layout (binding = 6) uniform texture2DArray g_noise_table;


/*! This function retrieves four noise values for a pixel from g_noise_table.
	\param pixel The integer screen space location of the pixel.
	\param sample_index Passing different values yields independent values up
		to the point where all available noise has been used.
	\param resolution_mask Resolution of the textures in g_noise_table, minus
		one. The resolution is supposed to be a power of two, such that binary
		and implements wrapping.
	\param texture_index_mask Number of textures in g_noise_table, minus one.
		The count is also supposed to be a power of two.
	\param noise_random_numbers Uniforms used to offer randomization across
		frames.
  \note The values are not independent across pixels. Values within a returned
  	vector may not be independent dependent on the choice of noise. In fact,
	some noise types are completely deterministic but still have good
	uniformity properties.*/
vec4 get_noise_sample(uvec2 pixel, uint sample_index, uvec2 resolution_mask, uint texture_index_mask, uvec4 noise_random_numbers) {
	// Grab some random numbers
	uvec4 random_numbers = ((sample_index & 2) != 0) ? noise_random_numbers.zwxy : noise_random_numbers;
	random_numbers.xyz = ((sample_index & 1) != 0) ? random_numbers.yzw : random_numbers.xyz;
	uint shift = (sample_index & 124) >> 2;
	uvec2 texture_offset = random_numbers.xy >> shift;
	uint texture_index = (random_numbers.z + sample_index) & texture_index_mask;
	// Get the noise vector
	uvec2 sample_location = (pixel + texture_offset) & resolution_mask;
	return texelFetch(g_noise_table, ivec3(sample_location, texture_index), 0);
}


//! Returns a noise accessor providing access to the first available noise
//! values in the sequence for the current frame and the given pixel.
//! Parameters forward to get_noise_sample().
noise_accessor_t get_noise_accessor(uvec2 pixel, uvec2 resolution_mask, uint texture_index_mask, uvec4 noise_random_numbers) {
	noise_accessor_t result;
	result.resolution_mask = resolution_mask;
	result.texture_index_mask = texture_index_mask;
	result.random_numbers = noise_random_numbers;
	result.noise = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	result.available_noise_count = 0;
	result.pixel = pixel;
	result.sample_index = 0;
	return result;
}


//! Retrieves the next two noise values and advances the accessor
vec2 get_noise_2(inout noise_accessor_t accessor) {
	if (accessor.available_noise_count <= 1) {
		accessor.noise = get_noise_sample(accessor.pixel, accessor.sample_index, accessor.resolution_mask, accessor.texture_index_mask, accessor.random_numbers);
		accessor.available_noise_count = 4;
		++accessor.sample_index;
	}
	accessor.available_noise_count -= 2;
	vec2 result = accessor.noise.xy;
	accessor.noise.xy = accessor.noise.zw;
	return result;
}


//! Retrieves the next noise value and advances the accessor
float get_noise_1(inout noise_accessor_t accessor) {
	if (accessor.available_noise_count <= 0) {
		accessor.noise = get_noise_sample(accessor.pixel, accessor.sample_index, accessor.resolution_mask, accessor.texture_index_mask, accessor.random_numbers);
		accessor.available_noise_count = 4;
		++accessor.sample_index;
	}
	--accessor.available_noise_count;
	float result = accessor.noise.x;
	accessor.noise.xyz = accessor.noise.yzw;
	return result;
}
