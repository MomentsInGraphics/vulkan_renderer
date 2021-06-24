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


#include "polygonal_light_utility.glsl"
#include "ltc_utility.glsl"

layout (std140, row_major, binding = 0) uniform per_frame_constants {
	//! Bounding-box dependent constants needed for dequantization of positions
	vec3 g_mesh_dequantization_factor, g_mesh_dequantization_summand;
	//! The reciprocal of the minimal error that maps to a distinct color
	float g_error_factor;
	//! The transform from world space to projection space
	mat4 g_world_to_projection_space;
	//! Turns a column vector (pixel.x, pixel.y, 1.0f) with integer screen
	//! space coordinates into an unnormalized world space ray direction for
	//! the center of that pixel
	mat3 g_pixel_to_ray_direction_world_space;
	//! The location of the camera in world space
	vec3 g_camera_position_world_space;
	//! An estimate of how much darker each shading point is due to shadows.
	//! Of course, a uniform can impossibly get this right. This parameter
	//! mostly controls for which scenarios optimal MIS works well.
	float g_mis_visibility_estimate;
	//! The viewport size in pixels
	uvec2 g_viewport_size;
	//! The location of the mouse cursor in pixels, relative to the left top of
	//! the content area of the window
	ivec2 g_cursor_position;
	//! An exposure factor. It is multiplied onto the linear outgoing radiance
	//! once all shading work is completed
	float g_exposure_factor;
	//! This constant provides the means to experiment with surfaces of
	//! different roughness quickly. All roughness values are multiplied by it
	//! and then clampled to the range from zero to one.
	float g_roughness_factor;
	//! Resolution of textures in g_noise_table, which must be a power of two,
	//! minus one
	uvec2 g_noise_resolution_mask;
	//! Number of textures in g_noise_table, which must be a power of two,
	//! minus one
	uint g_noise_texture_index_mask;
	//! 0 if an LDR frame should be output, 1 if low bits of a 16-bit HDR frame
	//! should be output, 2 if high bits of a 16-bit HDR frame should be output
	uint g_frame_bits;
	//! Constants to randomize access to noise textures
	uvec4 g_noise_random_numbers;
	//! Constants for accessing linearly transformed cosine tables
	ltc_constants_t g_ltc_constants;
#ifdef POLYGONAL_LIGHT_ARRAY_SIZE
	//! The polygonal lights that are illuminating the scene
	polygonal_light_t g_polygonal_lights[POLYGONAL_LIGHT_ARRAY_SIZE];
#endif
};
