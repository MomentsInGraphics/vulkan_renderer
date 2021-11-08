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


#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_samplerless_texture_functions : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_control_flow_attributes : enable
#if TRACE_SHADOW_RAYS
#extension GL_EXT_ray_query : enable
#endif
#include "noise_utility.glsl"
#include "brdfs.glsl"
#include "mesh_quantization.glsl"
//#include "line_sampling.glsl" via line_sampling_related_work.glsl
#include "line_sampling_related_work.glsl"
#include "shared_constants.glsl"
#include "srgb_utility.glsl"
#include "unrolling.glsl"

#if TRACE_SHADOW_RAYS
/*! Ray tracing instructions directly inside loops cause huge slow-downs. The
	[[unroll]] directive from GL_EXT_control_flow_attributes only helps to some
	extent (in HLSL it is much more effective). Thus, we take a rather drastic
	approch. We duplicate code in the pre-processor if ray tracing is enabled.
	\see unrolling.glsl */
#define RAY_TRACING_FOR_LOOP(INDEX, COUNT, CLAMPED_COUNT, CODE) UNROLLED_FOR_LOOP(INDEX, COUNT, CLAMPED_COUNT, CODE)
#else
//! Without ray tracing, we use common for loops
#define RAY_TRACING_FOR_LOOP(INDEX, COUNT, CLAMPED_COUNT, CODE) for (uint INDEX = 0; INDEX != COUNT; ++INDEX) {CODE}
#endif


//! Bindings for mesh geometry (see mesh_t in the C code)
layout (binding = 1) uniform utextureBuffer g_quantized_vertex_positions;
layout (binding = 2) uniform textureBuffer g_packed_normals_and_tex_coords;
layout (binding = 3) uniform utextureBuffer g_material_indices;

//! The texture with primitive indices per pixel produced by the visibility pass
layout (binding = 4, input_attachment_index = 0) uniform usubpassInput g_visibility_buffer;

//! Textures (base color, specular, normal consecutively) for each material
layout (binding = 5) uniform sampler2D g_material_textures[3 * MATERIAL_COUNT];

//! The top-level acceleration structure that contains all shadow-casting
//! geometry
#if TRACE_SHADOW_RAYS
layout(binding = 8, set = 0) uniform accelerationStructureEXT g_top_level_acceleration_structure;
#endif

//! The pixel index with origin in the upper left corner
layout(origin_upper_left) in vec4 gl_FragCoord;
//! Color written to the swapchain image
layout (location = 0) out vec4 g_out_color;


/*! Turns an error value into a color that makes it easy to see the magnitude
	of the error. The method uses the tab20b colormap of matplotlib, which
	uses colors with five hues, each in four different levels of saturation.
	Each of these five hues maps to one power of ten, ranging from 1.0e-7f to
	1.0e-2f.*/
vec3 error_to_color(float error) {
	const float min_exponent = 0.0f;
	const float max_exponent = 5.0f;
	const float min_error = pow(10.0, min_exponent);
	const float max_error = pow(10.0, max_exponent - 0.01f);
	float color_count = 20.0f;
	error = clamp(abs(g_error_factor * error), min_error, max_error);
	// 0.0f for log10(error) == min_exponent
	// color_count for log10(error) == max_exponent
	float color_index = fma(log2(error), color_count / ((max_exponent - min_exponent) * log2(10.0)), color_count * -min_exponent / (max_exponent - min_exponent));
	// These colors have been converted from sRGB to linear Rec. 709
	vec3 tab20b_colors[] = {
		vec3(0.04092, 0.04374, 0.19120),
		vec3(0.08438, 0.08866, 0.36625),
		vec3(0.14703, 0.15593, 0.62396),
		vec3(0.33245, 0.34191, 0.73046),
		vec3(0.12477, 0.19120, 0.04092),
		vec3(0.26225, 0.36131, 0.08438),
		vec3(0.46208, 0.62396, 0.14703),
		vec3(0.61721, 0.70838, 0.33245),
		vec3(0.26225, 0.15293, 0.03071),
		vec3(0.50888, 0.34191, 0.04092),
		vec3(0.79910, 0.49102, 0.08438),
		vec3(0.79910, 0.59720, 0.29614),
		vec3(0.23074, 0.04519, 0.04092),
		vec3(0.41789, 0.06663, 0.06848),
		vec3(0.67244, 0.11954, 0.14703),
		vec3(0.79910, 0.30499, 0.33245),
		vec3(0.19807, 0.05286, 0.17144),
		vec3(0.37626, 0.08228, 0.29614),
		vec3(0.61721, 0.15293, 0.50888),
		vec3(0.73046, 0.34191, 0.67244),
	};
	return tab20b_colors[int(color_index)];
}


/*! Gets the random numbers for the next sample. It assumes that a single one-
	dimensional integral is approximated using SAMPLE_COUNT samples.
	\param noise The previously used sample (if sample_index > 0), which is
		overwritten with the next sample.
	\param sample_index An index from 0 to SAMPLE_COUNT - 1. You have to invoke
		this method exactly once with each index in increasing order.
	\param accessor An accessor for a noise table. With jittered uniform
		sampling, it is only used if sample_index == 0, otherwise it is used
		for each sample.*/
