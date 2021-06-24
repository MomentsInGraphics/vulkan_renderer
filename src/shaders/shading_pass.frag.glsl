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
#extension GL_EXT_control_flow_attributes : enable
#if TRACE_SHADOW_RAYS
#extension GL_EXT_ray_query : enable
#endif
#include "noise_utility.glsl"
#include "brdfs.glsl"
#include "mesh_quantization.glsl"
//#include "polygon_sampling.glsl" via polygon_sampling_related_work.glsl
#include "polygon_sampling_related_work.glsl"
#include "polygon_clipping.glsl"
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

//! Textures for each polygonal light. These can be plane space textures, light
//! probes or IES profiles
layout (binding = 8) uniform sampler2D g_light_textures[LIGHT_TEXTURE_COUNT];

//! The top-level acceleration structure that contains all shadow-casting
//! geometry
#if TRACE_SHADOW_RAYS
layout(binding = 9, set = 0) uniform accelerationStructureEXT g_top_level_acceleration_structure;
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


/*! If shadow rays are enabled, this function traces a shadow ray towards the
	given polygonal light and updates visibility accordingly. If visibility is
	false already, no ray is traced. The ray direction must be normalized.*/
void get_polygon_visibility(inout bool visibility, vec3 sampled_dir, vec3 shading_position, polygonal_light_t polygonal_light) {
#if TRACE_SHADOW_RAYS
	if (visibility) {
		float max_t = -dot(vec4(shading_position, 1.0f), polygonal_light.plane) / dot(sampled_dir, polygonal_light.plane.xyz);
		float min_t = 1.0e-3f;
		// Perform a ray query and wait for it to finish. One call to
		// rayQueryProceedEXT() should be enough because of
		// gl_RayFlagsTerminateOnFirstHitEXT.
		rayQueryEXT ray_query;
		rayQueryInitializeEXT(ray_query, g_top_level_acceleration_structure,
			gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,
			0xFF, shading_position, min_t, sampled_dir, max_t);
		rayQueryProceedEXT(ray_query);
		// Update the visibility
		bool occluder_hit = (rayQueryGetIntersectionTypeEXT(ray_query, true) != gl_RayQueryCommittedIntersectionNoneEXT);
		visibility = !occluder_hit;
	}
#endif
}


/*! Determines the radiance received from the given direction due to the given
	polygonal light (ignoring visibility).
	\param sampled_dir The normalized direction from the shading point to the
		light source in world space. It must be chosen such that the ray
		actually intersects the polygon (possibly behind an occluder or below
		the horizon).
	\param shading_position The location of the shading point.
	\param polygonal_light The light source for which the incoming radiance is
		evaluated.
	\return Received radiance.*/
vec3 get_polygon_radiance(vec3 sampled_dir, vec3 shading_position, polygonal_light_t polygonal_light) {
	vec3 radiance = polygonal_light.surface_radiance;
	uint technique = polygonal_light.texturing_technique;
	if (technique != polygon_texturing_none) {
		vec2 tex_coord;
		if (technique == polygon_texturing_area) {
			// Intersect the ray with the plane of the light source
			float intersection_t = -dot(vec4(shading_position, 1.0f), polygonal_light.plane) / dot(sampled_dir, polygonal_light.plane.xyz);
			vec3 intersection = shading_position + intersection_t * sampled_dir;
			// Transform to plane space
			intersection -= polygonal_light.translation;
			tex_coord = (transpose(polygonal_light.rotation) * intersection).xy;
			tex_coord *= vec2(polygonal_light.inv_scaling_x, polygonal_light.inv_scaling_y);
		}
		else {
			vec3 lookup_dir;
			if (technique == polygon_texturing_ies_profile) {
				// For IES profiles, we transform to plane space
				lookup_dir = transpose(polygonal_light.rotation) * sampled_dir;
				// IES profiles already include this cosine term, so we divide
				// it out
				radiance *= 1.0f / abs(lookup_dir.z);
			}
			else
				// This code is designed to be compatible with the coordinate
				// system used for HDRI Haven light probes
				lookup_dir = vec3(-sampled_dir.x, sampled_dir.y, sampled_dir.z);
			// Now we compute spherical coordinates
			tex_coord.x = atan(lookup_dir.y, lookup_dir.x) * (0.5f * M_INV_PI);
			tex_coord.y = acos(lookup_dir.z) * M_INV_PI;
		}
		radiance *= textureLod(g_light_textures[polygonal_light.texture_index], tex_coord, 0.0f).rgb;
	}
	return radiance;
}


