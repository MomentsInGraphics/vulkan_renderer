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
	// A mapping from line sampling techniques to names used in file names
	const char* sample_line_name[sample_line_count];
	sample_line_name[sample_line_baseline] = "baseline";
	sample_line_name[sample_line_area] = "area";
	sample_line_name[sample_line_solid_angle] = "solid_angle";
	sample_line_name[sample_line_clipped_solid_angle] = "clipped_solid_angle";
	sample_line_name[sample_line_linear_cosine_warp_clipping_hart] = "linear_cosine_warp_hart";
	sample_line_name[sample_line_quadratic_cosine_warp_clipping_hart] = "quadratic_cosine_warp_hart";
	sample_line_name[sample_line_projected_solid_angle_li] = "projected_solid_angle_li";
	sample_line_name[sample_line_projected_solid_angle] = "projected_solid_angle";

	// Set to VK_TRUE to run all experiments for generation of figures in the
	// paper
	VkBool32 all_figs = VK_TRUE;
	// Set to VK_TRUE to run all experiments for generation of run time
	// measurements in the paper
	VkBool32 all_timings = VK_TRUE;
	// Set to VK_TRUE to take *.hdr screenshots (16-bit float stored as 32-bit
	// float) instead of *.png
	VkBool32 take_hdr_screenshots = VK_FALSE;

	// The bistro exterior for the teaser
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.brdf_model = brdf_frostbite_diffuse_specular,
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.mis_heuristic = mis_heuristic_optimal_clamped, .mis_visibility_estimate = 0.5f,
			.sampling_strategies = sampling_strategies_diffuse_only,
			.line_sampling_technique = sample_line_projected_solid_angle,
			.noise_type = noise_type_blue, .animate_noise = VK_FALSE, .use_jittered_uniform = VK_TRUE,
			.trace_shadow_rays = VK_TRUE, .show_linear_lights = VK_TRUE,
		};
		experiment_t bistro_base = {
			.scene_index = scene_bistro_outside,
			.width = 1920, .height = 1080,
			.render_settings = settings_base
		};
		// Clipped solid angle sampling
		experiments[count] = bistro_base;
		experiments[count].render_settings.line_sampling_technique = sample_line_clipped_solid_angle;
		experiments[count].screenshot_path = copy_string("data/experiments/bistro_outside_clipped_solid_angle_%.3f.png");
		++count;
		// Projected solid angle sampling
		experiments[count] = bistro_base;
		experiments[count].screenshot_path = copy_string("data/experiments/bistro_outside_projected_solid_angle_%.3f.png");
		++count;
		// Projected solid angle sampling and LTC sampling
		experiments[count] = bistro_base;
		experiments[count].render_settings.sampling_strategies = sampling_strategies_diffuse_specular_mis;
		experiments[count].screenshot_path = copy_string("data/experiments/bistro_outside_clamped_optimal_mis_%.3f.png");
		++count;
		// Reference
		experiments[count] = bistro_base;
		experiments[count].render_settings.line_sampling_technique = sample_line_clipped_solid_angle;
		experiments[count].render_settings.sample_count = 256;
		experiments[count].screenshot_path = copy_string("data/experiments/bistro_outside_reference_%.3f.png");
		++count;
	}

	// The Cornell box with various diffuse sampling strategies
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.brdf_model = brdf_lambertian_diffuse,
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_only,
			.line_sampling_technique = sample_line_projected_solid_angle,
			.noise_type = noise_type_blue, .animate_noise = VK_FALSE, .use_jittered_uniform = VK_TRUE,
			.trace_shadow_rays = VK_TRUE, .show_linear_lights = VK_FALSE,
		};
		experiment_t cornell_box_base = {
			.scene_index = scene_cornell_box,
			.width = 1024, .height = 1024,
			.render_settings = settings_base
		};
		// Use each diffuse technique
		for (uint32_t j = 0; j != sample_line_count; ++j) {
			experiments[count] = cornell_box_base;
			const char* path_pieces[] = { "data/experiments/cornell_box_", sample_line_name[j], "_%.3f.png" };
			experiments[count].screenshot_path = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
			experiments[count].render_settings.line_sampling_technique = j;
			++count;
		}
		// And we want a ground truth image with solid angle sampling
		experiments[count] = cornell_box_base;
		experiments[count].screenshot_path = copy_string("data/experiments/cornell_box_reference_%.3f.png");
		experiments[count].render_settings.line_sampling_technique = sample_line_solid_angle;
		experiments[count].render_settings.sample_count = 256;
		++count;
	}

	// The error of projected solid angle sampling in the Cornell box (with the
	// linear light beginning in the middle)
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.brdf_model = brdf_lambertian_diffuse,
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_only,
			.line_sampling_technique = sample_line_projected_solid_angle,
			.noise_type = noise_type_blue, .animate_noise = VK_FALSE, .use_jittered_uniform = VK_TRUE,
			.trace_shadow_rays = VK_TRUE, .show_linear_lights = VK_FALSE,
			.error_min_exponent = -7.0f,
		};
		experiment_t cornell_box_base = {
			.scene_index = scene_cornell_box,
			.width = 1024, .height = 1024,
			.render_settings = settings_base
		};
		// Backward error
		experiments[count] = cornell_box_base;
		experiments[count].screenshot_path = copy_string("data/experiments/cornell_box_error_%.3f.png");
		experiments[count].quick_save_path = copy_string("data/quicksaves/cornell_box_error.save");
		experiments[count].render_settings.error_display = error_display_diffuse_backward;
		++count;
		// Scaled backward error
		experiments[count] = cornell_box_base;
		experiments[count].screenshot_path = copy_string("data/experiments/cornell_box_error_scaled_%.3f.png");
		experiments[count].quick_save_path = copy_string("data/quicksaves/cornell_box_error.save");
		experiments[count].render_settings.error_display = error_display_diffuse_backward_scaled;
		++count;
	}

	// The bistro interior with different noise
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.brdf_model = brdf_frostbite_diffuse_specular,
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 3,
			.mis_heuristic = mis_heuristic_optimal_clamped, .mis_visibility_estimate = 0.5f,
			.sampling_strategies = sampling_strategies_diffuse_specular_mis,
			.line_sampling_technique = sample_line_projected_solid_angle,
			.noise_type = noise_type_blue, .animate_noise = VK_FALSE, .use_jittered_uniform = VK_TRUE,
			.trace_shadow_rays = VK_TRUE, .show_linear_lights = VK_TRUE,
		};
		experiment_t bistro_base = {
			.scene_index = scene_bistro_inside,
			.width = 1280, .height = 1024,
			.render_settings = settings_base
		};
		// Use white noise
		experiments[count] = bistro_base;
		experiments[count].screenshot_path = copy_string("data/experiments/bistro_inside_independent_white_%.3f.png");
		experiments[count].render_settings.noise_type = noise_type_white;
		experiments[count].render_settings.use_jittered_uniform = VK_FALSE;
		++count;
		// Use independent blue noise
		experiments[count] = bistro_base;
		experiments[count].screenshot_path = copy_string("data/experiments/bistro_inside_independent_blue_%.3f.png");
		experiments[count].render_settings.noise_type = noise_type_blue;
		experiments[count].render_settings.use_jittered_uniform = VK_FALSE;
		++count;
		// Use independent blue noise with jittered uniform sampling
		experiments[count] = bistro_base;
		experiments[count].screenshot_path = copy_string("data/experiments/bistro_inside_jittered_blue_%.3f.png");
		experiments[count].render_settings.noise_type = noise_type_blue;
		experiments[count].render_settings.use_jittered_uniform = VK_TRUE;
		++count;
		// And we want a ground truth image with solid angle sampling
		experiments[count] = bistro_base;
		experiments[count].screenshot_path = copy_string("data/experiments/bistro_inside_reference_%.3f.png");
		experiments[count].render_settings.line_sampling_technique = sample_line_solid_angle;
		experiments[count].render_settings.sample_count = 256;
		++count;
	}

	// The shadowed plane with varying roughness
	if (all_figs || VK_FALSE) {
		render_settings_t settings_base = {
			.brdf_model = brdf_frostbite_diffuse_specular,
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 3,
			.mis_heuristic = mis_heuristic_optimal_clamped, .mis_visibility_estimate = 0.5f,
			.sampling_strategies = sampling_strategies_diffuse_specular_mis,
			.line_sampling_technique = sample_line_projected_solid_angle,
			.noise_type = noise_type_blue, .animate_noise = VK_FALSE, .use_jittered_uniform = VK_TRUE,
			.trace_shadow_rays = VK_TRUE, .show_linear_lights = VK_TRUE,
		};
		experiment_t plane_base = {
			.scene_index = scene_shadowed_plane,
			.width = 1024, .height = 1024,
			.render_settings = settings_base
		};
		// Low roughness
		experiments[count] = plane_base;
		experiments[count].screenshot_path = copy_string("data/experiments/shadowed_plane_roughness_0.3_%.3f.png");
		experiments[count].render_settings.roughness_factor = 0.3f;
		++count;
		// High roughness
		experiments[count] = plane_base;
		experiments[count].screenshot_path = copy_string("data/experiments/shadowed_plane_roughness_1.0_%.3f.png");
		experiments[count].render_settings.roughness_factor = 1.0f;
		++count;
	}

	// Measure timings for different line sampling techniques
	if (all_timings || VK_FALSE) {
		render_settings_t diffuse_only_base = {
			.brdf_model = brdf_frostbite_diffuse_specular,
			.exposure_factor = 8.0f, .roughness_factor = 1.0f, .sample_count = 1,
			.sampling_strategies = sampling_strategies_diffuse_only,
			.line_sampling_technique = sample_line_projected_solid_angle,
			.noise_type = noise_type_blue, .animate_noise = VK_FALSE, .use_jittered_uniform = VK_TRUE,
			.trace_shadow_rays = VK_FALSE, .show_linear_lights = VK_FALSE,
		};
		experiment_t timings_bases[] = {
			{
				.scene_index = scene_roughness_planes,
				.width = 1920, .height = 1080,
				.render_settings = diffuse_only_base,
			},
			{
				.scene_index = scene_bistro_outside,
				.width = 1920, .height = 1080,
				.render_settings = diffuse_only_base,
			},
		};
		const char* scene_names[] = { "roughness_planes", "Bistro_outside" };
		// Test with a plane and the bistro
		for (uint32_t j = 0; j != 2; ++j) {
			// Test with many lights and one light per sample or with one
			// light and many samples per light
			for (uint32_t k = 0; k != 2; ++k) {
				// Test each diffuse technique
				for (uint32_t l = 0; l != sample_line_count; ++l) {
					// Decode the indices
					uint32_t sample_count = (k == 0) ? 1 : 128;
					uint32_t light_count = (k == 0) ? 128 : 1;
					const char* light_count_string = (k == 0) ? "128" : "1";
					// Assemble paths
					experiments[count] = timings_bases[j];
					const char* path_pieces[] = { "data/experiments/timings_", scene_names[j], "_", light_count_string, "_", sample_line_name[l], "_%.3f.png" };
					experiments[count].screenshot_path = concatenate_strings(COUNT_OF(path_pieces), path_pieces);
					const char* quick_save_pieces[] = { "data/quicksaves/", scene_names[j] , "_", light_count_string, ".save" };
					experiments[count].quick_save_path = concatenate_strings(COUNT_OF(quick_save_pieces), quick_save_pieces);
					// Adjust settings
					experiments[count].render_settings.line_sampling_technique = l;
					experiments[count].render_settings.sample_count = sample_count;
					experiments[count].render_settings.exposure_factor /= (float) light_count;
					++count;
				}
			}
		}
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