void get_sample_noise_1(inout float noise, uint sample_index, inout noise_accessor_t accessor) {
#if USE_JITTERED_UNIFORM
	if (sample_index == 0)
		noise = get_noise_1(accessor) * (1.0f / SAMPLE_COUNT);
	else
		noise += 1.0f / SAMPLE_COUNT;
#else
	noise = get_noise_1(accessor);
#endif
}

//! Variant of get_sample_noise_1() that provides access to two random numbers
//! at once
void get_sample_noise_2(inout vec2 noise, uint sample_index, inout noise_accessor_t accessor) {
#if USE_JITTERED_UNIFORM
	if (sample_index == 0)
		noise = get_noise_2(accessor) * (1.0f / SAMPLE_COUNT);
	else
		noise += vec2(1.0f / SAMPLE_COUNT);
#else
	noise = get_noise_2(accessor);
#endif
}


//! Assumes that the given ray intersects the given linear light and computes a
//! t such that ray_origin + t * ray_direction is a point on the linear light
float get_ray_line_intersection(linear_light_t linear_light, vec3 ray_origin, vec3 ray_direction) {
	vec3 line_begin_offset = ray_origin - linear_light.begin;
	// Solve the problem twice and take the solution that looks more stable
	vec2 kernel_0 = cross(vec3(ray_direction.x, linear_light.line_direction.x, line_begin_offset.x), vec3(ray_direction.y, linear_light.line_direction.y, line_begin_offset.y)).xz;
	vec2 kernel_1 = cross(vec3(ray_direction.x, linear_light.line_direction.x, line_begin_offset.x), vec3(ray_direction.z, linear_light.line_direction.z, line_begin_offset.z)).xz;
	return (abs(kernel_0.y) > abs(kernel_1.y)) ? (kernel_0.x / kernel_0.y) : (kernel_1.x / kernel_1.y);
}

/*! If shadow rays are enabled, this function traces a shadow ray towards the
	given linear light and updates visibility accordingly. If visibility is
	false already, no ray is traced. The ray direction must be normalized.*/
void get_line_visibility(inout bool visibility, vec3 ray_direction, vec3 shading_position, linear_light_t linear_light) {
#if TRACE_SHADOW_RAYS
	if (visibility) {
		float max_t = get_ray_line_intersection(linear_light, shading_position, ray_direction);
		float min_t = 1.0e-3f;
		// Perform a ray query and wait for it to finish. One call to
		// rayQueryProceedEXT() should be enough because of
		// gl_RayFlagsTerminateOnFirstHitEXT.
		rayQueryEXT ray_query;
		rayQueryInitializeEXT(ray_query, g_top_level_acceleration_structure,
			gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,
			0xFF, shading_position, min_t, ray_direction, max_t);
		rayQueryProceedEXT(ray_query);
		// Update the visibility
		bool occluder_hit = (rayQueryGetIntersectionTypeEXT(ray_query, true) != gl_RayQueryCommittedIntersectionNoneEXT);
		visibility = !occluder_hit;
	}
#endif
}


/*! Determines the radiance times radius received from the given direction due
	to the given linear light and multiplies it by the BRDF for this direction.
	If necessary, this function traces a shadow ray to determine visibility.
	\param lambert The dot product of the normal vector and the given
		direction, in case you have use for it outside this function.
	\param sampled_dir The normalized direction from the shading point to the
		light source in world space. It must be chosen such that the ray
		actually intersects the line (possibly behind an occluder or below
		the horizon).
	\param shading_data Shading data for the shading point.
	\param linear_light The light source for which the incoming radiance is
		evaluated.
	\return BRDF times incoming radiance times visibility.*/
vec3 get_line_radiance_radius_visibility_brdf_product(out float out_lambert, vec3 sampled_dir, shading_data_t shading_data, linear_light_t linear_light) {
	out_lambert = dot(shading_data.normal, sampled_dir);
	bool visibility = (out_lambert > 0.0f);
	get_line_visibility(visibility, sampled_dir, shading_data.position, linear_light);
	if (visibility)
		return linear_light.radiance_times_radius * evaluate_brdf(shading_data, sampled_dir);
	else
		return vec3(0.0f);
}


/*! Implements weight computation for multiple importance sampling (MIS) using
	the currently enabled heuristic.
	\param sampled_density The probability density function of the strategy
		used to create the sample at the sample location.
	\param other_density The probability density function of the other used
		strategy at the sample location.
	\return The MIS weight for the sample, divided by the density used to draw
		that sample (i.e. sampled_density).
	\see mis_heuristic_t */
float get_mis_weight_over_density(float sampled_density, float other_density) {
#if MIS_HEURISTIC_BALANCE
	return 1.0f / (sampled_density + other_density);
#elif MIS_HEURISTIC_POWER
	return sampled_density / (sampled_density * sampled_density + other_density * other_density);
#else
	// Not supported, use get_mis_estimate()
	return 0.0f;
#endif
}