/*! Determines the radiance received from the given direction due to the given
	polygonal light and multiplies it by the BRDF for this direction. If
	necessary, this function traces a shadow ray to determine visibility.
	\param lambert The dot product of the normal vector and the given
		direction, in case you have use for it outside this function.
	\param sampled_dir The normalized direction from the shading point to the
		light source in world space. It must be chosen such that the ray
		actually intersects the polygon (possibly behind an occluder or below
		the horizon).
	\param shading_data Shading data for the shading point.
	\param polygonal_light The light source for which the incoming radiance is
		evaluated.
	\param diffuse Whether the diffuse BRDF component is evaluated.
	\param specular Whether the specular BRDF component is evaluated.
	\return BRDF times incoming radiance times visibility.*/
vec3 get_polygon_radiance_visibility_brdf_product(out float out_lambert, vec3 sampled_dir, shading_data_t shading_data, polygonal_light_t polygonal_light, bool diffuse, bool specular) {
	out_lambert = dot(shading_data.normal, sampled_dir);
	bool visibility = (out_lambert > 0.0f);
	get_polygon_visibility(visibility, sampled_dir, shading_data.position, polygonal_light);
	if (visibility) {
		vec3 radiance = get_polygon_radiance(sampled_dir, shading_data.position, polygonal_light);
		return radiance * evaluate_brdf(shading_data, sampled_dir, diffuse, specular);
	}
	else
		return vec3(0.0f);
}

//! Overload that evaluates diffuse and specular BRDF components
vec3 get_polygon_radiance_visibility_brdf_product(out float lambert, vec3 sampled_dir, shading_data_t shading_data, polygonal_light_t polygonal_light) {
	return get_polygon_radiance_visibility_brdf_product(lambert, sampled_dir, shading_data, polygonal_light, true, true);
}

//! Like get_polygon_radiance_visibility_brdf_product() but always evaluates
//! both BRDF components and also outputs the visibility term explicitly.
vec3 get_polygon_radiance_visibility_brdf_product(out bool out_visibility, vec3 sampled_dir, shading_data_t shading_data, polygonal_light_t polygonal_light) {
	out_visibility = (dot(shading_data.normal, sampled_dir) > 0.0f);
	get_polygon_visibility(out_visibility, sampled_dir, shading_data.position, polygonal_light);
	if (out_visibility) {
		vec3 radiance = get_polygon_radiance(sampled_dir, shading_data.position, polygonal_light);
		return radiance * evaluate_brdf(shading_data, sampled_dir, true, true);
	}
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
	MIS heuristic. It supports our weighted balance heuristic and optimal MIS.
	\param visibility true iff the given sample is occluded.
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
vec3 get_mis_estimate(bool visibility, vec3 integrand, vec3 sampled_weight, float sampled_density, vec3 other_weight, float other_density, float visibility_estimate) {
#if MIS_HEURISTIC_WEIGHTED
	vec3 weighted_sum = sampled_weight * sampled_density + other_weight * other_density;
	return (sampled_weight * integrand) / weighted_sum;

#elif MIS_HEURISTIC_OPTIMAL_CLAMPED || MIS_HEURISTIC_OPTIMAL
	float balance_weight_over_density = 1.0f / (sampled_density + other_density);
	vec3 weighted_sum = sampled_weight * sampled_density + other_weight * other_density;
#if MIS_HEURISTIC_OPTIMAL_CLAMPED
	vec3 weighted_weight_over_density = sampled_weight / weighted_sum;
	vec3 mixed_weight_over_density = vec3(fma(-visibility_estimate, balance_weight_over_density, balance_weight_over_density));
	mixed_weight_over_density = fma(vec3(visibility_estimate), weighted_weight_over_density, vec3(mixed_weight_over_density));
	// For visible samples, we use the actual integrand
	vec3 visible_estimate = mixed_weight_over_density * integrand;
	return visible_estimate;

#elif MIS_HEURISTIC_OPTIMAL
	return visibility_estimate * sampled_weight + balance_weight_over_density * (integrand - visibility_estimate * weighted_sum);
#endif

#else
	return get_mis_weight_over_density(sampled_density, other_density) * integrand;
#endif
}


/*! Returns the Monte Carlo estimate of the lighting contribution of a
	polygonal light. Most parameters forward to
	get_polygon_radiance_visibility_brdf_product(). If enabled, this method
	implements multiple importance sampling with importance sampling of the
	visible normal distribution. Thus, the given sample must be generated by
	next event estimation, i.e. as direction towards the light source.
	\param sampled_density The density of sampled_dir with respect to the solid
		angle measure.
	\see get_polygon_radiance_visibility_brdf_product() */
vec3 get_polygonal_light_mis_estimate(vec3 sampled_dir, float sampled_density, shading_data_t shading_data, polygonal_light_t polygonal_light) {
	float lambert;
	vec3 radiance_times_brdf = get_polygon_radiance_visibility_brdf_product(lambert, sampled_dir, shading_data, polygonal_light);

#if SAMPLING_STRATEGIES_DIFFUSE_ONLY

	// If the density is exactly zero, that must also be true for the integrand
	return (sampled_density > 0.0f) ? (radiance_times_brdf * (lambert / sampled_density)) : vec3(0.0f);

#elif SAMPLING_STRATEGIES_DIFFUSE_GGX_MIS

	float ggx_density = get_ggx_reflected_direction_density(shading_data.lambert_outgoing, shading_data.outgoing, sampled_dir, shading_data.normal, shading_data.roughness);
	return radiance_times_brdf * lambert * get_mis_weight_over_density(sampled_density, ggx_density);

#else
	// The method is not suitable when LTC importance sampling is involved
	return vec3(0.0f);
#endif
}


/*! Takes samples from the given polygonal light to compute shading. The number
	of samples and sampling techniques are determined by defines.
	\return The color that arose from shading.*/
vec3 evaluate_polygonal_light_shading(shading_data_t shading_data, ltc_coefficients_t ltc, polygonal_light_t polygonal_light, inout noise_accessor_t accessor) {
	vec3 result = vec3(0.0f);

#if SAMPLE_POLYGON_BASELINE
	vec3 corner_offset = polygonal_light.translation - shading_data.position;
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		// This technique is broken. Its purpose is to make sure that random
		// numbers are still consumed and the BRDF is still evaluated while the
		// cost for sampling is negligible. Useful as baseline in run time
		// measurements.
		vec2 random_numbers = get_noise_2(accessor);
		vec3 diffuse_dir = normalize(corner_offset + random_numbers[0] * polygonal_light.rotation[0] + random_numbers[1] * polygonal_light.rotation[1]);
		result += get_polygonal_light_mis_estimate(diffuse_dir, 1.0f, shading_data, polygonal_light);
	)

#elif SAMPLE_POLYGON_AREA_TURK
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		vec3 light_sample = sample_area_polygon_turk(polygonal_light.vertex_count, polygonal_light.vertices_world_space, polygonal_light.fan_areas, get_noise_2(accessor));
		vec3 diffuse_dir;
		float density = get_area_sample_density(diffuse_dir, light_sample, shading_data.position, polygonal_light.plane.xyz, polygonal_light.area);
		result += get_polygonal_light_mis_estimate(diffuse_dir, density, shading_data, polygonal_light);
	)

