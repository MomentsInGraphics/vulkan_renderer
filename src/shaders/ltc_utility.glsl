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


//! \see ltc_constants_t in the C code for documentation
struct ltc_constants_t {
	float fresnel_index_factor, fresnel_index_summand;
	float roughness_factor, roughness_summand;
	float inclination_factor, inclination_summand;
};

//! \see ltc_table_t.texture_arrays for documentation
layout (binding = 7) uniform sampler2DArray g_ltc_tables[2];


/*! This structure holds all coefficients needed to evaluate linearly
	transformed cosines.*/
struct ltc_coefficients_t {
	/*! A rotation and translation that satisfies the following conditions:
		- The world space surface normal maps onto (0, 0, 1),
		- The world space outgoing light direction maps to the xz-plane and has
		  positive x-coordinate,
		- The world space shading position maps onto (0, 0, 0).
		Shading with linearly transformed cosines happens in this coordinate
		frame.*/
	mat4x3 world_to_shading_space;
	//! The matrix that maps directions from the local shading coordinate
	//! system to the space of the clamped cosine.
	mat3 shading_to_cosine_space;
	//! The matrix product shading_to_cosine_space * world_to_shading_space
	mat4x3 world_to_cosine_space;
	/*! The inverse of shading_to_cosine_space. Needed to construct shadow rays or
		to evaluate the original undistorted BRDF.*/
	mat3 cosine_to_shading_space;
	//! The albedo of the BRDF for the queried direction
	float albedo;
	/*! The determinant of shading_to_cosine_space.*/
	float shading_to_cosine_space_determinant;
};


/*! \return The coefficients needed to evaluate a linearly transformed cosine
	for a shading point with the given parameters. It also constructs the
	appropriate local coordinate frame for the given surface normal, outgoing
	light direction and shading position.*/
ltc_coefficients_t get_ltc_coefficients(float fresnel_0, float roughness, vec3 world_space_position, vec3 world_space_normal, vec3 world_space_outgoing_light_direction, ltc_constants_t constants) {
	ltc_coefficients_t ltc;
	float normal_dot_outgoing = dot(world_space_normal, world_space_outgoing_light_direction);
	float inclination = acos(clamp(normal_dot_outgoing, 0.0f, 1.0f));
	// Load the shading to cosine space transform and the albedo. We store LTC
	// transforms as five entries so that the maximal entry will be 1 and UNORM
	// quantization works.
	vec3 texture_coordinate = vec3(
		fma(sqrt(clamp(roughness, 0.0f, 1.0f)), constants.roughness_factor, constants.roughness_summand),
		fma(inclination, constants.inclination_factor, constants.inclination_summand),
		fma(clamp(fresnel_0, 0.0f, 1.0f), constants.fresnel_index_factor, constants.fresnel_index_summand));
	vec4 data_0 = textureLod(g_ltc_tables[0], texture_coordinate, 0.0f);
	vec2 data_1 = textureLod(g_ltc_tables[1], texture_coordinate, 0.0f).rg;
	ltc.shading_to_cosine_space = mat3(
		data_0.x, 0.0f, -data_0.y,
		0.0f, data_0.z, 0.0f,
		data_0.w, 0.0f, data_1.x);
	ltc.albedo = data_1.y;
	float determinant_2x2 = data_0.x * data_1.x + data_0.y * data_0.w;
	ltc.shading_to_cosine_space_determinant = data_0.z * determinant_2x2;
	// Invert the shading to cosine space transform
	float inv_determinant_2x2 = 1.0f / determinant_2x2;
	ltc.cosine_to_shading_space = mat3(
		data_1.x * inv_determinant_2x2, 0.0f, data_0.y * inv_determinant_2x2,
		0.0f, 1.0f / data_0.z, 0.0f,
		-data_0.w * inv_determinant_2x2, 0.0f, data_0.x * inv_determinant_2x2);
	// Construct shading space and related transforms
	vec3 x_axis = normalize(fma(vec3(-normal_dot_outgoing), world_space_normal, world_space_outgoing_light_direction));
	vec3 y_axis = cross(world_space_normal, x_axis);
	mat3 rotation = transpose(mat3(x_axis, y_axis, world_space_normal));
	ltc.world_to_shading_space = mat4x3(rotation[0], rotation[1], rotation[2], -rotation * world_space_position);
	ltc.world_to_cosine_space = ltc.shading_to_cosine_space * ltc.world_to_shading_space;
	return ltc;
}


/*! Evaluates the density of a linearly transformed cosine.
	\param ltc Coefficients as produced by get_ltc_coefficients().
	\param dir_shading_space A normalized direction vector in shading space of
		the LTC.
	\param rcp_projected_solid_angle The reciprocal of the projected solid
		angle of the domain in cosine space over which the LTC should be
		normalized. Pass 1 / pi to get the default normalization of LTCs.
		Whatever you pass just gets multiplied onto the output.
	\return The density.*/
float evaluate_ltc_density(ltc_coefficients_t ltc, vec3 dir_shading_space, float rcp_projected_solid_angle) {
	vec3 dir_cosine_space = ltc.shading_to_cosine_space * dir_shading_space;
	float cosine_space_length_squared = dot(dir_cosine_space, dir_cosine_space);
	float ltc_density = max(0.0f, dir_cosine_space.z) * ltc.shading_to_cosine_space_determinant / (cosine_space_length_squared * cosine_space_length_squared);
	return ltc_density * rcp_projected_solid_angle;
}


/*! Like evaluate_ltc_density() but inverted. It evaluates the density of a
	cosine distribution defined in shading space but linearly transformed to
	cosine space. In other words, it just works with the inverse transform.*/
float evaluate_ltc_density_inv(ltc_coefficients_t ltc, vec3 dir_cosine_space, float rcp_projected_solid_angle) {
	vec3 dir_shading_space = ltc.cosine_to_shading_space * dir_cosine_space;
	float shading_space_length_squared = dot(dir_shading_space, dir_shading_space);
	float ltc_density = max(0.0f, dir_shading_space.z) / (ltc.shading_to_cosine_space_determinant * shading_space_length_squared * shading_space_length_squared);
	return ltc_density * rcp_projected_solid_angle;
}