/*! Returns the MIS estimator for the given sample using the currently enabled
	MIS heuristic. It supports our weighted balance heuristic and clamped
	optimal MIS.
	\param integrand The value of the integrand with respect to solid angle
		measure at the sampled location.
	\param sampled_density, other_density See get_mis_weight_over_density().
	\param sampled_weight, other_weight Estimates of unshadowed shading for the
		respective BRDF components. For all techniques except optimal MIS, it
		is legal to introduce an arbitrary but identical constant factor for
		both of them.
	\param visibility_estimate Some estimate of how much the shading point is
		shadowed on average (0 means fully shadowed). It does not have to be
		accurate, it just blends between two MIS heuristics for optimal MIS.
	\return An unbiased multiple importance sampling estimator. Note that it
		may be negative when optimal MIS is used.*/
vec3 get_mis_estimate(vec3 integrand, vec3 sampled_weight, float sampled_density, vec3 other_weight, float other_density, float visibility_estimate) {
#if MIS_HEURISTIC_WEIGHTED
	vec3 weighted_sum = sampled_weight * sampled_density + other_weight * other_density;
	return (sampled_weight * integrand) / weighted_sum;

#elif MIS_HEURISTIC_OPTIMAL_CLAMPED
	float balance_weight_over_density = 1.0f / (sampled_density + other_density);
	vec3 weighted_sum = sampled_weight * sampled_density + other_weight * other_density;
	vec3 weighted_weight_over_density = sampled_weight / weighted_sum;
	vec3 mixed_weight_over_density = vec3(fma(-visibility_estimate, balance_weight_over_density, balance_weight_over_density));
	mixed_weight_over_density = fma(vec3(visibility_estimate), weighted_weight_over_density, vec3(mixed_weight_over_density));
	// For visible samples, we use the actual integrand
	vec3 visible_estimate = mixed_weight_over_density * integrand;
	return visible_estimate;

#else
	return get_mis_weight_over_density(sampled_density, other_density) * integrand;
#endif
}


/*! Returns the Monte Carlo estimate of the lighting contribution of a
	linear light. Most parameters forward to
	get_line_radiance_radius_visibility_brdf_product(). The given sample must
	be generated by next event estimation, i.e. as direction towards the light
	source.
	\param sampled_density The density of sampled_dir with respect to the solid
		angle measure.
	\see get_line_radiance_radius_visibility_brdf_product() */
vec3 get_linear_light_monte_carlo_estimate(vec3 sampled_dir, float sampled_density, shading_data_t shading_data, linear_light_t linear_light) {
	float lambert;
	vec3 radiance_times_brdf = get_line_radiance_radius_visibility_brdf_product(lambert, sampled_dir, shading_data, linear_light);
	// If the density is exactly zero, that must also be true for the integrand
	return (sampled_density > 0.0f) ? (radiance_times_brdf * (lambert / sampled_density)) : vec3(0.0f);
}


/*! Takes samples from the given linear light to compute shading. The number
	of samples and sampling techniques are determined by defines.
	\return The color that arose from shading.*/
vec3 evaluate_linear_light_shading(shading_data_t shading_data, ltc_coefficients_t ltc, linear_light_t linear_light, inout noise_accessor_t accessor) {
	vec3 result = vec3(0.0f);

	// If requested, clip the given line and make it relative to the shading
	// point
#if SAMPLE_LINE_CLIPPED_SOLID_ANGLE || SAMPLE_LINE_LINEAR_COSINE_WARP_CLIPPING_HART || SAMPLE_LINE_QUADRATIC_COSINE_WARP_CLIPPING_HART || SAMPLE_LINE_PROJECTED_SOLID_ANGLE_LI || (SAMPLE_LINE_PROJECTED_SOLID_ANGLE && SAMPLING_STRATEGIES_DIFFUSE_ONLY)
	vec3 clipped_begin = linear_light.begin - shading_data.position;
	vec3 clipped_end;
	float clipped_length = linear_light.line_length;
	clip_line(vec3(0.0f), shading_data.normal, clipped_begin, clipped_end, linear_light.line_direction, clipped_length);
	if (clipped_length <= 0.0f)
		return vec3(0.0f);
#endif

#if SAMPLE_LINE_BASELINE
	vec3 begin_offset = linear_light.begin - shading_data.position;
	float random_number;
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		// This technique is broken. Its purpose is to make sure that random
		// numbers are still consumed and the BRDF is still evaluated while the
		// cost for sampling is negligible. Useful as baseline in run time
		// measurements.
		get_sample_noise_1(random_number, i, accessor);
		vec3 light_sample = fma(vec3(random_number), linear_light.begin_to_end, begin_offset);
		vec3 diffuse_dir = normalize(light_sample);
		result += get_linear_light_monte_carlo_estimate(diffuse_dir, 1.0f, shading_data, linear_light);
	)

