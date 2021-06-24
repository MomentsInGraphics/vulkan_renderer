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
#include <stdint.h>

//! The available types of noise tables
typedef enum noise_type_e {
	//! Each entry in the table is a uniform random number, independent from
	//! all other entries.
	noise_type_white = 0,
	//! Each channel of each image is a blue noise dither array. Different
	//! dither arrays are independent.
	noise_type_blue,
	//! A precomputed table that distributes a Sobol sequence across pixels in
	//! a way that breaks patterns: Ahmed and Wonka 2020, ToG 39:6,
	//! Screen-Space Blue-Noise Diffusion of Monte Carlo Sampling Error via
	//! Hierarchical Ordering of Pixels,
	//! https://doi.org/10.1145/3414685.3417881
	noise_type_ahmed,
	//! Number of entries in this enumeration, not a valid type. We have
	//! moved this up to disable a few noise types that are not shipped with
	//! the demo.
	noise_type_count,
	//! Each pair of channels of each image is constructed from 4D Sobol
	//! points. The first two dimensions determine the screen space location,
	//! the latter two are the sample point in the integrand.
	noise_type_sobol,
	//! Like noise_type_sobol but uses Owen scrambling on top of Sobol
	noise_type_owen,
	//! Like noise_type_sobol but uses Owen scrambling based on Burley's
	//! implementation on top of Sobol
	noise_type_burley_owen,
	//! A 2D blue noise dither array, generated using the method due to:
	//! Iliyan Georgiev and Marcos Fajardo, 2016, Blue-noise dithered sampling,
	//! ACM SIGGRAPH 2016 Talks
	noise_type_blue_noise_dithered,
	//! True number of entries in this enumeration, counting disabled noise
	//! types
	noise_type_full_count,
} noise_type_t;


/*! This struct holds a texture array providing access to precomputed grids of
	sample points for integration (e.g. blue noise dither arrays).*/
typedef struct noise_table_s {
	//! A single texture array holding all of the sample points (RGBA)
	images_t noise_array;
	//! The next random seed that will be used for randomization of accesses to
	//! the texture array
	uint32_t random_seed;
} noise_table_t;


//! Returns the default resolution for precomputed tables of noise of the given
//! type
VkExtent3D get_default_noise_resolution(noise_type_t noise_type);

/*! Loads the specified noise table into a device-local texture array.
	\param noise The output object. Use destroy_noise_table() for cleanup.
	\param device Device used for texture creation.
	\param resolution Width and height of the noise textures as well as the
		number of RGBA slices in the texture array.
	\param noise_type The type of noise that is to be generated or loaded from
		disk.
	\return 0 on success.*/
int load_noise_table(noise_table_t* noise, const device_t* device, VkExtent3D resolution, noise_type_t noise_type);

//! Frees and zeros the given object
void destroy_noise_table(noise_table_t* noise, const device_t* device);

//! Writes constants that are needed to sample noise from the given table to
//! the given output arrays. If animate_noise is VK_TRUE, the random numbers
//! are different each frame.
void set_noise_constants(uint32_t resolution_mask[2], uint32_t* texture_index_mask, uint32_t random_numbers[4], noise_table_t* noise, VkBool32 animate_noise);
