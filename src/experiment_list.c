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

	// Which experiments should be defined
	VkBool32 all_errors = VK_TRUE;
	VkBool32 all_timings = VK_TRUE;

	if (all_errors) {
		for (uint32_t i = 0; i != blend_attribute_compression_count; ++i) {
			if (i == blend_attribute_compression_none || i == blend_attribute_compression_optimal_simplex_sampling_19 || i == blend_attribute_compression_optimal_simplex_sampling_35)
				continue;
			render_settings_t settings = {
				.exposure_factor = 1.0f, .roughness = 0.5f, .instance_count = 50,
				.error_display = error_display_positions_logarithmic, .error_min_exponent = -5.0f, .error_max_exponent = -3.5f,
				.requested_vertex_size = 4, .requested_max_bone_count = 4,
				.compression_params = {
					.max_tuple_count = g_scene_sources[scene_boss].max_tuple_count,
					.method = i,
				}
			};
			settings.compression_params.vertex_size = settings.requested_vertex_size;
			settings.compression_params.max_bone_count = settings.requested_max_bone_count;
			complete_blend_attribute_compression_parameters(&settings.compression_params);
			if (settings.requested_vertex_size != settings.compression_params.vertex_size || settings.requested_max_bone_count != settings.compression_params.max_bone_count) {
				printf("Method %u does not support %u bytes per vertex with %u bones per vertex (but %u and %u).\n", i, settings.requested_vertex_size, settings.requested_max_bone_count, settings.compression_params.vertex_size, settings.compression_params.max_bone_count);
				continue;
			}
			experiment_t experiment = {
				.scene_index = scene_boss,
				.width = 700, .height = 1024,
				.render_settings = settings,
				.screenshot_path = format_uint("data/experiments/errors_%u.png", i),
			};
			experiments[count] = experiment;
			++count;
		}
		// Define one more experiment without error display
		experiment_t shaded = experiments[count - 1];
		shaded.render_settings.error_display = error_display_none;
		shaded.screenshot_path = copy_string("data/experiments/errors_shaded.png");
		experiments[count] = shaded;
		++count;
	}

	if (all_timings) {
		// For each compression method, this array provides a set of bytes per
		// vertex (which are actually supported) and maximal bone counts.
		// Entries with 0 need to be skipped. All of these pertain to the big
		// scene (tuple index count ca. 7000).
		static const uint32_t method_settings[blend_attribute_compression_count][6][2] = {
			// blend_attribute_compression_none
			{ {24, 4}, {36, 6}, {48, 8}, {60, 10} },
			// blend_attribute_compression_unit_cube_sampling
			{ {4, 4}, {6, 6}, {8, 8}, {8, 4}, {12, 10} },
			// blend_attribute_compression_power_of_two_aabb
			{ {4, 4}, {6, 6}, {6, 8}, {8, 4}, {8, 8}, {8, 10} },
			// blend_attribute_compression_optimal_simplex_sampling_19
			{ {4, 4} },
			// blend_attribute_compression_optimal_simplex_sampling_22
			{ {0, 0} },
			// blend_attribute_compression_optimal_simplex_sampling_35
			{ {6, 4} },
			// blend_attribute_compression_permutation_coding
			{ {4, 4}, {4, 5}, {4, 6}, {6, 8}, {8, 8}, {8, 10} },
		};
		// Define the experiments
		for (uint32_t i = 0; i != blend_attribute_compression_count; ++i) {
			for (uint32_t j = 0; j != COUNT_OF(method_settings[0]); ++j) {
				uint32_t vertex_size = method_settings[i][j][0];
				uint32_t max_bone_count = method_settings[i][j][1];
				if (vertex_size == 0)
					continue;
				render_settings_t settings = {
					.exposure_factor = 1.0f, .roughness = 0.5f, .instance_count = 50,
					.requested_vertex_size = vertex_size, .requested_max_bone_count = max_bone_count,
					.compression_params = {
						.max_tuple_count = g_scene_sources[scene_characters].max_tuple_count,
						.method = i,
					}
				};
				settings.compression_params.vertex_size = settings.requested_vertex_size;
				settings.compression_params.max_bone_count = settings.requested_max_bone_count;
				complete_blend_attribute_compression_parameters(&settings.compression_params);
				if (vertex_size != settings.compression_params.vertex_size || max_bone_count != settings.compression_params.max_bone_count) {
					printf("Method %u does not support %u bytes per vertex with %u bones per vertex (but %u and %u).\n", i, vertex_size, max_bone_count, settings.compression_params.vertex_size, settings.compression_params.max_bone_count);
					continue;
				}
				experiment_t experiment = {
					.scene_index = scene_characters,
					.width = 1280, .height = 1024,
					.render_settings = settings,
					.screenshot_path = format_uint3("data/experiments/timings_%u_%u_%u_%%.3f.png", i, vertex_size, max_bone_count),
				};
				experiments[count] = experiment;
				++count;
			}
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
