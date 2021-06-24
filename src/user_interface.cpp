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


#include "user_interface.h"
#include "string_utilities.h"
#include "frame_timer.h"
#include "math_utilities.h"
#include <cstring>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <iostream>

void specify_user_interface(application_updates_t* updates, application_t* app, float frame_time) {
	// A few preparations
	ImGui::SetCurrentContext((ImGuiContext*) app->imgui.handle);
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGui::Begin("Scene and render settings");
	scene_specification_t* scene = &app->scene_specification;
	render_settings_t* settings = &app->render_settings;
	// Display some help text
	ImGui::Text("Controls [?]");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip(
			"LMB			Interact with GUI\n"
			"RMB			Rotate camera\n"
			"WASDQE	Move camera\n"
			"Ctrl			  Move slower\n"
			"Shift			Move faster\n"
			"F1				Toggle user interface\n"
			"F2				Toggle v-sync\n"
			"F3				Quick save (camera and lights)\n"
			"F4				Quick load (camera and lights)\n"
			"F5				Reload shaders\n"
			"F10, F12	   Take screenshot"
		);
	// Display the frame rate
	ImGui::SameLine();
	ImGui::Text("Frame time: %.2f ms", frame_time * 1000.0f);
	// Display a text that changes each frame to indicate to the user whether
	// the renderer is running
	static uint32_t frame_index = 0;
	++frame_index;
	ImGui::SameLine();
	const char* progress_texts[] = {" ......", ". .....", ".. ....", "... ...", ".... ..", "..... .", "...... "};
	ImGui::Text(progress_texts[frame_index % COUNT_OF(progress_texts)]);

	// Scene selection
	int scene_index = 0;
	for (; scene_index != COUNT_OF(g_scene_paths); ++scene_index) {
		int offset = (int) strlen(scene->file_path) - (int) strlen(g_scene_paths[scene_index][1]);
		if (offset >= 0 && strcmp(scene->file_path + offset, g_scene_paths[scene_index][1]) == 0)
			break;
	}
	const char* scene_names[COUNT_OF(g_scene_paths)];
	for (uint32_t i = 0; i != COUNT_OF(g_scene_paths); ++i)
		scene_names[i] = g_scene_paths[i][0];
	if (ImGui::Combo("Scene", &scene_index, scene_names, COUNT_OF(scene_names))) {
		free(scene->file_path);
		free(scene->quick_save_path);
		free(scene->texture_path);
		scene->file_path = copy_string(g_scene_paths[scene_index][1]);
		scene->texture_path = copy_string(g_scene_paths[scene_index][2]);
		scene->quick_save_path = copy_string(g_scene_paths[scene_index][3]);
		updates->quick_load = updates->reload_scene = VK_TRUE;
	}
	// BRDF model
	const char* brdf_models[brdf_model_count];
	brdf_models[brdf_lambertian_diffuse] = "Lambertian diffuse";
	brdf_models[brdf_disney_diffuse] = "Disney diffuse";
	brdf_models[brdf_frostbite_diffuse_specular] = "Frostbite diffuse and specular";
	if (ImGui::Combo("BRDF", (int*) &settings->brdf_model, brdf_models, COUNT_OF(brdf_models)))
		updates->change_shading = VK_TRUE;

	// Sampling settings
	const char* sampling_strategies[sampling_strategies_count];
	sampling_strategies[sampling_strategies_diffuse_only] = "Diffuse only";
	sampling_strategies[sampling_strategies_diffuse_specular_mis] = "Diffuse and specular with MIS";
	if (ImGui::Combo("Sampling strategies", (int*) &settings->sampling_strategies, sampling_strategies, COUNT_OF(sampling_strategies)))
		updates->change_shading = VK_TRUE;
	bool specular_sampling = (settings->sampling_strategies != sampling_strategies_diffuse_only);
	if (settings->sampling_strategies == sampling_strategies_diffuse_specular_mis) {
		const char* mis_heuristics[mis_heuristic_count];
		mis_heuristics[mis_heuristic_balance] = "Balance (Veach)";
		mis_heuristics[mis_heuristic_power] = "Power (exponent 2, Veach)";
		mis_heuristics[mis_heuristic_weighted] = "Weighted balance heuristic (ours)";
		mis_heuristics[mis_heuristic_optimal_clamped] = "Clamped optimal heuristic (ours)";
		uint32_t mis_heuristic_count = COUNT_OF(mis_heuristics);
		if (settings->line_sampling_technique != sample_line_projected_solid_angle) {
			mis_heuristic_count = mis_heuristic_weighted;
			if (settings->mis_heuristic >= mis_heuristic_weighted)
				settings->mis_heuristic = mis_heuristic_power;
		}
		if (ImGui::Combo("MIS heuristic", (int*) &settings->mis_heuristic, mis_heuristics, mis_heuristic_count))
			updates->change_shading = VK_TRUE;
	}
	if (settings->sampling_strategies == sampling_strategies_diffuse_specular_mis && (settings->mis_heuristic == mis_heuristic_optimal_clamped))
		ImGui::DragFloat("MIS visibility estimate", &settings->mis_visibility_estimate, 0.01f, 0.0f, 1.0f, "%.2f");
	const char* line_sampling_techniques[sample_line_count];
	line_sampling_techniques[sample_line_baseline] = "Baseline (zero cost, bogus results)";
	line_sampling_techniques[sample_line_area] = "Area sampling";
	line_sampling_techniques[sample_line_solid_angle] = "Solid angle sampling";
	line_sampling_techniques[sample_line_clipped_solid_angle] = "Clipped solid angle sampling";
	line_sampling_techniques[sample_line_linear_cosine_warp_clipping_hart] = "Linear cosine warp (Hart et al.)";
	line_sampling_techniques[sample_line_quadratic_cosine_warp_clipping_hart] = "Quadratic cosine warp (Hart et al.)";
	line_sampling_techniques[sample_line_projected_solid_angle_li] = "Projected solid angle sampling (Li et al.)";
	line_sampling_techniques[sample_line_projected_solid_angle] = "Our projected solid angle sampling";
	if (!specular_sampling) {
		// All sampling techniques are available
		if (ImGui::Combo("Line sampling", (int*) &settings->line_sampling_technique, line_sampling_techniques, COUNT_OF(line_sampling_techniques)))
			updates->change_shading = VK_TRUE;
	}
	else {
		// The specular sampling strategy is only available with our projected
		// solid angle sampling
		int zero = 0;
		ImGui::Combo("Line sampling", (int*) &zero, line_sampling_techniques + sample_line_projected_solid_angle, 1);
		if (settings->line_sampling_technique != sample_line_projected_solid_angle) {
			settings->line_sampling_technique = sample_line_projected_solid_angle;
			updates->change_shading = VK_TRUE;
		}
	}
	if (settings->line_sampling_technique != sample_line_projected_solid_angle
	&& (settings->sampling_strategies == sampling_strategies_diffuse_specular_mis)
	&& settings->mis_heuristic >= mis_heuristic_weighted)
		settings->mis_heuristic = mis_heuristic_power;
	// Whether the error of the sampling procedure should be visualized
	if (settings->line_sampling_technique == sample_line_projected_solid_angle) {
		const char* error_displays[error_display_count];
		error_displays[error_display_none] = "Disabled";
		error_displays[error_display_diffuse_backward] = "Diffuse backward error";
		error_displays[error_display_diffuse_backward_scaled] = "Diffuse backward error times diffuse LTC shading";
		error_displays[error_display_specular_backward] = "Specular backward error";
		error_displays[error_display_specular_backward_scaled] = "Specular backward error times specular LTC shading";
		if (ImGui::Combo("Error display", (int*) &settings->error_display, error_displays, COUNT_OF(error_displays)))
			updates->change_shading = VK_TRUE;
		if (settings->error_display != error_display_none)
			ImGui::DragFloat("Min error exponent (base 10)", &settings->error_min_exponent, 0.1f, -9.0f, 0.0f, "%.1f");
	}
	
	// Switching ray tracing on or off
	if (app->device.ray_tracing_supported) {
		if (ImGui::Checkbox("Trace shadow rays", (bool*) &settings->trace_shadow_rays))
			updates->change_shading = VK_TRUE;
	}
	else
		ImGui::Text("Ray tracing not supported");
	// Switching vertical synchronization
	if (ImGui::Checkbox("Vsync", (bool*) &settings->v_sync))
		updates->recreate_swapchain = VK_TRUE;
	// Changing the sample count
	if (ImGui::InputInt("Sample count", (int*) &settings->sample_count, 1, 10)) {
		if (settings->sample_count < 1) settings->sample_count = 1;
		updates->change_shading = VK_TRUE;
	}
	// Source of pseudorandom numbers
	const char* noise_types[noise_type_full_count];
	noise_types[noise_type_white] = "White noise";
	noise_types[noise_type_blue] = "Blue noise (1D)";
	noise_types[noise_type_sobol] = "Sobol (2+2D)";
	noise_types[noise_type_owen] = "Owen-scrambled Sobol (2+2D)";
	noise_types[noise_type_burley_owen] = "Burley's Owen-scrambled Sobol (2+2D)";
	noise_types[noise_type_ahmed] = "Ahmed's blue-noise diffusion for Sobol (2+2D)";
	noise_types[noise_type_blue_noise_dithered] = "Blue noise dithered (2D)";
	if (ImGui::Combo("Noise type", (int*) &settings->noise_type, noise_types, noise_type_count))
		updates->regenerate_noise = VK_TRUE;
	if (ImGui::Checkbox("Jittered uniform sampling", (bool*) &settings->use_jittered_uniform))
		updates->change_shading = VK_TRUE;
	ImGui::Checkbox("Animate noise", (bool*) &settings->animate_noise);
	// Various rendering settings
	if (settings->error_display != error_display_diffuse_backward && settings->error_display != error_display_specular_backward)
		ImGui::DragFloat("Exposure", &settings->exposure_factor, 0.05f, 0.0f, 200.0f, "%.2f");
	ImGui::DragFloat("Roughness factor", &settings->roughness_factor, 0.01f, 0.0f, 2.0f, "%.2f");

	// Linear light controls
	if (ImGui::Checkbox("Show linear lights", (bool*) &settings->show_linear_lights))
		updates->change_shading = VK_TRUE;
	for (uint32_t i = 0; i < scene->linear_light_count; ++i) {
		char* group_name = format_uint("Linear light %u", i);
		linear_light_t* light = &scene->linear_lights[i];
		if (ImGui::TreeNode(group_name)) {
			ImGui::DragFloat3("Begin", light->begin, 0.01f);
			if (ImGui::Button("Begin at camera"))
				memcpy(light->begin, scene->camera.position_world_space, sizeof(float) * 3);
			ImGui::DragFloat3("End", light->end, 0.01f);
			if (ImGui::Button("End at camera"))
				memcpy(light->end, scene->camera.position_world_space, sizeof(float) * 3);
			ImGui::ColorEdit3("Color", light->radiance_times_radius);
			if (scene->linear_light_count > 0 && ImGui::Button("Delete"))
				light->line_length = -1.0f;
			ImGui::TreePop();
		}
		free(group_name);
	}
	// Go over all lights to see which of them have been deleted
	uint32_t new_light_index = 0;
	for (uint32_t i = 0; i < scene->linear_light_count; ++i) {
		if (scene->linear_lights[i].line_length >= 0.0f) {
			scene->linear_lights[new_light_index] = scene->linear_lights[i];
			++new_light_index;
		}
		else
			updates->update_light_count = VK_TRUE;
	}
	scene->linear_light_count = new_light_index;
	if (ImGui::Button("Add linear light")) {
		// Copy over old linear lights
		scene_specification_t old = *scene;
		scene->linear_lights = (linear_light_t*) malloc(sizeof(linear_light_t) * (scene->linear_light_count + 1));
		memcpy(scene->linear_lights, old.linear_lights, sizeof(linear_light_t) * scene->linear_light_count);
		free(old.linear_lights);
		// Create a new one
		linear_light_t default_light;
		default_light.begin[0] = -1.0f;  default_light.begin[1] = 0.0f;  default_light.begin[2] = 1.0f;
		default_light.end[0] = 1.0f;  default_light.end[1] = 0.0f;  default_light.end[2] = 1.0f;
		default_light.radiance_times_radius[0] = default_light.radiance_times_radius[1] = default_light.radiance_times_radius[2] = 10.0f;
		scene->linear_lights[scene->linear_light_count] = default_light;
		scene->linear_light_count = old.linear_light_count + 1;
		updates->update_light_count = VK_TRUE;
	}
	/* This button exists for profiling
	if (scene->linear_light_count == 1 && ImGui::Button("Duplicate 128 times")) {
		scene->linear_light_count = 128;
		linear_light_t light = scene->linear_lights[0];
		free(scene->linear_lights);
		scene->linear_lights = (linear_light_t*) malloc(sizeof(linear_light_t) * scene->linear_light_count);
		for (uint32_t i = 0; i != scene->linear_light_count; ++i)
			scene->linear_lights[i] = light;
		updates->update_light_count = VK_TRUE;
	}
	//*/

	// Show buttons for quick save and quick load
	if (ImGui::Button("Quick save"))
		updates->quick_save = VK_TRUE;
	ImGui::SameLine();
	if (ImGui::Button("Quick load"))
		updates->quick_load = VK_TRUE;
	// A button to reproduce experiments from the publication
	if (ImGui::Button("Reproduce experiments"))
		app->experiment_list.next = 0;
	// That's all
	ImGui::End();
	ImGui::EndFrame();
}