#elif SAMPLE_LINE_AREA
	vec3 begin_offset = linear_light.begin - shading_data.position;
	float random_number;
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		// Sample the light source uniformly by length
		get_sample_noise_1(random_number, i, accessor);
		vec3 light_sample = fma(vec3(random_number), linear_light.begin_to_end, begin_offset);
		float squared_distance = dot(light_sample, light_sample);
		vec3 diffuse_dir = light_sample * inversesqrt(squared_distance);
		// Compute the density times radius
		float lambert_outgoing = dot(linear_light.line_direction, diffuse_dir);
		lambert_outgoing = sqrt(max(0.0f, fma(-lambert_outgoing, lambert_outgoing, 1.0f)));
		float density = squared_distance / (2.0f * linear_light.line_length * lambert_outgoing);
		result += get_linear_light_monte_carlo_estimate(diffuse_dir, density, shading_data, linear_light);
	)

#elif SAMPLE_LINE_SOLID_ANGLE
	// Prepare sampling
	solid_angle_line_t line_diffuse = prepare_solid_angle_line_sampling(
		linear_light.begin - shading_data.position, linear_light.line_direction, linear_light.line_length);
	// Perform sampling
	float random_number;
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		get_sample_noise_1(random_number, i, accessor);
		vec3 diffuse_dir = sample_solid_angle_line(line_diffuse, random_number);
		float density = 1.0f / line_diffuse.rcp_density;
		result += get_linear_light_monte_carlo_estimate(diffuse_dir, density, shading_data, linear_light);
	)

#elif SAMPLE_LINE_CLIPPED_SOLID_ANGLE
	// Prepare sampling
	solid_angle_line_t line_diffuse = prepare_solid_angle_line_sampling(
		clipped_begin, linear_light.line_direction, clipped_length);
	// Perform sampling
	float random_number;
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		get_sample_noise_1(random_number, i, accessor);
		vec3 diffuse_dir = sample_solid_angle_line(line_diffuse, random_number);
		float density = 1.0f / line_diffuse.rcp_density;
		result += get_linear_light_monte_carlo_estimate(diffuse_dir, density, shading_data, linear_light);
	)

#elif SAMPLE_LINE_LINEAR_COSINE_WARP_CLIPPING_HART
	// Prepare sampling
	linear_cosine_warp_line_hart_t line_diffuse = prepare_linear_cosine_warp_line_sampling_hart(
		shading_data.normal, linear_light.begin - shading_data.position, linear_light.line_direction, linear_light.line_length);
	// Perform sampling
	float random_number;
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		get_sample_noise_1(random_number, i, accessor);
		float density;
		vec3 diffuse_dir = sample_linear_cosine_warp_line_hart(density, line_diffuse, random_number);
		result += get_linear_light_monte_carlo_estimate(diffuse_dir, density, shading_data, linear_light);
	)

#elif SAMPLE_LINE_QUADRATIC_COSINE_WARP_CLIPPING_HART
	// Prepare sampling
	quadratic_cosine_warp_line_hart_t line_diffuse = prepare_quadratic_cosine_warp_line_sampling_hart(
		shading_data.normal, linear_light.begin - shading_data.position, linear_light.line_direction, linear_light.line_length);
	// Perform sampling
	float random_number;
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		get_sample_noise_1(random_number, i, accessor);
		float density;
		vec3 diffuse_dir = sample_quadratic_cosine_warp_line_hart(density, line_diffuse, random_number);
		result += get_linear_light_monte_carlo_estimate(diffuse_dir, density, shading_data, linear_light);
	)

#elif SAMPLE_LINE_PROJECTED_SOLID_ANGLE_LI
	// Prepare sampling
	projected_solid_angle_line_li_t line_diffuse = prepare_projected_solid_angle_line_sampling_li_without_clipping(
		shading_data.normal, clipped_begin, linear_light.line_direction, clipped_length);
	// Perform sampling
	float random_number;
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		get_sample_noise_1(random_number, i, accessor);
		vec3 diffuse_dir = sample_projected_solid_angle_line_li(line_diffuse, random_number);
		float density = dot(shading_data.normal, diffuse_dir) / line_diffuse.rcp_density;
		result += get_linear_light_monte_carlo_estimate(diffuse_dir, density, shading_data, linear_light);
	)

#elif SAMPLE_LINE_PROJECTED_SOLID_ANGLE && SAMPLING_STRATEGIES_DIFFUSE_ONLY

	// Prepare sampling
	projected_solid_angle_line_t line_diffuse = prepare_projected_solid_angle_line_sampling_without_clipping(
		shading_data.normal, clipped_begin, linear_light.line_direction, clipped_length);
#if ERROR_DISPLAY_DIFFUSE
	float random_number = get_noise_1(accessor);
	vec3 sampled_dir = sample_projected_solid_angle_line(line_diffuse, random_number);
	float error = compute_projected_solid_angle_line_sampling_error(line_diffuse, random_number, sampled_dir);
	float ltc_brightness = dot(linear_light.radiance_times_radius * shading_data.diffuse_albedo, vec3(0.2126f, 0.7152f, 0.0722f)) * (g_exposure_factor * M_INV_PI * line_diffuse.solid.rcp_density);
	float error_scales[2] = { 1.0f, ltc_brightness };
	error *= error_scales[ERROR_INDEX];
	return error_to_color(error) / g_exposure_factor;
