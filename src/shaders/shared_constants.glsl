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


layout (std140, row_major, binding = 0) uniform per_frame_constants {
	//! Bounding-box dependent constants needed for dequantization of positions
	vec3 g_mesh_dequantization_factor, g_mesh_dequantization_summand;
	//! The transform from world space to projection space
	mat4 g_world_to_projection_space;
	//! Turns a column vector (pixel.x, pixel.y, 1.0f) with integer screen
	//! space coordinates into an unnormalized world space ray direction for
	//! the center of that pixel
	mat3 g_pixel_to_ray_direction_world_space;
	//! The location of the camera in world space
	vec3 g_camera_position_world_space;
	//! To map log2(error) to an error value from 0 to 1 for display, multiply
	//! by g_error_factor and add g_error_summand, then clamp
	float g_error_factor;
	//! The normalized world space direction vector for the directional light
	vec3 g_light_direction_world_space;
	//! \see g_error_factor
	float g_error_summand;
	//! The irradiance of the directional light on a surface orthogonal to the
	//! light direction (linear RGB)
	vec3 g_light_irradiance;
	//! The viewport size in pixels
	uvec2 g_viewport_size;
	//! The location of the mouse cursor in pixels, relative to the left top of
	//! the content area of the window
	ivec2 g_cursor_position;
	//! An exposure factor. It is multiplied onto the linear outgoing radiance
	//! once all shading work is completed
	float g_exposure_factor;
	//! All materials use this constant roughness value
	float g_roughness;
	//! 0 if an LDR frame should be output, 1 if low bits of a 16-bit HDR frame
	//! should be output, 2 if high bits of a 16-bit HDR frame should be output
	uint g_frame_bits;
	//! The texture coordinate that corresponds to the currently displayed time
	//! in the animation texture
	float g_time_tex_coord;
	//! The reciprocal of the number of bones in the animation texture
	float g_inv_bone_count;
	//! The reciprocal of the width of the animation texture (i.e. the distance
	//! in texture coordinates between data for two rows of a transform)
	float g_animation_column_spacing;
	//! 0.5f * g_animation_column_spacing
	float g_animation_half_column_spacing;
	//! Factors and summands used for dequantization of 16-bit uints in the
	//! animation texture
	vec4 g_animation_dequantization[4];
};