#elif SAMPLE_POLYGON_RECTANGLE_SOLID_ANGLE_URENA
	// Prepare sampling
	solid_angle_rectangle_urena_t polygon_diffuse = prepare_solid_angle_rectangle_sampling_urena(
		polygonal_light.translation, polygonal_light.scaling_x * polygonal_light.rotation[0], polygonal_light.scaling_y * polygonal_light.rotation[1],
		polygonal_light.scaling_x, polygonal_light.scaling_y, polygonal_light.rotation, shading_data.position);
	// Perform sampling
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		vec3 diffuse_dir = sample_solid_angle_rectangle_urena(polygon_diffuse, get_noise_2(accessor));
		float density = 1.0f / polygon_diffuse.solid_angle;
		result += get_polygonal_light_mis_estimate(diffuse_dir, density, shading_data, polygonal_light);
	)

#elif SAMPLE_POLYGON_SOLID_ANGLE_ARVO
	// Prepare sampling
	solid_angle_polygon_arvo_t polygon_diffuse = prepare_solid_angle_polygon_sampling_arvo(
		polygonal_light.vertex_count, polygonal_light.vertices_world_space, shading_data.position);
	// Perform sampling
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		vec3 diffuse_dir = sample_solid_angle_polygon_arvo(polygon_diffuse, get_noise_2(accessor));
		float density = 1.0f / polygon_diffuse.solid_angle;
		result += get_polygonal_light_mis_estimate(diffuse_dir, density, shading_data, polygonal_light);
	)

#elif SAMPLE_POLYGON_SOLID_ANGLE
	// Prepare sampling
	solid_angle_polygon_t polygon_diffuse = prepare_solid_angle_polygon_sampling(
		polygonal_light.vertex_count, polygonal_light.vertices_world_space, shading_data.position);
	// Perform sampling
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		vec3 diffuse_dir = sample_solid_angle_polygon(polygon_diffuse, get_noise_2(accessor));
		float density = 1.0f / polygon_diffuse.solid_angle;
		result += get_polygonal_light_mis_estimate(diffuse_dir, density, shading_data, polygonal_light);
	)