#else
	// Perform sampling
	float random_number;
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		get_sample_noise_1(random_number, i, accessor);
		vec3 diffuse_dir = sample_projected_solid_angle_line(line_diffuse, random_number);
		float density = dot(shading_data.normal, diffuse_dir) / line_diffuse.solid.rcp_density;
		result += get_linear_light_monte_carlo_estimate(diffuse_dir, density, shading_data, linear_light);
	)
#endif

#elif SAMPLE_LINE_PROJECTED_SOLID_ANGLE && SAMPLING_STRATEGIES_DIFFUSE_SPECULAR_MIS

	// Clip the linear light in place
	clip_line(shading_data.position, shading_data.normal, linear_light.begin, linear_light.end, linear_light.line_direction, linear_light.line_length);
	if (linear_light.line_length <= 0.0f)
		return vec3(0.0f);
	// Prepare diffuse sampling
	projected_solid_angle_line_t line_diffuse = prepare_projected_solid_angle_line_sampling_without_clipping(
		shading_data.normal, linear_light.begin - shading_data.position, linear_light.line_direction, linear_light.line_length);
	if (line_diffuse.solid.rcp_density <= 0.0f)
		return vec3(0.0f);

#if ERROR_DISPLAY_DIFFUSE
	float random_number = get_noise_1(accessor);
	vec3 sampled_dir = sample_projected_solid_angle_line(line_diffuse, random_number);
	float error = compute_projected_solid_angle_line_sampling_error(line_diffuse, random_number, sampled_dir);
	float ltc_brightness = dot(linear_light.radiance_times_radius * shading_data.diffuse_albedo, vec3(0.2126f, 0.7152f, 0.0722f)) * (g_exposure_factor * M_INV_PI * line_diffuse.solid.rcp_density);
	float error_scales[2] = { 1.0f, ltc_brightness };
	error *= error_scales[ERROR_INDEX];
	return error_to_color(error) / g_exposure_factor;
#else

	// Transform to cosine space
	linear_light_t transformed_light;
	float ltc_density_factor;
	transform_linear_light(transformed_light, ltc_density_factor, linear_light, ltc, shading_data.position);
	// Clip in cosine space
	clip_line(vec3(0.0f), vec3(0.0f, 0.0f, 1.0f), transformed_light.begin, transformed_light.end, transformed_light.line_direction, transformed_light.line_length);
	bool skip_specular = (transformed_light.line_length <= 0.0f);
	projected_solid_angle_line_t line_specular;
	if (!skip_specular) {
		// Prepare specular sampling
		line_specular = prepare_projected_solid_angle_line_sampling_without_clipping(
			vec3(0.0f, 0.0f, 1.0f), transformed_light.begin, transformed_light.line_direction, transformed_light.line_length);
		skip_specular = (line_specular.solid.rcp_density <= 0.0f);
	}
	// If the LTC is zero on the whole linear light, use diffuse only
	if (skip_specular) {
#if ERROR_DISPLAY_SPECULAR
		return vec3(0.0f);
#else
		// Perform sampling
		float random_number;
		RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
			get_sample_noise_1(random_number, i, accessor);
			vec3 diffuse_dir = sample_projected_solid_angle_line(line_diffuse, random_number);
			float density = dot(shading_data.normal, diffuse_dir) / line_diffuse.solid.rcp_density;
			result += get_linear_light_monte_carlo_estimate(diffuse_dir, density, shading_data, linear_light);
		)
