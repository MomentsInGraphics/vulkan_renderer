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


#include "main.h"
#include "string_utilities.h"
#include <stdlib.h>
#include <string.h>

void create_experiment_list(experiment_list_t* list) {
	memset(list, 0, sizeof(*list));
	// Allocate a fixed amount of space
	uint32_t count = 0;
	uint32_t size = 1000;
	experiment_t* experiments = malloc(size * sizeof(experiment_t));
	memset(experiments, 0, size * sizeof(experiment_t));
	// A mapping from polygon sampling techniques to names used in file names
	const char* sample_polygon_name[sample_polygon_count];
	sample_polygon_name[sample_polygon_baseline] = "baseline";
	sample_polygon_name[sample_polygon_area_turk] = "area_turk";
	sample_polygon_name[sample_polygon_rectangle_solid_angle_urena] = "rectangle_solid_angle_urena";
	sample_polygon_name[sample_polygon_solid_angle_arvo] = "solid_angle_arvo";
	sample_polygon_name[sample_polygon_solid_angle] = "solid_angle_ours";
	sample_polygon_name[sample_polygon_clipped_solid_angle] = "clipped_solid_angle_ours";
	sample_polygon_name[sample_polygon_bilinear_cosine_warp_hart] = "bilinear_cosine_warp_hart";
	sample_polygon_name[sample_polygon_bilinear_cosine_warp_clipping_hart] = "bilinear_cosine_warp_clipping_hart";
	sample_polygon_name[sample_polygon_biquadratic_cosine_warp_hart] = "biquadratic_cosine_warp_hart";
	sample_polygon_name[sample_polygon_biquadratic_cosine_warp_clipping_hart] = "biquadratic_cosine_warp_clipping_hart";
	sample_polygon_name[sample_polygon_projected_solid_angle_arvo] = "projected_solid_angle_arvo";
	sample_polygon_name[sample_polygon_projected_solid_angle] = "projected_solid_angle_ours";
	sample_polygon_name[sample_polygon_projected_solid_angle_biased] = "projected_solid_angle_biased_ours";

	// Set to VK_TRUE to run all experiments for generation of figures in the
	// paper (including some variants that are not in the paper)
	VkBool32 all_figs = VK_TRUE;
	// Set to VK_TRUE to create additional figures for the HTML viewer
	VkBool32 html_figs = VK_FALSE;
	// Set to VK_TRUE to run all experiments for generation of run time
	// measurements in the paper
	VkBool32 all_timings = VK_TRUE;
	// Set to VK_TRUE to take *.hdr screenshots (16-bit float stored as 32-bit
	// float) instead of *.png
	VkBool32 take_hdr_screenshots = VK_FALSE;

	// The attic scene with a wide variety of sampling techiques
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.mis_heuristic = mis_heuristic_balance, .mis_visibility_estimate = 0.5f,
			.error_min_exponent = -7.0f,
			.polygon_sampling_technique = sample_polygon_projected_solid_angle,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_TRUE, .show_polygonal_lights = VK_TRUE,
		};
		experiment_t attic_base = {
			.scene_index = scene_attic,
			.width = 1440, .height = 1440,
			.render_settings = settings_base
		};
		// Solid angle sampling MISed with GGX sampling
		experiments[count] = attic_base;
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_ggx_mis;
		experiments[count].render_settings.polygon_sampling_technique = sample_polygon_solid_angle;
		experiments[count].screenshot_path = copy_string("data/experiments/attic_solid_angle_and_ggx_mis_2spp_%.3f.png");
		++count;
		// Projected solid angle sampling MISed with GGX sampling
		experiments[count] = attic_base;
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_ggx_mis;
		experiments[count].screenshot_path = copy_string("data/experiments/attic_projected_solid_angle_ours_and_ggx_mis_2spp_%.3f.png");
		++count;
		// Projected solid angle sampling only but two samples
		experiments[count] = attic_base;
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_only;
		experiments[count].render_settings.sample_count = 2;
		experiments[count].screenshot_path = copy_string("data/experiments/attic_projected_solid_angle_ours_2spp_%.3f.png");
		++count;
		// Diffuse and specular sampling with our clamped optimal MIS
		experiments[count] = attic_base;
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_specular_mis;
		experiments[count].render_settings.mis_heuristic = mis_heuristic_optimal_clamped;
		experiments[count].screenshot_path = copy_string("data/experiments/attic_diffuse_and_specular_ours_clamped_optimal_mis_ours_2spp_%.3f.png");
		++count;
		// Reference
		experiments[count] = attic_base;
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_specular_mis;
		experiments[count].render_settings.sample_count = 64;
		experiments[count].screenshot_path = copy_string("data/experiments/attic_reference_128spp_%.3f.png");
		++count;
	}

	// Measure the error in the attic scene
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sample_polygon_projected_solid_angle,
			.polygon_sampling_technique = sample_polygon_projected_solid_angle,
			.error_min_exponent = -7.0f,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_FALSE, .show_polygonal_lights = VK_FALSE,
		};
		experiment_t attic_base = {
			.scene_index = scene_attic,
			.width = 1440, .height = 1440,
			.render_settings = settings_base
		};
		// Backward error
		experiments[count] = attic_base;
		experiments[count].render_settings.error_display = error_display_diffuse_backward;
		experiments[count].screenshot_path = copy_string("data/experiments/error_attic_backward_%.3f.png");
		++count;
		// Backward error times projected solid angle
		experiments[count] = attic_base;
		experiments[count].render_settings.error_display = error_display_diffuse_backward_scaled;
		experiments[count].screenshot_path = copy_string("data/experiments/error_attic_backward_times_psa_%.3f.png");
		++count;
	}

	// The bistro with small distant light sources
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.exposure_factor = 14.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_only,
			.polygon_sampling_technique = sample_polygon_projected_solid_angle,
			.error_min_exponent = -7.0f,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_TRUE, .show_polygonal_lights = VK_TRUE,
		};
		experiment_t small_polygon_base = {
			.scene_index = scene_bistro_outside,
			.width = 1920, .height = 1080,
			.render_settings = settings_base
		};
		// A small and a tiny light source
		const char* sizes[2] = { "small", "tiny" };
		for (uint32_t i = 0; i != COUNT_OF(sizes); ++i) {
			const char* size = sizes[i];
			const char* save_pieces[] = { "data/quicksaves/Bistro_outside_", size, "_light.save" };
			// Use each diffuse technique except clipping variants of Hart
			for (uint32_t j = 0; j != sample_polygon_count; ++j) {
				if (j == sample_polygon_bilinear_cosine_warp_clipping_hart || j == sample_polygon_biquadratic_cosine_warp_clipping_hart)
					continue;
				experiments[count] = small_polygon_base;
				const char* path_pieces[] = { "data/experiments/bistro_", size, "_polygon_", sample_polygon_name[j], "_1spp_%.3f.png" };
				experiments[count].screenshot_path = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
				experiments[count].quick_save_path = concatenate_strings(COUNT_OF(save_pieces), save_pieces);
				experiments[count].render_settings.polygon_sampling_technique = j;
				++count;
			}
			// Produce a reference with area sampling
			experiments[count] = small_polygon_base;
			const char* path_pieces[] = { "data/experiments/bistro_", size, "_polygon_reference_128spp_%.3f.png" };
			experiments[count].screenshot_path = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
			experiments[count].quick_save_path = concatenate_strings(COUNT_OF(save_pieces), save_pieces);
			experiments[count].render_settings.polygon_sampling_technique = sample_polygon_area_turk;
			experiments[count].render_settings.sample_count = 128;
			++count;
		}
	}

	// A shadowed plane with different MIS techniques
	if (all_figs || VK_FALSE) {
		render_settings_t mis_base = {
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_specular_mis,
			.mis_visibility_estimate = 0.5f,
			.polygon_sampling_technique = sample_polygon_projected_solid_angle,
			.error_min_exponent = -7.0f,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_TRUE, .show_polygonal_lights = VK_TRUE,
		};
		experiment_t mis_plane_base = {
			.scene_index = scene_mis_plane,
			.width = 1024, .height = 1024,
			.render_settings = mis_base
		};
		// Use all available MIS heuristics
		const char* names[mis_heuristic_count];
		names[mis_heuristic_balance] = "balance_veach";
		names[mis_heuristic_power] = "power_veach";
		names[mis_heuristic_weighted] = "weighted_ours";
		names[mis_heuristic_optimal_clamped] = "clamped_optimal_ours";
		names[mis_heuristic_optimal] = "optimal_ours";
		for (uint32_t j = 0; j != mis_heuristic_count; ++j) {
			experiments[count] = mis_plane_base;
			const char* path_pieces[] = { "data/experiments/mis_plane_", names[j], "_2spp_%.3f.png" };
			experiments[count].screenshot_path = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
			experiments[count].render_settings.mis_heuristic = j;
			++count;
		}
		// Also use balance MIS between solid angle sampling and GGX sampling
		experiments[count] = mis_plane_base;
		experiments[count].screenshot_path = copy_string("data/experiments/mis_plane_solid_angle_and_ggx_balance_veach_2spp_%.3f.png");
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_ggx_mis;
		experiments[count].render_settings.mis_heuristic = mis_heuristic_balance;
		++count;
		// And the one-sample estimator
		experiments[count] = mis_plane_base;
		experiments[count].screenshot_path = copy_string("data/experiments/mis_plane_diffuse_and_specular_random_ours_1spp_%.3f.png");
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_specular_random;
		++count;
		// And produce a ground truth with solid angle and GGX sampling
		experiments[count] = mis_plane_base;
		experiments[count].screenshot_path = copy_string("data/experiments/mis_plane_reference_128spp_%.3f.png");
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_specular_mis;
		experiments[count].render_settings.mis_heuristic = mis_heuristic_balance;
		experiments[count].render_settings.sample_count = 64;
		++count;
	}

	// The Cornell box with different diffuse sampling techiques
	if (all_figs || VK_FALSE) {
		render_settings_t diffuse_only_base = {
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_only,
			.error_min_exponent = -7.0f,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_TRUE, .show_polygonal_lights = VK_TRUE,
		};
		experiment_t cornell_box_base = {
			.scene_index = scene_cornell_box,
			.width = 1024, .height = 1024,
			.render_settings = diffuse_only_base
		};
		// Use each diffuse technique
		for (uint32_t j = 0; j != sample_polygon_count; ++j) {
			experiments[count] = cornell_box_base;
			const char* path_pieces[] = { "data/experiments/cornell_box_", sample_polygon_name[j], "_1spp_%.3f.png" };
			experiments[count].screenshot_path = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
			experiments[count].render_settings.polygon_sampling_technique = j;
			++count;
		}
		// For Arvo's technique we also create a version with a tilted light
		// source
		experiments[count] = cornell_box_base;
		experiments[count].quick_save_path = copy_string("data/quicksaves/cornell_box_tilted_light.save");
		experiments[count].screenshot_path = copy_string("data/experiments/cornell_box_projected_solid_angle_arvo_tilted_1spp_%.3f.png");
		experiments[count].render_settings.polygon_sampling_technique = sample_polygon_projected_solid_angle_arvo;
		++count;
		// And a reference for that
		experiments[count] = cornell_box_base;
		experiments[count].quick_save_path = copy_string("data/quicksaves/cornell_box_tilted_light.save");
		experiments[count].screenshot_path = copy_string("data/experiments/cornell_box_reference_tilted_128spp_%.3f.png");
		experiments[count].render_settings.polygon_sampling_technique = sample_polygon_solid_angle;
		experiments[count].render_settings.sample_count = 128;
		++count;
		// And we want a ground truth image with solid angle sampling
		experiments[count] = cornell_box_base;
		experiments[count].screenshot_path = copy_string("data/experiments/cornell_box_reference_128spp_%.3f.png");
		experiments[count].render_settings.polygon_sampling_technique = sample_polygon_solid_angle;
		experiments[count].render_settings.sample_count = 128;
		++count;
	}

	// A plane shadowed by a bollard and a potted plant. The view configuration
	// provokes bias in our biased method.
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.exposure_factor = 10.0f, .roughness_factor = 1.0f, .sample_count = 2048,
			.sampling_strategies = sampling_strategies_diffuse_specular_mis,
			.mis_heuristic = mis_heuristic_optimal_clamped, .mis_visibility_estimate = 0.5f,
			.error_min_exponent = -7.0f,
			.polygon_sampling_technique = sample_polygon_projected_solid_angle,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_TRUE, .show_polygonal_lights = VK_TRUE,
		};
		experiment_t planes_base = {
			.scene_index = scene_shadowed_plane,
			.width = 1024, .height = 1024,
			.render_settings = settings_base
		};
		// Unbiased
		experiments[count] = planes_base;
		experiments[count].screenshot_path = copy_string("data/experiments/shadowed_plane_reference_4096spp_%.3f.png");
		++count;
		// Biased
		experiments[count] = planes_base;
		experiments[count].render_settings.polygon_sampling_technique = sample_polygon_projected_solid_angle_biased;
		experiments[count].screenshot_path = copy_string("data/experiments/shadowed_plane_biased_4096spp_%.3f.png");
		++count;
	}

	// The attic with a rectangular light that uses an IES profile
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_specular_mis,
			.mis_heuristic = mis_heuristic_optimal_clamped, .mis_visibility_estimate = 0.5f,
			.error_min_exponent = -7.0f,
			.polygon_sampling_technique = sample_polygon_projected_solid_angle,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_TRUE, .show_polygonal_lights = VK_TRUE,
		};
		experiment_t attic_base = {
			.scene_index = scene_attic,
			.width = 1280, .height = 1024,
			.render_settings = settings_base
		};
		experiments[count] = attic_base;
		experiments[count].quick_save_path = copy_string("data/quicksaves/attic_ies_profile.save");
		experiments[count].screenshot_path = copy_string("data/experiments/ies_profile_attic_2spp_%.3f.png");
		++count;
	}

	// Three planes of different roughness lit by a Lambertian emitter
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_specular_mis,
			.mis_heuristic = mis_heuristic_weighted, .mis_visibility_estimate = 0.5f,
			.error_min_exponent = -7.0f,
			.polygon_sampling_technique = sample_polygon_projected_solid_angle,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_TRUE, .show_polygonal_lights = VK_TRUE,
		};
		experiment_t planes_base = {
			.scene_index = scene_roughness_planes,
			.width = 2048 + 256, .height = 1024,
			.render_settings = settings_base
		};
		experiments[count] = planes_base;
		experiments[count].screenshot_path = copy_string("data/experiments/roughness_planes_lambertian_2spp_%.3f.png");
		++count;
		// Same with projected solid angle sampling only
		experiments[count] = planes_base;
		experiments[count].screenshot_path = copy_string("data/experiments/roughness_planes_lambertian_diffuse_only_1spp_%.3f.png");
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_only;
		++count;
	}

	// Three planes of different roughness lit by a textured emitter
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_specular_mis,
			.mis_heuristic = mis_heuristic_optimal_clamped, .mis_visibility_estimate = 0.5f,
			.error_min_exponent = -7.0f,
			.polygon_sampling_technique = sample_polygon_projected_solid_angle,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_TRUE, .show_polygonal_lights = VK_TRUE,
		};
		experiment_t planes_base = {
			.scene_index = scene_roughness_planes,
			.width = 1280, .height = 1024,
			.render_settings = settings_base
		};
		experiments[count] = planes_base;
		experiments[count].quick_save_path = copy_string("data/quicksaves/roughness_planes_screen.save");
		experiments[count].screenshot_path = copy_string("data/experiments/roughness_planes_screen_2spp_%.3f.png");
		++count;
	}

	// Measure timings for different polygon sampling techniques in different
	// geometric configurations
	if (all_timings || VK_FALSE) {
		render_settings_t diffuse_only_base = {
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_only,
			.error_min_exponent = -7.0f,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_FALSE, .show_polygonal_lights = VK_FALSE,
		};
		experiment_t timings_base = {
			.scene_index = scene_roughness_planes,
			.width = 1920, .height = 1080,
			.render_settings = diffuse_only_base
		};
		// Test different vertex counts
		for (uint32_t i = 3; i != 8; ++i) {
			// Test central and decentral cases
			for (uint32_t j = 0; j != 2; ++j) {
				// Test with many lights and one light per sample or with one
				// light and many samples per light
				for (uint32_t k = 0; k != 2; ++k) {
					// Test each diffuse technique
					for (uint32_t l = 0; l != sample_polygon_count; ++l) {
						// Decode the indices
						const char vertex_count_string[2] = { '0' + i, 0 };
						const char* configuration = (j == 0) ? "central_" : "decentral_";
						uint32_t sample_count = (k == 0) ? 1 : 128;
						uint32_t light_count = (k == 0) ? 128 : 1;
						const char* light_count_string = (k == 0) ? "_128" : "";
						// Assemble paths
						experiments[count] = timings_base;
						const char* path_pieces[] = { "data/experiments/timings_", configuration, vertex_count_string, light_count_string, "_", sample_polygon_name[l], "_%.3f.png" };
						experiments[count].screenshot_path = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
						const char* quick_save_pieces[] = { "data/quicksaves/roughness_planes_", configuration, vertex_count_string, light_count_string, ".save" };
						experiments[count].quick_save_path = concatenate_strings(COUNT_OF(quick_save_pieces), quick_save_pieces);
						// Adjust settings
						experiments[count].render_settings.polygon_sampling_technique = l;
						experiments[count].render_settings.sample_count = sample_count;
						experiments[count].render_settings.exposure_factor /= (float) light_count;
						++count;
					}
				}
			}
		}
	}

	// The arcade with a heptagonal area light mounted to a wall
	if (html_figs || VK_FALSE) {
		render_settings_t diffuse_only_base = {
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_only,
			.mis_heuristic = mis_heuristic_optimal_clamped, .mis_visibility_estimate = 0.5f,
			.error_min_exponent = -7.0f,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_TRUE, .show_polygonal_lights = VK_TRUE,
		};
		experiment_t arcade_base = {
			.scene_index = scene_arcade,
			.width = 1024, .height = 1024,
			.render_settings = diffuse_only_base
		};
		const char* quick_save_path = "data/quicksaves/Arcade_heptagon.save";
		// Use several diffuse techniques
		sample_polygon_technique_t techniques[] = {
			sample_polygon_area_turk,
			sample_polygon_clipped_solid_angle,
			sample_polygon_projected_solid_angle,
			sample_polygon_projected_solid_angle_biased,
		};
		for (uint32_t j = 0; j != COUNT_OF(techniques); ++j) {
			experiments[count] = arcade_base;
			const char* path_pieces[] = { "data/experiments/arcade_", sample_polygon_name[techniques[j]], "_1spp_%.3f.png" };
			experiments[count].quick_save_path = copy_string(quick_save_path);
			experiments[count].screenshot_path = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
			experiments[count].render_settings.polygon_sampling_technique = techniques[j];
			++count;
		}
		// Also try clamped optimal MIS
		experiments[count] = arcade_base;
		experiments[count].screenshot_path = copy_string("data/experiments/arcade_clamped_optimal_mis_ours_2spp_%.3f.png");
		experiments[count].quick_save_path = copy_string(quick_save_path);
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_specular_mis;
		experiments[count].render_settings.mis_heuristic = mis_heuristic_optimal_clamped;
		experiments[count].render_settings.polygon_sampling_technique = sample_polygon_projected_solid_angle;
		++count;
		// And use GGX importance sampling with solid angle sampling
		experiments[count] = arcade_base;
		experiments[count].screenshot_path = copy_string("data/experiments/arcade_solid_angle_and_ggx_mis_2spp_%.3f.png");
		experiments[count].quick_save_path = copy_string(quick_save_path);
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_ggx_mis;
		experiments[count].render_settings.mis_heuristic = mis_heuristic_balance;
		experiments[count].render_settings.polygon_sampling_technique = sample_polygon_solid_angle;
		++count;
		// We want a ground truth image with clamped optimal MIS
		experiments[count] = arcade_base;
		experiments[count].screenshot_path = copy_string("data/experiments/arcade_reference_128spp_%.3f.png");
		experiments[count].quick_save_path = copy_string(quick_save_path);
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_specular_mis;
		experiments[count].render_settings.mis_heuristic = mis_heuristic_optimal_clamped;
		experiments[count].render_settings.polygon_sampling_technique = sample_polygon_projected_solid_angle;
		experiments[count].render_settings.sample_count = 64;
		++count;
	}

	// The living room with a fairly small square ceiling light
	if (html_figs || VK_FALSE) {
		render_settings_t diffuse_only_base = {
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_only,
			.mis_heuristic = mis_heuristic_optimal_clamped, .mis_visibility_estimate = 0.5f,
			.error_min_exponent = -7.0f,
			.noise_type = noise_type_ahmed, .animate_noise = VK_FALSE,
			.trace_shadow_rays = VK_TRUE, .show_polygonal_lights = VK_TRUE,
		};
		experiment_t living_room_base = {
			.scene_index = scene_living_room,
			.width = 1920, .height = 1080,
			.render_settings = diffuse_only_base
		};
		const char* quick_save_path = "data/quicksaves/living_room_ceiling_light.save";
		// Use several diffuse techniques
		sample_polygon_technique_t techniques[] = {
			sample_polygon_area_turk,
			sample_polygon_rectangle_solid_angle_urena,
			sample_polygon_clipped_solid_angle,
			sample_polygon_projected_solid_angle,
			sample_polygon_projected_solid_angle_biased,
		};
		for (uint32_t j = 0; j != COUNT_OF(techniques); ++j) {
			experiments[count] = living_room_base;
			const char* path_pieces[] = { "data/experiments/living_room_", sample_polygon_name[techniques[j]], "_1spp_%.3f.png" };
			experiments[count].screenshot_path = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
			experiments[count].quick_save_path = copy_string(quick_save_path);
			experiments[count].render_settings.polygon_sampling_technique = techniques[j];
			++count;
		}
		// Also try clamped optimal MIS
		experiments[count] = living_room_base;
		experiments[count].screenshot_path = copy_string("data/experiments/living_room_clamped_optimal_mis_ours_2spp_%.3f.png");
		experiments[count].quick_save_path = copy_string(quick_save_path);
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_specular_mis;
		experiments[count].render_settings.mis_heuristic = mis_heuristic_optimal_clamped;
		experiments[count].render_settings.polygon_sampling_technique = sample_polygon_projected_solid_angle;
		++count;
		// We want a ground truth image with solid angle sampling
		experiments[count] = living_room_base;
		experiments[count].screenshot_path = copy_string("data/experiments/living_room_reference_128spp_%.3f.png");
		experiments[count].quick_save_path = copy_string(quick_save_path);
		experiments[count].render_settings.polygon_sampling_technique = sample_polygon_solid_angle;
		experiments[count].render_settings.sample_count = 128;
		++count;
	}

	// Update the file ending for HDR screenshots
	if (take_hdr_screenshots) {
		for (uint32_t i = 0; i != count; ++i) {
			char* path = experiments[i].screenshot_path;
			size_t length = strlen(path);
			path[length - 3] = 'h';
			path[length - 2] = 'd';
			path[length - 1] = 'r';
			experiments[i].use_hdr = VK_TRUE;
		}
	}

	// Output everything
	if (count > size)
		printf("WARNING: Insufficient space allocated for %d experiments.\n", count);
	else
		printf("Defined %d experiments to reproduce.\n", count);
	// Print screenshot paths and indices (useful to figure out command line
	// arguments)
	if (VK_FALSE) {
		for (uint32_t i = 0; i != count; ++i)
			printf("%03d: %s\n", i, experiments[i].screenshot_path);
	}
	list->count = count;
	list->next = count + 1;
	list->experiments = experiments;
	list->next_setup_time = glfwGetTime();
}


void destroy_experiment_list(experiment_list_t* list) {
	for (uint32_t i = 0; i != list->count; ++i) {
		free(list->experiments[i].quick_save_path);
		free(list->experiments[i].screenshot_path);
	}
	free(list->experiments);
	memset(list, 0, sizeof(*list));
}