#elif SAMPLE_POLYGON_CLIPPED_SOLID_ANGLE \
	|| SAMPLE_POLYGON_BILINEAR_COSINE_WARP_HART || SAMPLE_POLYGON_BILINEAR_COSINE_WARP_CLIPPING_HART \
	|| SAMPLE_POLYGON_BIQUADRATIC_COSINE_WARP_HART || SAMPLE_POLYGON_BIQUADRATIC_COSINE_WARP_CLIPPING_HART

	// Transform to shading space and clip if necessary
	vec3 vertices_shading_space[MAX_POLYGON_VERTEX_COUNT];
	[[unroll]]
	for (uint i = 0; i != MAX_POLYGONAL_LIGHT_VERTEX_COUNT; ++i)
		vertices_shading_space[i] = ltc.world_to_shading_space * vec4(polygonal_light.vertices_world_space[i], 1.0f);
#if SAMPLE_POLYGON_BILINEAR_COSINE_WARP_HART || SAMPLE_POLYGON_BIQUADRATIC_COSINE_WARP_HART
	uint clipped_vertex_count = polygonal_light.vertex_count;
#else
	uint clipped_vertex_count = clip_polygon(polygonal_light.vertex_count, vertices_shading_space);
	if (clipped_vertex_count == 0)
		return vec3(0.0f);
#endif

#if SAMPLE_POLYGON_CLIPPED_SOLID_ANGLE
	// Prepare sampling
	solid_angle_polygon_t polygon_diffuse = prepare_solid_angle_polygon_sampling(
		clipped_vertex_count, vertices_shading_space, vec3(0.0f));
	// Perform sampling
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		vec3 diffuse_dir = sample_solid_angle_polygon(polygon_diffuse, get_noise_2(accessor));
		diffuse_dir = (transpose(ltc.world_to_shading_space) * diffuse_dir).xyz;
		float density = 1.0f / polygon_diffuse.solid_angle;
		result += get_polygonal_light_mis_estimate(diffuse_dir, density, shading_data, polygonal_light);
	)

#elif SAMPLE_POLYGON_BILINEAR_COSINE_WARP_HART || SAMPLE_POLYGON_BILINEAR_COSINE_WARP_CLIPPING_HART
	// Prepare sampling
	bilinear_cosine_warp_polygon_hart_t polygon_diffuse = prepare_bilinear_cosine_warp_polygon_sampling_hart(
		clipped_vertex_count, vertices_shading_space);
	// Perform sampling
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		float density;
		vec3 diffuse_dir = sample_bilinear_cosine_warp_polygon_hart(density, polygon_diffuse, get_noise_2(accessor));
		diffuse_dir = (transpose(ltc.world_to_shading_space) * diffuse_dir).xyz;
		result += get_polygonal_light_mis_estimate(diffuse_dir, density, shading_data, polygonal_light);
	)

#elif SAMPLE_POLYGON_BIQUADRATIC_COSINE_WARP_HART || SAMPLE_POLYGON_BIQUADRATIC_COSINE_WARP_CLIPPING_HART
	// Prepare sampling
	biquadratic_cosine_warp_polygon_hart_t polygon_diffuse = prepare_biquadratic_cosine_warp_polygon_sampling_hart(
		clipped_vertex_count, vertices_shading_space);
	// Perform sampling
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		float density;
		vec3 diffuse_dir = sample_biquadratic_cosine_warp_polygon_hart(density, polygon_diffuse, get_noise_2(accessor));
		diffuse_dir = (transpose(ltc.world_to_shading_space) * diffuse_dir).xyz;
		result += get_polygonal_light_mis_estimate(diffuse_dir, density, shading_data, polygonal_light);
	)

#endif

#elif SAMPLE_POLYGON_PROJECTED_SOLID_ANGLE || SAMPLE_POLYGON_PROJECTED_SOLID_ANGLE_ARVO
	// If the shading point is on the wrong side of the polygon, we get a
	// correct winding by flipping the orientation of the shading space
	float side = dot(vec4(shading_data.position, 1.0f), polygonal_light.plane);
	[[unroll]]
	for (uint i = 0; i != 4; ++i) {
		ltc.world_to_shading_space[i][1] = (side < 0.0f) ? -ltc.world_to_shading_space[i][1] : ltc.world_to_shading_space[i][1];
		ltc.world_to_cosine_space[i][1] = (side < 0.0f) ? -ltc.world_to_cosine_space[i][1] : ltc.world_to_cosine_space[i][1];
	}