#endif
	}
	// Otherwise use diffuse and specular sampling with MIS
	else {
#if ERROR_DISPLAY_SPECULAR
		float random_number = get_noise_1(accessor);
		vec3 sampled_dir = sample_projected_solid_angle_line(line_specular, random_number);
		float error = compute_projected_solid_angle_line_sampling_error(line_specular, random_number, sampled_dir);
		float ltc_brightness = dot(linear_light.radiance_times_radius * ltc.albedo, vec3(0.2126f, 0.7152f, 0.0722f)) * (g_exposure_factor * M_INV_PI * line_specular.solid.rcp_density / ltc_density_factor);
		float error_scales[2] = { 1.0f, ltc_brightness };
		error *= error_scales[ERROR_INDEX];
		return error_to_color(error) / g_exposure_factor;

#else
		// Compute the importance of the sampling techniques using their
		// albedos and projected solid angles. Zero albedo for diffuse is
		// forbidden because we need a non-zero weight for diffuse samples in
		// parts where the LTC is zero but the specular BRDF is not. Thus, we
		// clamp.
		vec3 diffuse_albedo = max(shading_data.diffuse_albedo, vec3(0.01f));
		vec3 diffuse_weight = diffuse_albedo * line_diffuse.solid.rcp_density;
		float rcp_diffuse_projected_solid_angle = 1.0f / line_diffuse.solid.rcp_density;
		float specular_albedo = ltc.albedo;
		float specular_weight = specular_albedo * (line_specular.solid.rcp_density / ltc_density_factor);
		float rcp_specular_projected_solid_angle = ltc_density_factor / line_specular.solid.rcp_density;
		vec3 specular_weight_rgb = vec3(specular_weight);
		// Perform sampling
		vec2 random_numbers;
		RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
			get_sample_noise_2(random_numbers, i, accessor);
			// Use the diffuse strategy and evaluate both densities
			vec3 diffuse_dir = sample_projected_solid_angle_line(line_diffuse, random_numbers[0]);
			float diffuse_density_diffuse_dir = dot(shading_data.normal, diffuse_dir) * rcp_diffuse_projected_solid_angle;
			float specular_density_diffuse_dir = evaluate_ltc_density_world_space(ltc, diffuse_dir, rcp_specular_projected_solid_angle);
			// Use the specular strategy, evaluate both densties and transform
			vec3 specular_dir_cosine_space = sample_projected_solid_angle_line(line_specular, random_numbers[1]);
			vec3 specular_dir_shading_space;
			float specular_density_specular_dir = evaluate_ltc_density_cosine_space(specular_dir_shading_space, ltc, specular_dir_cosine_space, rcp_specular_projected_solid_angle);
			vec3 specular_dir = (transpose(ltc.world_to_shading_space) * specular_dir_shading_space).xyz;
			float diffuse_density_specular_dir = specular_dir_shading_space.z * rcp_diffuse_projected_solid_angle;
			// Evaluate radiance and BRDF and the integrand as a whole
			float lambert;
			vec3 integrand_diffuse_dir = get_line_radiance_radius_visibility_brdf_product(lambert, diffuse_dir, shading_data, linear_light);
			integrand_diffuse_dir *= lambert;
			vec3 integrand_specular_dir = get_line_radiance_radius_visibility_brdf_product(lambert, specular_dir, shading_data, linear_light);
			integrand_specular_dir *= lambert;
			// Evaluate the MIS estimates
			result += get_mis_estimate(integrand_diffuse_dir, diffuse_weight, diffuse_density_diffuse_dir,
				specular_weight_rgb, specular_density_diffuse_dir, g_mis_visibility_estimate);
			result += get_mis_estimate(integrand_specular_dir, specular_weight_rgb, specular_density_specular_dir,
				diffuse_weight, diffuse_density_specular_dir, g_mis_visibility_estimate);
		)
#endif
	}
#endif

#endif

	return result * (1.0f / SAMPLE_COUNT);
}


/*! Based on the knowledge that the given primitive is visible on the given
	pixel, this function recovers complete shading data for this pixel.
	\param pixel Coordinates of the pixel inside the viewport in pixels.
	\param primitive_index Index into g_material_indices, etc.
	\param ray_direction Direction of a ray through that pixel in world space.
		Does not need to be normalized.
	\note This implementation assumes a perspective projection */
