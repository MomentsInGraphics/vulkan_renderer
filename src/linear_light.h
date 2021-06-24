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

//! Available methods for sampling linear lights
typedef enum sample_line_technique_e {
	//! Uniform sampling along the length of the line with incorrect density
	//! computation. This is as cheap as it gets and thus provides a good
	//! baseline for run time measurements.
	sample_line_baseline,
	//! Uniform sampling along the length of the line
	sample_line_area,
	//! Sampling proportional to solid angle
	sample_line_solid_angle,
	//! Sampling proportional to solid angle but with clipping to the upper
	//! hemisphere
	sample_line_clipped_solid_angle,
	//! Sampling proportional to solid angle but with clipping to the upper
	//! hemisphere and a linear density in primary sample space as proposed by
	//! Hart et al.
	sample_line_linear_cosine_warp_clipping_hart,
	//! Same with a quadratic density
	sample_line_quadratic_cosine_warp_clipping_hart,
	//! Sampling proportional to projected solid angle using the method of Li
	//! et al.
	sample_line_projected_solid_angle_li,
	//! Sampling proportional to projected solid angle using our method
	sample_line_projected_solid_angle,
	//! Number of available line sampling techniques
	sample_line_count
} sample_line_technique_t;


/*! This struct represents a cylinder light with infinitesimally small cylinder
	radius that works like a Lambertian emitter. By design, it matches the
	layout of the corresponding structure in the shader. Some members are
	redundant to avoid computations in a shader that operate on uniforms
	only. Use update_linear_light() to fill the correct values. For detailed
	documentation, refer to the shader.
	\see linear_light_utility.glsl */
typedef struct linear_light_s {
	float begin[3];
	// Use update_linear_light() to set line_length
	float line_length;
	float end[3];
	float padding_0;
	float radiance_times_radius[3];
	float padding_1;
	// Elements below are redundant. Use update_linear_light() to set them
	float begin_to_end[3];
	float padding_2;
	float line_direction[3];
	float padding_3;
} linear_light_t;

//! Updates values of redundant members
void update_linear_light(linear_light_t* light);
