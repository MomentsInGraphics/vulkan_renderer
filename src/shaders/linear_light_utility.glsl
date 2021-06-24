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


#include "ltc_utility.glsl"


/*! This struct represents a cylindrical light with infinitesimally small
	cylinder radius that works like a Lambertian emitter.*/
struct linear_light_t {
	//! The starting point of the linear light
	vec3 begin;
	//! The distance between begin and end. Placed here so that it functions as
	//! padding.
	float line_length;
	//! The ending point of the linear light
	vec3 end;
	/*! Each point on the surface of the cylinder emits light isotropically in
		all directions. This vector gives the light radiance in linear RGB
		assuming a (non-infinitesimal) radius of one.*/
	vec3 radiance_times_radius;
	//! The vector end - begin (not normalized).
	vec3 begin_to_end;
	//! Normalized version of begin_to_end.
	vec3 line_direction;
};


/*! This function transforms the given linear light from world space to cosine
	space as specified by the given linearly transformed cosine. For output
	vectors shading_position is the origin. The radiance_times_radius entry is
	just copied. out_ltc_density_factor is set to the factor by which densities
	of LTCs need to be multiplied to account for the change in the
	infinitesimal radius of the light source.*/
void transform_linear_light(out linear_light_t result, out float out_ltc_density_factor, linear_light_t light, ltc_coefficients_t ltc, vec3 shading_position) {
	// Transform positions
	result.begin = ltc.shading_to_cosine_space * (ltc.world_to_shading_space * vec4(light.begin, 1.0f));
	result.end = ltc.shading_to_cosine_space * (ltc.world_to_shading_space * vec4(light.end, 1.0f));
	// Compute redundant quantities
	result.begin_to_end = result.end - result.begin;
	float rcp_line_length = inversesqrt(dot(result.begin_to_end, result.begin_to_end));
	result.line_length = 1.0f / rcp_line_length;
	result.line_direction = result.begin_to_end * rcp_line_length;
	result.radiance_times_radius = light.radiance_times_radius;
	// Compute the density correction factor
	vec3 line_normal = cross(light.line_direction, light.begin - shading_position);
	vec3 transformed_line_normal = transpose(ltc.cosine_to_shading_space) * (ltc.world_to_shading_space * vec4(line_normal, 0.0f));
	out_ltc_density_factor = sqrt(dot(transformed_line_normal, transformed_line_normal) / dot(line_normal, line_normal));
}


/*! This function uses distance-field based rendering to display the given
	linear light. The light is visualized with a Gaussian falloff using radius
	as standard deviation in world space. This nicely avoids aliasing and gives
	a sortof bloom effect. If the given view_ray_end (in homogeneous
	coordinates) is met before the linear light along the view ray, the linear
	light is occluded. The view direction towards the shading point must be
	normalized.
	\return An RGB radiance.*/
vec3 render_linear_light(linear_light_t light, float radius, vec3 camera_position, vec3 view_ray_direction, vec4 view_ray_end) {
	// Compute the point on the linear light with minimal distance to the view
	// ray (relative to the camera position)
	vec3 offset = light.begin - camera_position;
	vec3 orthogonal = fma(-view_ray_direction, vec3(dot(view_ray_direction, light.line_direction)), light.line_direction);
	float line_x = -dot(offset, orthogonal) / dot(orthogonal, orthogonal);
	line_x = clamp(line_x, 0.0f, light.line_length);
	vec3 closest_point = fma(light.line_direction, vec3(line_x), offset);
	// Compare the distance along the view ray to closest_point and
	// view_ray_end
	float closest_depth = max(0.0f, dot(view_ray_direction, closest_point));
	float end_depth = dot(view_ray_direction, fma(vec3(-view_ray_end.w), camera_position, view_ray_end.xyz));
	if (closest_depth * view_ray_end.w > end_depth)
		return vec3(0.0f);
	// Compute the minimal distance between the ray and the line
	float min_distance_squared = fma(-closest_depth, closest_depth, dot(closest_point, closest_point));
	// Now put together the radiance
	vec3 radiance = light.radiance_times_radius * (1.0f / radius);
	return exp(-0.5f * min_distance_squared / (radius * radius)) * radiance;
}
