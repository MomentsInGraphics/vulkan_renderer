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


#pragma once
#include "vulkan_basics.h"

/*! Gathers constant values that are needed to make use of a table with
	linearly transformed cosine coefficients in a shader. Includes padding for
	direct copy into a constant buffer.*/
typedef struct ltc_constants_s {
	/*! This linear transform (a*x+b) turns a Fresnel F0 coefficient between
		zero and one into an index for a texture in the LTC texture arrays.
		Round to nearest once you have the floating point index.*/
	float fresnel_index_factor, fresnel_index_summand;
	/*! This linear transform (a*x+b) turns a roughness between zero and one
		into a texture coordinate for the LTC table.*/
	float roughness_factor, roughness_summand;
	/*! This linear transform (a*x+b) turns an inclination between zero and
		0.5f*M_PI_F into a texture coordinate for the LTC texture array.*/
	float inclination_factor, inclination_summand;
	float padding[2];
} ltc_constants_t;


/*! This struct provides a pair of texture arrays and a sampler object
	providing access to lookup tables for coefficients of linearly transformed
	cosines.*/
typedef struct ltc_table_s {
	/*! The number of different roughness values (from 0 to 1), inclinations
		(from 0 to 0.5*M_PI_F) and Fresnel F0 coefficients (from 0 to 1). All
		quantities are sampled equidistantly and there are samples at both
		endpoints.*/
	uint32_t roughness_count, inclination_count, fresnel_count;
	/*! Two texture arrays with 16-bit fixed-point matrix entries (UNORM).
		For the first texture, R is entry 0,0, G is 2,0 (with flipped sign), B
		is 1,1 and A is 0,2. For the second texture, R is entry 2,2 and G is
		the albedo. The number of textures is fresnel_count, the width is
		roughness_count, the height is inclination_count.*/
	images_t texture_arrays;
	//! A sampler with bilinear interpolation and no wrapping.
	VkSampler sampler;
	//! Constants that are needed to use the table in a shader
	ltc_constants_t constants;
} ltc_table_t;


/*! Loads the specified linearly transformed cosine tables into device-local
	texture arrays and prepares sampling.
	\param table The output object. Use destroy_ltc_table() for cleanup.
	\param device Device used for texture creation.
	\param directory A directory with one precomputed LTC table per Fresnel F0
		coefficient.
	\param fresnel_count The number of tables for different Fresnel F0
		coefficients.
	\return 0 on success.*/
int load_ltc_table(ltc_table_t* table, const device_t* device, const char* directory, uint32_t fresnel_count);

//! Frees and zeros the given object
void destroy_ltc_table(ltc_table_t* table, const device_t* device);