#if SAMPLING_STRATEGIES_DIFFUSE_ONLY || SAMPLING_STRATEGIES_DIFFUSE_GGX_MIS
	// Transform to shading space
	vec3 vertices_shading_space[MAX_POLYGON_VERTEX_COUNT];
	[[unroll]]
	for (uint i = 0; i != MAX_POLYGONAL_LIGHT_VERTEX_COUNT; ++i)
		vertices_shading_space[i] = ltc.world_to_shading_space * vec4(polygonal_light.vertices_world_space[i], 1.0f);
	// Clip
	uint clipped_vertex_count = clip_polygon(polygonal_light.vertex_count, vertices_shading_space);
	if (clipped_vertex_count == 0)
		return vec3(0.0f);

#if SAMPLE_POLYGON_PROJECTED_SOLID_ANGLE_ARVO
	// Prepare sampling
	projected_solid_angle_polygon_arvo_t polygon_diffuse = prepare_projected_solid_angle_polygon_sampling_arvo(
		clipped_vertex_count, vertices_shading_space);
	if (polygon_diffuse.projected_solid_angle <= 0.0f)
		return vec3(0.0f);
#if ERROR_DISPLAY_DIFFUSE
	vec2 random_numbers = get_noise_2(accessor);
	vec3 sampled_dir = sample_projected_solid_angle_polygon_arvo(polygon_diffuse, random_numbers, 3);
	float error = compute_projected_solid_angle_polygon_sampling_error_arvo(polygon_diffuse, random_numbers, sampled_dir)[ERROR_INDEX];
	return error_to_color(error) / g_exposure_factor;
#else
	// Perform sampling
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		vec3 diffuse_dir = sample_projected_solid_angle_polygon_arvo(polygon_diffuse, get_noise_2(accessor), 3);
		float density = diffuse_dir.z / polygon_diffuse.projected_solid_angle;
		diffuse_dir = (transpose(ltc.world_to_shading_space) * diffuse_dir).xyz;
		result += get_polygonal_light_mis_estimate(diffuse_dir, density, shading_data, polygonal_light);
	)
#endif

#elif SAMPLE_POLYGON_PROJECTED_SOLID_ANGLE
	// Prepare sampling
	projected_solid_angle_polygon_t polygon_diffuse = prepare_projected_solid_angle_polygon_sampling(
		clipped_vertex_count, vertices_shading_space);
	if (polygon_diffuse.projected_solid_angle <= 0.0f)
		return vec3(0.0f);
#if ERROR_DISPLAY_DIFFUSE
	vec2 random_numbers = get_noise_2(accessor);
	vec3 sampled_dir = sample_projected_solid_angle_polygon(polygon_diffuse, random_numbers);
	float error = compute_projected_solid_angle_polygon_sampling_error(polygon_diffuse, random_numbers, sampled_dir)[ERROR_INDEX];
	return error_to_color(error) / g_exposure_factor;
#else
	// Perform sampling
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		vec3 diffuse_dir = sample_projected_solid_angle_polygon(polygon_diffuse, get_noise_2(accessor));
		float density = diffuse_dir.z / polygon_diffuse.projected_solid_angle;
		diffuse_dir = (transpose(ltc.world_to_shading_space) * diffuse_dir).xyz;
		result += get_polygonal_light_mis_estimate(diffuse_dir, density, shading_data, polygonal_light);
	)
#endif

#endif

#else // Combined diffuse and specular strategies

	// Instruction cache misses are a concern. Thus, we strive to keep the code
	// small by preparing the diffuse (i==0) and specular (i==1) sampling
	// strategies in the same loop.
	projected_solid_angle_polygon_t polygon_diffuse;
	projected_solid_angle_polygon_t polygon_specular;
	[[dont_unroll]]
	for (uint i = 0; i != 2; ++i) {
		// Local space is either shading space (for the diffuse technique) or
		// cosine space (for the specular technique)
		mat4x3 world_to_local_space = (i == 0) ? ltc.world_to_shading_space : ltc.world_to_cosine_space;
		if (i > 0)
			// We put this object in the wrong place at first to avoid move
			// instructions
			polygon_diffuse = polygon_specular;
		// Transform to local space
		vec3 vertices_local_space[MAX_POLYGON_VERTEX_COUNT];
		[[unroll]]
		for (uint j = 0; j != MAX_POLYGONAL_LIGHT_VERTEX_COUNT; ++j)
			vertices_local_space[j] = world_to_local_space * vec4(polygonal_light.vertices_world_space[j], 1.0f);
		// Clip
		uint clipped_vertex_count = clip_polygon(polygonal_light.vertex_count, vertices_local_space);
		if (clipped_vertex_count == 0 && i == 0)
			// The polygon is completely below the horizon
			return vec3(0.0f);
		else if (clipped_vertex_count == 0) {
			// The linearly transformed cosine is zero on the polygon
			polygon_specular.projected_solid_angle = 0.0f;
			break;
		}
		// Prepare sampling
		polygon_specular = prepare_projected_solid_angle_polygon_sampling(clipped_vertex_count, vertices_local_space);
	}
	// Even when something remains after clipping, the projected solid angle
	// may still underflow
	if (polygon_diffuse.projected_solid_angle == 0.0f)
		return vec3(0.0f);
	// Compute the importance of the specular sampling technique using an
	// LTC-based estimate of unshadowed shading
	float specular_albedo = ltc.albedo;
	float specular_weight = specular_albedo * polygon_specular.projected_solid_angle;