shading_data_t get_shading_data(ivec2 pixel, int primitive_index, vec3 ray_direction) {
	shading_data_t result;
	// Load position, normal and texture coordinates for each triangle vertex
	vec3 positions[3], normals[3];
	vec2 tex_coords[3];
	[[unroll]]
	for (int i = 0; i != 3; ++i) {
		int vertex_index = primitive_index * 3 + i;
		uvec2 quantized_position = texelFetch(g_quantized_vertex_positions, vertex_index).rg;
		positions[i] = decode_position_64_bit(quantized_position, g_mesh_dequantization_factor, g_mesh_dequantization_summand);
		vec4 normal_and_tex_coords = texelFetch(g_packed_normals_and_tex_coords, vertex_index);
		normals[i] = decode_normal_32_bit(normal_and_tex_coords.xy);
		tex_coords[i] = fma(normal_and_tex_coords.zw, vec2(8.0f, -8.0f), vec2(0.0f, 1.0f));
	}
	// Construct the view ray for the pixel at hand (the ray direction is not
	// normalized)
	vec3 ray_origin = g_camera_position_world_space;
	// Perform ray triangle intersection to figure out barycentrics within the
	// triangle
	vec3 barycentrics;
	vec3 edges[2] = {
		positions[1] - positions[0],
		positions[2] - positions[0]
	};
	vec3 ray_cross_edge_1 = cross(ray_direction, edges[1]);
	float rcp_det_edges_direction = 1.0f / dot(edges[0], ray_cross_edge_1);
	vec3 ray_to_0 = ray_origin - positions[0];
	float det_0_dir_edge_1 = dot(ray_to_0, ray_cross_edge_1);
	barycentrics.y = rcp_det_edges_direction * det_0_dir_edge_1;
	vec3 edge_0_cross_0 = cross(edges[0], ray_to_0);
	float det_dir_edge_0_0 = dot(ray_direction, edge_0_cross_0);
	barycentrics.z = -rcp_det_edges_direction * det_dir_edge_0_0;
	barycentrics.x = 1.0f - (barycentrics.y + barycentrics.z);
	// Compute screen space derivatives for the barycentrics
	vec3 barycentrics_derivs[2];
	[[unroll]]
	for (uint i = 0; i != 2; ++i) {
		vec3 ray_direction_deriv = g_pixel_to_ray_direction_world_space[i];
		vec3 ray_cross_edge_1_deriv = cross(ray_direction_deriv, edges[1]);
		float rcp_det_edges_direction_deriv = -dot(edges[0], ray_cross_edge_1_deriv) * rcp_det_edges_direction * rcp_det_edges_direction;
		float det_0_dir_edge_1_deriv = dot(ray_to_0, ray_cross_edge_1_deriv);
		barycentrics_derivs[i].y = rcp_det_edges_direction_deriv * det_0_dir_edge_1 + rcp_det_edges_direction * det_0_dir_edge_1_deriv;
		float det_dir_edge_0_0_deriv = dot(ray_direction_deriv, edge_0_cross_0);
		barycentrics_derivs[i].z = -rcp_det_edges_direction_deriv * det_dir_edge_0_0 - rcp_det_edges_direction * det_dir_edge_0_0_deriv;
		barycentrics_derivs[i].x = -(barycentrics_derivs[i].y + barycentrics_derivs[i].z);
	}
	// Interpolate vertex attributes across the triangle
	result.position = fma(vec3(barycentrics[0]), positions[0], fma(vec3(barycentrics[1]), positions[1], barycentrics[2] * positions[2]));
	vec3 interpolated_normal = normalize(fma(vec3(barycentrics[0]), normals[0], fma(vec3(barycentrics[1]), normals[1], barycentrics[2] * normals[2])));
	vec2 tex_coord = fma(vec2(barycentrics[0]), tex_coords[0], fma(vec2(barycentrics[1]), tex_coords[1], barycentrics[2] * tex_coords[2]));
	// Compute screen space texture coordinate derivatives for filtering
	vec2 tex_coord_derivs[2] = { vec2(0.0f), vec2(0.0f) };
	[[unroll]]
	for (uint i = 0; i != 2; ++i)
		[[unroll]]
		for (uint j = 0; j != 3; ++j)
			tex_coord_derivs[i] += barycentrics_derivs[i][j] * tex_coords[j];
	// Read all three textures
	uint material_index = texelFetch(g_material_indices, primitive_index).r;
	vec3 base_color = textureGrad(g_material_textures[nonuniformEXT(3 * material_index + 0)], tex_coord, tex_coord_derivs[0], tex_coord_derivs[1]).rgb;
	vec3 specular_data = textureGrad(g_material_textures[nonuniformEXT(3 * material_index + 1)], tex_coord, tex_coord_derivs[0], tex_coord_derivs[1]).rgb;
	vec3 normal_tangent_space;
	normal_tangent_space.xy = textureGrad(g_material_textures[nonuniformEXT(3 * material_index + 2)], tex_coord, tex_coord_derivs[0], tex_coord_derivs[1]).rg;
	normal_tangent_space.xy = fma(normal_tangent_space.xy, vec2(2.0f), vec2(-1.0f));
	normal_tangent_space.z = sqrt(max(0.0f, fma(-normal_tangent_space.x, normal_tangent_space.x, fma(-normal_tangent_space.y, normal_tangent_space.y, 1.0f))));
	// Prepare BRDF parameters (i.e. immitate Falcor to be compatible with its
	// assets, which in turn immitates the Unreal engine). The Fresnel F0 value
	// for surfaces with zero metalicity is set to 0.02, not 0.04 as in Falcor
	// because this way colors throughout the scenes are a little less
	// desaturated.
	float metalicity = specular_data.b;
	result.diffuse_albedo = fma(base_color, -vec3(metalicity), base_color);
	result.fresnel_0 = mix(vec3(0.02f), base_color, metalicity);
	float linear_roughness = specular_data.g;
	result.roughness = linear_roughness * linear_roughness;
	result.roughness = clamp(result.roughness * g_roughness_factor, 0.0064f, 1.0f);
	// Transform the normal vector to world space
	vec2 tex_coord_edges[2] = {
		tex_coords[1] - tex_coords[0],
		tex_coords[2] - tex_coords[0]
	};
	vec3 normal_cross_edge_0 = cross(interpolated_normal, edges[0]);
	vec3 edge1_cross_normal = cross(edges[1], interpolated_normal);
	vec3 tangent = edge1_cross_normal * tex_coord_edges[0].x + normal_cross_edge_0 * tex_coord_edges[1].x;
	vec3 bitangent = edge1_cross_normal * tex_coord_edges[0].y + normal_cross_edge_0 * tex_coord_edges[1].y;
	float mean_tangent_length = sqrt(0.5f * (dot(tangent, tangent) + dot(bitangent, bitangent)));
	mat3 tangent_to_world_space = mat3(tangent, bitangent, interpolated_normal);
	normal_tangent_space.z *= max(1.0e-10f, mean_tangent_length);
	result.normal = normalize(tangent_to_world_space * normal_tangent_space);
	// Perform local shading normal adaptation to avoid that the view direction
	// is below the horizon. Inspired by Keller et al., Section A.3, but
	// different since the method of Keller et al. often led to normal vectors
	// "running off to the side." We simply clip the shading normal into the
	// hemisphere of the outgoing direction.
	// https://arxiv.org/abs/1705.01263
	result.outgoing = normalize(g_camera_position_world_space - result.position);
	float normal_offset = max(0.0f, 1.0e-3f - dot(result.normal, result.outgoing));
	result.normal = fma(vec3(normal_offset), result.outgoing, result.normal);
	result.normal = normalize(result.normal);
	result.lambert_outgoing = dot(result.normal, result.outgoing);
	return result;
}

