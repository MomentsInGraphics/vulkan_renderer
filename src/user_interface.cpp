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

	// Sampling settings
	const char* sampling_strategies[sampling_strategies_count];
	sampling_strategies[sampling_strategies_diffuse_only] = "Diffuse only";
	sampling_strategies[sampling_strategies_diffuse_ggx_mis] = "Diffuse and GGX with MIS";
	sampling_strategies[sampling_strategies_diffuse_specular_separately] = "Diffuse and specular separately";
	sampling_strategies[sampling_strategies_diffuse_specular_mis] = "Diffuse and specular with MIS";
	sampling_strategies[sampling_strategies_diffuse_specular_random] = "Diffuse or specular chosen randomly";
	if (ImGui::Combo("Sampling strategies", (int*) &settings->sampling_strategies, sampling_strategies, COUNT_OF(sampling_strategies)))
		updates->change_shading = VK_TRUE;
	bool specular_sampling = (settings->sampling_strategies >= sampling_strategies_diffuse_specular_separately);
	bool ggx_sampling = (settings->sampling_strategies == sampling_strategies_diffuse_ggx_mis);
	if (settings->sampling_strategies == sampling_strategies_diffuse_ggx_mis || settings->sampling_strategies == sampling_strategies_diffuse_specular_mis) {
		const char* mis_heuristics[mis_heuristic_count];
		mis_heuristics[mis_heuristic_balance] = "Balance (Veach)";
		mis_heuristics[mis_heuristic_power] = "Power (exponent 2, Veach)";
		mis_heuristics[mis_heuristic_weighted] = "Weighted balance heuristic (ours)";
		mis_heuristics[mis_heuristic_optimal_clamped] = "Clamped optimal heuristic (ours)";
		mis_heuristics[mis_heuristic_optimal] = "Optimal heuristic (ours)";
		uint32_t mis_heuristic_count = COUNT_OF(mis_heuristics);
		if ((settings->polygon_sampling_technique != sample_polygon_projected_solid_angle && settings->polygon_sampling_technique != sample_polygon_projected_solid_angle_biased) || settings->sampling_strategies != sampling_strategies_diffuse_specular_mis) {
			mis_heuristic_count = mis_heuristic_weighted;
			if (settings->mis_heuristic >= mis_heuristic_weighted)
				settings->mis_heuristic = mis_heuristic_power;
		}
		if (ImGui::Combo("MIS heuristic", (int*) &settings->mis_heuristic, mis_heuristics, mis_heuristic_count))
			updates->change_shading = VK_TRUE;
	}
	if (settings->sampling_strategies == sampling_strategies_diffuse_specular_mis && (settings->mis_heuristic == mis_heuristic_optimal_clamped || settings->mis_heuristic == mis_heuristic_optimal))
		ImGui::DragFloat("MIS visibility estimate", &settings->mis_visibility_estimate, 0.01f, 0.0f, 1.0f, "%.2f");
	const char* polygon_sampling_techniques[sample_polygon_count];
	polygon_sampling_techniques[sample_polygon_baseline] = "Baseline (zero cost, bogus results)";
	polygon_sampling_techniques[sample_polygon_area_turk] = "Area sampling (Turk)";
	polygon_sampling_techniques[sample_polygon_rectangle_solid_angle_urena] = "Rectangle solid angle sampling (Urena)";
	polygon_sampling_techniques[sample_polygon_solid_angle_arvo] = "Solid angle sampling (Arvo)";
	polygon_sampling_techniques[sample_polygon_solid_angle] = "Solid angle sampling (ours)";
	polygon_sampling_techniques[sample_polygon_clipped_solid_angle] = "Clipped solid angle sampling (ours)";
	polygon_sampling_techniques[sample_polygon_bilinear_cosine_warp_hart] = "Bilinear cosine warp for solid angle sampling (Hart et al.)";
	polygon_sampling_techniques[sample_polygon_bilinear_cosine_warp_clipping_hart] = "Bilinear cosine warp for clipped solid angle sampling (Hart et al.)";
	polygon_sampling_techniques[sample_polygon_biquadratic_cosine_warp_hart] = "Biquadratic cosine warp for solid angle sampling (Hart et al.)";
	polygon_sampling_techniques[sample_polygon_biquadratic_cosine_warp_clipping_hart] = "Biquadratic cosine warp for clipped solid angle sampling (Hart et al.)";
	polygon_sampling_techniques[sample_polygon_projected_solid_angle_arvo] = "Projected solid angle sampling (Arvo)";
	polygon_sampling_techniques[sample_polygon_projected_solid_angle] = "Projected solid angle sampling (ours)";
	polygon_sampling_techniques[sample_polygon_projected_solid_angle_biased] = "Biased projected solid angle sampling (ours)";
	if (!specular_sampling && !ggx_sampling) {
		// All sampling techniques are available
		if (ImGui::Combo("Polygon sampling", (int*) &settings->polygon_sampling_technique, polygon_sampling_techniques, COUNT_OF(polygon_sampling_techniques)))
			updates->change_shading = VK_TRUE;
	}
	else if (ggx_sampling) {
		// For a few sampling techniques, we have not implemented density
		// computation independent of sampling, so MIS with the GGX sampling is
		// not supported
		sample_polygon_technique_t mis_deny_list[] = {
			sample_polygon_baseline,
			sample_polygon_area_turk,
			sample_polygon_bilinear_cosine_warp_hart,
			sample_polygon_bilinear_cosine_warp_clipping_hart,
			sample_polygon_biquadratic_cosine_warp_hart,
			sample_polygon_biquadratic_cosine_warp_clipping_hart,
		};
		// Prepare the list of allowed techniques and remap the current choice
		const char* allowed_names[sample_polygon_count];
		sample_polygon_technique_t allowed_techniques[sample_polygon_count];
		uint32_t allowed_count = 0;
		int current_choice = 0;
		for (int i = 0; i != sample_polygon_count; ++i) {
			bool allowed = true;
			for (uint32_t j = 0; j != COUNT_OF(mis_deny_list); ++j)
				allowed &= (mis_deny_list[j] != i);
			if (allowed) {
				if (i <= settings->polygon_sampling_technique)
					current_choice = allowed_count;
				allowed_names[allowed_count] = polygon_sampling_techniques[i];
				allowed_techniques[allowed_count] = static_cast<sample_polygon_technique_t>(i);
				++allowed_count;
			}
		}
		// Create the interface and remap outputs
		if (ImGui::Combo("Polygon sampling", &current_choice, allowed_names, allowed_count))
			updates->change_shading = VK_TRUE;
		settings->polygon_sampling_technique = allowed_techniques[current_choice];
	}
	// The specular sampling strategy is only available with our projected
	// solid angle sampling or its biased variant
	else {
		if (settings->polygon_sampling_technique != sample_polygon_projected_solid_angle && settings->polygon_sampling_technique != sample_polygon_projected_solid_angle_biased) {
			settings->polygon_sampling_technique = sample_polygon_projected_solid_angle;
			updates->change_shading = VK_TRUE;
		}
		// Force projected solid angle sampling or its biased variant
		int technique = settings->polygon_sampling_technique - sample_polygon_projected_solid_angle;
		if (ImGui::Combo("Polygon sampling", (int*) &technique,  polygon_sampling_techniques + sample_polygon_projected_solid_angle, 2))
			updates->change_shading = VK_TRUE;
		settings->polygon_sampling_technique = (technique == 0) ? sample_polygon_projected_solid_angle : sample_polygon_projected_solid_angle_biased;
	}
	if (settings->polygon_sampling_technique != sample_polygon_projected_solid_angle && settings->polygon_sampling_technique != sample_polygon_projected_solid_angle_biased
	&& (settings->sampling_strategies == sampling_strategies_diffuse_specular_mis
	||  settings->sampling_strategies == sampling_strategies_diffuse_ggx_mis)
	&& settings->mis_heuristic >= mis_heuristic_weighted)
		settings->mis_heuristic = mis_heuristic_power;
	// Whether the error of the sampling procedure should be visualized
	if ((settings->polygon_sampling_technique == sample_polygon_projected_solid_angle || settings->polygon_sampling_technique == sample_polygon_projected_solid_angle_arvo || settings->polygon_sampling_technique == sample_polygon_projected_solid_angle_biased)
	&& settings->sampling_strategies != sampling_strategies_diffuse_ggx_mis)
	{
		const char* error_displays[error_display_count];
		error_displays[error_display_none] = "Disabled";
		error_displays[error_display_diffuse_backward] = "Diffuse backward error";
		error_displays[error_display_diffuse_backward_scaled] = "Diffuse backward error times projected solid angle";
		error_displays[error_display_diffuse_forward] = "Diffuse forward error";
		error_displays[error_display_specular_backward] = "Specular backward error";
		error_displays[error_display_specular_backward_scaled] = "Specular backward error times projected solid angle";
		error_displays[error_display_specular_forward] = "Specular forward error";
		int count = (settings->sampling_strategies != sampling_strategies_diffuse_only && settings->sampling_strategies != sampling_strategies_diffuse_ggx_mis) ? error_display_count : error_display_specular_backward;
		count = (settings->polygon_sampling_technique == sample_polygon_projected_solid_angle_arvo) ? error_display_diffuse_forward : count;
		if (ImGui::Combo("Error display", (int*) &settings->error_display, error_displays, count))
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
	ImGui::Checkbox("Animate noise", (bool*) &settings->animate_noise);
	// Various rendering settings
	if (settings->error_display == error_display_none)
		ImGui::DragFloat("Exposure", &settings->exposure_factor, 0.05f, 0.0f, 200.0f, "%.2f");
	ImGui::DragFloat("Roughness factor", &settings->roughness_factor, 0.01f, 0.0f, 2.0f, "%.2f");

	// Polygonal light controls
	if (ImGui::Checkbox("Show polygonal lights", (bool*) &settings->show_polygonal_lights))
		updates->change_shading = VK_TRUE;
	for (uint32_t i = 0; i < scene->polygonal_light_count; ++i) {
		char* group_name = format_uint("Polygonal light %u", i);
		polygonal_light_t* light = &scene->polygonal_lights[i];
		if (ImGui::TreeNode(group_name)) {
			float angles_degrees[3] = { light->rotation_angles[0] * 180.0f / M_PI_F, light->rotation_angles[1] * 180.0f / M_PI_F, light->rotation_angles[2] * 180.0f / M_PI_F};
			ImGui::DragFloat3("Rotation (Euler angles)", angles_degrees, 0.1f, -180.0f, 180.0f);
			for (uint32_t i = 0; i != 3; ++i)
				light->rotation_angles[i] = angles_degrees[i] * M_PI_F / 180.0f;
			float scalings[2] = { light->scaling_x, light->scaling_y };
			ImGui::DragFloat2("Scaling (xy)", scalings, 0.01f, 0.01f, 100.0f);
			light->scaling_x = scalings[0];
			light->scaling_y = scalings[1];
			ImGui::DragFloat3("Translation (xyz)", light->translation, 0.01f);
			for (uint32_t i = 0; i != light->vertex_count; ++i) {
				char* label = format_uint("Vertex %u", i);
				ImGui::DragFloat2(label, &light->vertices_plane_space[i * 4], 0.01f);
				free(label);
			}
			ImGui::ColorEdit3("Radiant flux", light->radiant_flux);
			char texture_path[2048] = "";
			if (light->texture_file_path) strcpy(texture_path, light->texture_file_path);
			ImGui::InputText("Texture path (*.vkt)", texture_path, sizeof(texture_path));
			if ((light->texture_file_path == NULL || std::strcmp(texture_path, light->texture_file_path) != 0)
				&&  (std::strlen(texture_path) == 0
				||  std::strlen(texture_path) > 4 && std::strcmp(".vkt", texture_path + strlen(texture_path) - 4) == 0))
			{
				updates->update_light_textures = VK_TRUE;
				free(light->texture_file_path);
				light->texture_file_path = copy_string(texture_path);
			}
			const char* polygon_texturing_techniques[polygon_texturing_count];
			polygon_texturing_techniques[polygon_texturing_none] = "Disabled";
			polygon_texturing_techniques[polygon_texturing_area] = "Texture";
			polygon_texturing_techniques[polygon_texturing_portal] = "Light probe";
			polygon_texturing_techniques[polygon_texturing_ies_profile] = "IES profile";
			ImGui::Combo("Texture type", (int*) &light->texturing_technique, polygon_texturing_techniques, COUNT_OF(polygon_texturing_techniques));
			if (ImGui::Button("Add vertex")) {
				set_polygonal_light_vertex_count(light, light->vertex_count + 1);
				float* vertices = light->vertices_plane_space;
				vertices[(light->vertex_count - 1) * 4 + 0] = 0.5f * (vertices[0] + vertices[(light->vertex_count - 2) * 4 + 0]);
				vertices[(light->vertex_count - 1) * 4 + 1] = 0.5f * (vertices[1] + vertices[(light->vertex_count - 2) * 4 + 1]);
				updates->update_light_count = VK_TRUE;
			}
			if (light->vertex_count > 3) {
				ImGui::SameLine();
				if (ImGui::Button("Delete vertex")) {
					set_polygonal_light_vertex_count(light, light->vertex_count - 1);
					updates->update_light_count = VK_TRUE;
				}
			}
			if (scene->polygonal_light_count > 0) {
				ImGui::SameLine();
				if (ImGui::Button("Delete light"))
					light->vertex_count = 0;
			}
			ImGui::TreePop();
		}
		free(group_name);
	}
	// Go over all lights to see which of them have been deleted
	uint32_t new_light_index = 0;
	for (uint32_t i = 0; i < scene->polygonal_light_count; ++i) {
		if (scene->polygonal_lights[i].vertex_count > 0) {
			scene->polygonal_lights[new_light_index] = scene->polygonal_lights[i];
			++new_light_index;
		}
		else {
			destroy_polygonal_light(&scene->polygonal_lights[i]);
			updates->update_light_count = VK_TRUE;
		}
	}
	scene->polygonal_light_count = new_light_index;
	if (ImGui::Button("Add polygonal light")) {
		// Copy over old polygonal lights
		scene_specification_t old = *scene;
		scene->polygonal_lights = (polygonal_light_t*) malloc(sizeof(polygonal_light_t) * (scene->polygonal_light_count + 1));
		memcpy(scene->polygonal_lights, old.polygonal_lights, sizeof(polygonal_light_t) * scene->polygonal_light_count);
		free(old.polygonal_lights);
		// Create a new one
		polygonal_light_t default_light;
		memset(&default_light, 0, sizeof(default_light));
		default_light.rotation_angles[0] = 0.5f * 3.14159265358f;
		default_light.scaling_x = default_light.scaling_y = 1.0f;
		default_light.radiant_flux[0] = default_light.radiant_flux[1] = default_light.radiant_flux[2] = 1.0f;
		set_polygonal_light_vertex_count(&default_light, 4);
		default_light.vertices_plane_space[0 * 4 + 0] = 0.0f;
		default_light.vertices_plane_space[0 * 4 + 1] = 0.0f;
		default_light.vertices_plane_space[1 * 4 + 0] = 1.0f;
		default_light.vertices_plane_space[1 * 4 + 1] = 0.0f;
		default_light.vertices_plane_space[2 * 4 + 0] = 1.0f;
		default_light.vertices_plane_space[2 * 4 + 1] = 1.0f;
		default_light.vertices_plane_space[3 * 4 + 0] = 0.0f;
		default_light.vertices_plane_space[3 * 4 + 1] = 1.0f;
		scene->polygonal_lights[scene->polygonal_light_count] = default_light;
		scene->polygonal_light_count = old.polygonal_light_count + 1;
		updates->update_light_count = VK_TRUE;
	}

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