#if ERROR_DISPLAY_DIFFUSE
	vec2 random_numbers = get_noise_2(accessor);
	vec3 sampled_dir = sample_projected_solid_angle_polygon(polygon_diffuse, random_numbers);
	float error = compute_projected_solid_angle_polygon_sampling_error(polygon_diffuse, random_numbers, sampled_dir)[ERROR_INDEX];
	return error_to_color(error) / g_exposure_factor;

#elif ERROR_DISPLAY_SPECULAR
	if (polygon_specular.projected_solid_angle > 0.0f) {
		vec2 random_numbers = get_noise_2(accessor);
		vec3 sampled_dir = sample_projected_solid_angle_polygon(polygon_specular, random_numbers);
		float error = compute_projected_solid_angle_polygon_sampling_error(polygon_specular, random_numbers, sampled_dir)[ERROR_INDEX];
		return error_to_color(error) / g_exposure_factor;
	}
	else
		return vec3(0.0f);

#elif SAMPLING_STRATEGIES_DIFFUSE_SPECULAR_SEPARATELY

	// Take the requested number of samples with both techniques
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		// Take a diffuse sample and accumulate the diffuse BRDF
		vec3 diffuse_dir = sample_projected_solid_angle_polygon(polygon_diffuse, get_noise_2(accessor));
		diffuse_dir = (transpose(ltc.world_to_shading_space) * diffuse_dir).xyz;
		float lambert;
		vec3 radiance_times_brdf = get_polygon_radiance_visibility_brdf_product(lambert, diffuse_dir, shading_data, polygonal_light, true, false);
		result += radiance_times_brdf * polygon_diffuse.projected_solid_angle;
		if (polygon_specular.projected_solid_angle > 0.0f) {
			// Take a specular sample
			vec3 dir_cosine_space = sample_projected_solid_angle_polygon(polygon_specular, get_noise_2(accessor));
			// Transform to shading space and compute the LTC density
			vec3 dir_shading_space = normalize(ltc.cosine_to_shading_space * dir_cosine_space);
			float ltc_density = evaluate_ltc_density(ltc, dir_shading_space, 1.0f);
			// Evaluate radiance and specular BRDF and accumulate in the result
			float lambert;
			vec3 radiance_times_brdf = get_polygon_radiance_visibility_brdf_product(lambert, (transpose(ltc.world_to_shading_space) * dir_shading_space).xyz, shading_data, polygonal_light, false, true);
			result += (dir_shading_space.z <= 0.0f || dir_cosine_space.z <= 0.0f) ? vec3(0.0f) : (radiance_times_brdf * dir_shading_space.z * polygon_specular.projected_solid_angle / ltc_density);
		}
	)

#elif SAMPLING_STRATEGIES_DIFFUSE_SPECULAR_MIS

	// Compute the importance of the diffuse sampling technique using the
	// diffuse albedo and the projected solid angle. Zero albedo is forbidden
	// because we need a non-zero weight for diffuse samples in parts where the
	// LTC is zero but the specular BRDF is not. Thus, we clamp.
	vec3 diffuse_albedo = max(shading_data.diffuse_albedo, vec3(0.01f));
	vec3 diffuse_weight = diffuse_albedo * polygon_diffuse.projected_solid_angle;
	uint technique_count = (polygon_specular.projected_solid_angle > 0.0f) ? 2 : 1;
	float rcp_diffuse_projected_solid_angle = 1.0f / polygon_diffuse.projected_solid_angle;
	float rcp_specular_projected_solid_angle = 1.0f / polygon_specular.projected_solid_angle;
	vec3 specular_weight_rgb = vec3(specular_weight);
	// For optimal MIS, constant factors in the diffuse and specular weight
	// matter