void main() {
	// Obtain an integer pixel index
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	// Get the primitive index from the visibility buffer
	uint primitive_index = subpassLoad(g_visibility_buffer).r;
	// Set the background color
	vec3 final_color = vec3(0.0f);
	// Figure out the ray to the first visible surface
	vec3 view_ray_direction = g_pixel_to_ray_direction_world_space * vec3(pixel, 1.0f);
	vec4 view_ray_end;
	shading_data_t shading_data;
	if (primitive_index == 0xFFFFFFFF)
		view_ray_end = vec4(view_ray_direction, 0.0f);
	else {
		// Prepare shading data for the visible surface point
		shading_data = get_shading_data(pixel, int(primitive_index), view_ray_direction);
		view_ray_end = vec4(shading_data.position, 1.0f);
#if SHOW_LINEAR_LIGHTS
	}
	// Display light sources
	view_ray_direction = normalize(view_ray_direction);
	for (uint i = 0; i != LINEAR_LIGHT_COUNT; ++i)
		final_color += render_linear_light(g_linear_lights[i], 0.025f, g_camera_position_world_space, view_ray_direction, view_ray_end);
	// We only need to shade anything if there is a primitive to shade
	if (primitive_index != 0xFFFFFFFF) {
#endif
		// Get ready to use linearly transformed cosines
		float fresnel_luminance = dot(shading_data.fresnel_0, vec3(0.2126f, 0.7152f, 0.0722f));
		ltc_coefficients_t ltc = get_ltc_coefficients(fresnel_luminance, shading_data.roughness, shading_data.position, shading_data.normal, shading_data.outgoing, g_ltc_constants);
		// Prepare noise for all sampling decisions
		noise_accessor_t noise_accessor = get_noise_accessor(pixel, g_noise_resolution_mask, g_noise_texture_index_mask, g_noise_random_numbers);
		// Shade with all linear lights
		RAY_TRACING_FOR_LOOP(i, LINEAR_LIGHT_COUNT, LINEAR_LIGHT_COUNT_CLAMPED,
			final_color += evaluate_linear_light_shading(shading_data, ltc, g_linear_lights[i], noise_accessor);
		)
	}
	// If there are NaNs or INFs, we want to know. Make them pink.
	if (isnan(final_color.r) || isnan(final_color.g) || isnan(final_color.b)
		|| isinf(final_color.r) || isinf(final_color.g) || isinf(final_color.b))
		final_color = vec3(1.0f, 0.0f, 0.8f) / g_exposure_factor;
	// Output the result of shading
	g_out_color = vec4(final_color * g_exposure_factor, 1.0f);
	// Here is how we support HDR screenshots: Always rendering to an
	// intermediate HDR render target would be wasting resources, since we do
	// not take screenshots each frame. Instead, a HDR screenshot consists of
	// two LDR screenshots holding different bits of halfs.
	if (g_frame_bits > 0) {
		uint mask = (g_frame_bits == 1) ? 0xFF : 0xFF00;
		uint shift = (g_frame_bits == 1) ? 0 : 8;
		uvec2 half_bits = uvec2(packHalf2x16(g_out_color.rg), packHalf2x16(g_out_color.ba));
		g_out_color = vec4(
			((half_bits[0] & mask) >> shift) * (1.0f / 255.0f),
			((((half_bits[0] & 0xFFFF0000) >> 16) & mask) >> shift) * (1.0f / 255.0f),
			((half_bits[1] & mask) >> shift) * (1.0f / 255.0f),
			1.0f
		);
		// We just want to write bits to the render target, not colors. If the
		// graphics pipeline does linear to sRGB conversion for us, we do sRGB
		// to linear conversion here to counter that.
#if OUTPUT_LINEAR_RGB
		g_out_color.rgb = convert_srgb_to_linear_rgb(g_out_color.rgb);
#endif
	}
#if !OUTPUT_LINEAR_RGB
	// if g_frame_bits == 0, we output linear RGB or sRGB as requested
	else
		g_out_color.rgb = convert_linear_rgb_to_srgb(g_out_color.rgb);
#endif
}