#if MIS_HEURISTIC_OPTIMAL
	vec3 radiance_over_pi = polygonal_light.surface_radiance * M_INV_PI;
	diffuse_weight *= radiance_over_pi;
	specular_weight_rgb *= radiance_over_pi;
#endif
	// Take the requested number of samples with both techniques
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		// Take the samples
		vec3 dir_shading_space_diffuse = sample_projected_solid_angle_polygon(polygon_diffuse, get_noise_2(accessor));
		vec3 dir_shading_space_specular;
		if (polygon_specular.projected_solid_angle > 0.0f) {
			dir_shading_space_specular = sample_projected_solid_angle_polygon(polygon_specular, get_noise_2(accessor));
			dir_shading_space_specular = normalize(ltc.cosine_to_shading_space * dir_shading_space_specular);
		}
		[[dont_unroll]]
		for (uint j = 0; j != technique_count; ++j) {
			vec3 dir_shading_space = (j == 0) ? dir_shading_space_diffuse : dir_shading_space_specular;
			if (dir_shading_space.z <= 0.0f) continue;
			// Compute the densities for the sample with respect to both
			// sampling techniques (w.r.t. solid angle measure)
			float diffuse_density = dir_shading_space.z * rcp_diffuse_projected_solid_angle;
			float specular_density = evaluate_ltc_density(ltc, dir_shading_space, rcp_specular_projected_solid_angle);
			// Evaluate radiance and BRDF and the integrand as a whole
			bool visibility;
			vec3 integrand = dir_shading_space.z * get_polygon_radiance_visibility_brdf_product(visibility, (transpose(ltc.world_to_shading_space) * dir_shading_space).xyz, shading_data, polygonal_light);
			// Use the appropriate MIS heuristic to turn the sample into a
			// splat and accummulate
			if (j == 0 && polygon_specular.projected_solid_angle <= 0.0f)
				// We only have one sampling technique, so no MIS is needed
				result += visibility ? (integrand * (1.0f / diffuse_density)) : vec3(0.0f);
			else if (j == 0)
				result += get_mis_estimate(visibility, integrand, diffuse_weight, diffuse_density, specular_weight_rgb, specular_density, g_mis_visibility_estimate);
			else
				result += get_mis_estimate(visibility, integrand, specular_weight_rgb, specular_density, diffuse_weight, diffuse_density, g_mis_visibility_estimate);
		}
	)

#elif SAMPLING_STRATEGIES_DIFFUSE_SPECULAR_RANDOM

	// Compute the importance of the diffuse sampling technique using the
	// luminance of the diffuse albedo and the projected solid angle
	const vec3 luminance_weights = vec3(0.21263901f, 0.71516868f, 0.07219232f);
	float diffuse_albedo = max(dot(shading_data.diffuse_albedo, luminance_weights), 0.01f);
	float diffuse_weight = diffuse_albedo * polygon_diffuse.projected_solid_angle;
	float diffuse_ratio = diffuse_weight / (diffuse_weight + specular_weight);
	// Take the requested number of samples selecting the technique randomly
	projected_solid_angle_polygon_t polygon_selected;
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		// Select the sampling technique randomly
		vec2 random_numbers = get_noise_2(accessor);
		bool specular_selected = random_numbers[0] >= diffuse_ratio;
		float random_number_offset = specular_selected ? 1.0f : 0.0f;
		random_numbers[0] = (random_numbers[0] - random_number_offset) / (diffuse_ratio - random_number_offset);
		polygon_selected = specular_selected ? polygon_specular : polygon_diffuse;
		// Take a sample
		vec3 dir_shading_space = sample_projected_solid_angle_polygon(polygon_selected, random_numbers);
		// Transform back to shading space
		if (specular_selected)
			dir_shading_space = normalize(ltc.cosine_to_shading_space * dir_shading_space);
		// Compute the density for both sampling techniques (each scaled by the
		// respective weight)
		float lambert = dir_shading_space.z;
		float diffuse_density = lambert * diffuse_albedo;
		float specular_density = evaluate_ltc_density(ltc, dir_shading_space, specular_albedo);
		float density = (diffuse_density + specular_density) / (diffuse_weight + specular_weight);
		// Do the shading (in world space)
		vec3 radiance_times_brdf = get_polygon_radiance_visibility_brdf_product(lambert, (transpose(ltc.world_to_shading_space) * dir_shading_space).xyz, shading_data, polygonal_light);
		result += (dir_shading_space.z <= 0.0f) ? vec3(0.0f) : (radiance_times_brdf * dir_shading_space.z / density);
	)

#endif
#endif
#endif

#if SAMPLING_STRATEGIES_DIFFUSE_GGX_MIS

	// Transform the outgoing light direction to shading space. By construction
	// the y-coordinate is zero.
	vec3 outgoing_shading_space = ltc.world_to_shading_space * vec4(shading_data.outgoing, 0.0f);
	outgoing_shading_space.y = 0.0f;
#if SAMPLE_POLYGON_PROJECTED_SOLID_ANGLE || SAMPLE_POLYGON_PROJECTED_SOLID_ANGLE_ARVO
	float density_factor = 1.0f / polygon_diffuse.projected_solid_angle;
#else
	float density_factor = 1.0f / polygon_diffuse.solid_angle;
#endif
	// Take the requested number of samples
	RAY_TRACING_FOR_LOOP(i, SAMPLE_COUNT, SAMPLE_COUNT_CLAMPED,
		// Take a sample approximately in proportion to the GGX specular BRDF
		float ggx_density;
		vec3 dir_shading_space_ggx = sample_ggx_reflected_direction(ggx_density, outgoing_shading_space, shading_data.roughness, get_noise_2(accessor));
		vec3 dir_world_space_ggx = (transpose(ltc.world_to_shading_space) * dir_shading_space_ggx).xyz;
		// Check if the ray hits the light source at all. Ideally, we would
		// gather contributions from all light sources here but doing so has
		// implications for many aspects of the renderer making the code
		// considerably more complicated. Since we only need this variant for
		// comparisons using a single light source, we keep it simple instead.
		if (dir_shading_space_ggx.z > 0.0f && polygonal_light_ray_intersection(polygonal_light, shading_data.position, vec4(dir_world_space_ggx, 0.0f))) {
			// Get the incoming radiance, multiplied by the BRDF
			float lambert;
			vec3 radiance_times_brdf = get_polygon_radiance_visibility_brdf_product(lambert, dir_world_space_ggx, shading_data, polygonal_light);
			// Evaluate the density for the polygonal light sampling technique
			float polygon_density = (SAMPLE_POLYGON_PROJECTED_SOLID_ANGLE != 0) ? (lambert * density_factor) : density_factor;
			// Now compute the contribution using multiple importance sampling
			result += radiance_times_brdf * lambert * get_mis_weight_over_density(ggx_density, polygon_density);
		}
	)

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
	vec3 base_color = textureGrad(g_material_textures[3 * material_index + 0], tex_coord, tex_coord_derivs[0], tex_coord_derivs[1]).rgb;
	vec3 specular_data = textureGrad(g_material_textures[3 * material_index + 1], tex_coord, tex_coord_derivs[0], tex_coord_derivs[1]).rgb;
	vec3 normal_tangent_space;
	normal_tangent_space.xy = textureGrad(g_material_textures[3 * material_index + 2], tex_coord, tex_coord_derivs[0], tex_coord_derivs[1]).rg;
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
#if SHOW_POLYGONAL_LIGHTS
	}
	// Display light sources
	view_ray_direction = normalize(view_ray_direction);
	for (uint i = 0; i != POLYGONAL_LIGHT_COUNT; ++i)
		if (polygonal_light_ray_intersection(g_polygonal_lights[i], g_camera_position_world_space, view_ray_end))
			final_color += get_polygon_radiance(view_ray_direction, g_camera_position_world_space, g_polygonal_lights[i]);
	// We only need to shade anything if there is a primitive to shade
	if (primitive_index != 0xFFFFFFFF) {
#endif
		// Get ready to use linearly transformed cosines
		float fresnel_luminance = dot(shading_data.fresnel_0, vec3(0.2126f, 0.7152f, 0.0722f));
		ltc_coefficients_t ltc = get_ltc_coefficients(fresnel_luminance, shading_data.roughness, shading_data.position, shading_data.normal, shading_data.outgoing, g_ltc_constants);
		// Prepare noise for all sampling decisions
		noise_accessor_t noise_accessor = get_noise_accessor(pixel, g_noise_resolution_mask, g_noise_texture_index_mask, g_noise_random_numbers);
		// Shade with all polygonal lights
		RAY_TRACING_FOR_LOOP(i, POLYGONAL_LIGHT_COUNT, POLYGONAL_LIGHT_COUNT_CLAMPED,
			final_color += evaluate_polygonal_light_shading(shading_data, ltc, g_polygonal_lights[i], noise_accessor);
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
