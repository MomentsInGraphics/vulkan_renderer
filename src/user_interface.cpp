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
	animation_t* animation = &app->scene.animation;
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
	for (; scene_index != COUNT_OF(g_scene_sources); ++scene_index) {
		int offset = (int) strlen(scene->source.file_path) - (int) strlen(g_scene_sources[scene_index].file_path);
		if (offset >= 0 && strcmp(scene->source.file_path + offset, g_scene_sources[scene_index].file_path) == 0)
			break;
	}
	const char* scene_names[COUNT_OF(g_scene_sources)];
	for (uint32_t i = 0; i != COUNT_OF(g_scene_sources); ++i)
		scene_names[i] = g_scene_sources[i].name;
	if (ImGui::Combo("Scene", &scene_index, scene_names, COUNT_OF(scene_names))) {
		destroy_scene_source(&scene->source);
		copy_scene_source(&scene->source, &g_scene_sources[scene_index]);
		updates->quick_load = updates->reload_scene = VK_TRUE;
	}
	ImGui::DragInt("Instance count", (int*) &settings->instance_count, 1.0f, 1, 1000);
	// Animation controls
	ImGui::SliderFloat("Time (s)", &scene->time, animation->time_start, animation->time_start + animation->time_sample_count * animation->time_step, "%.2f", 1.0f);
	ImGui::DragFloat("Playback speed", &settings->playback_speed, 0.02f, -4.0f, 4.0f, "%.2f", 1.0f);

	// The used compression technique
	blend_attribute_compression_parameters_t* compression_params = &settings->compression_params;
	const char* compression_methods[blend_attribute_compression_count];
	compression_methods[blend_attribute_compression_none] = "32 bit floats + 16 bit indices";
	compression_methods[blend_attribute_compression_unit_cube_sampling] = "Unit cube sampling (Kuth and Meyer)";
	compression_methods[blend_attribute_compression_power_of_two_aabb] = "Power-of-two AABB (Kuth and Meyer)";
	compression_methods[blend_attribute_compression_optimal_simplex_sampling_19] = "Optimal simplex sampling, 19 bit weights (Kuth and Meyer)";
	compression_methods[blend_attribute_compression_optimal_simplex_sampling_22] = "Optimal simplex sampling, 22 bit weights (Kuth and Meyer)";
	compression_methods[blend_attribute_compression_optimal_simplex_sampling_35] = "Optimal simplex sampling, 35 bit weights (Kuth and Meyer)";
	compression_methods[blend_attribute_compression_permutation_coding] = "Permutation coding (ours)";
	if (ImGui::Combo("Compression", (int*) &compression_params->method, compression_methods, blend_attribute_compression_count))
		updates->reload_scene = VK_TRUE;
	if (settings->compression_params.method != blend_attribute_compression_none) {
		// Provide user interfaces to change compression parameters
		if (ImGui::DragInt("Bytes per vertex (request)", (int*) &settings->requested_vertex_size, 1.0f, 1, 13 * 6))
			updates->reload_scene = VK_TRUE;
	}
	if (ImGui::DragInt("Bones per vertex (request)", (int*) &settings->requested_max_bone_count, 1.0f, 2, 13))
		updates->reload_scene = VK_TRUE;
	ImGui::Text("Using %u bytes per vertex\nUsing %u bones per vertex", compression_params->vertex_size, compression_params->max_bone_count);
	if (updates->reload_scene) {
		compression_params->max_bone_count = settings->requested_max_bone_count;
		if (compression_params->max_bone_count > scene->source.available_bone_count)
			compression_params->max_bone_count = scene->source.available_bone_count;
		compression_params->max_tuple_count = app->scene_specification.source.max_tuple_count;
		compression_params->vertex_size = settings->requested_vertex_size;
		complete_blend_attribute_compression_parameters(compression_params);
	}

	// Whether the error of the vertex positions should be visualized. Due to
	// attribute alignment (and laziness) that is only possible for even bone
	// counts.
	if (settings->compression_params.max_bone_count % 2 == 0) {
		const char* error_displays[error_display_count];
		error_displays[error_display_none] = "Disabled";
		error_displays[error_display_positions_logarithmic] = "Positions, logarithmic";
		if (ImGui::Combo("Error display", (int*) &settings->error_display, error_displays, error_display_count))
			updates->reload_scene = VK_TRUE;
		if (settings->error_display != error_display_none) {
			ImGui::DragFloat("Min error exponent (base 10)", &settings->error_min_exponent, 0.1f, -9.0f, 0.0f, "%.1f");
			ImGui::DragFloat("Max error exponent (base 10)", &settings->error_max_exponent, 0.1f, -9.0f, 0.0f, "%.1f");
		}
	}
	else if (settings->error_display != error_display_none) {
		settings->error_display = error_display_none;
		updates->reload_scene = VK_TRUE;
	}
	
	// Switching vertical synchronization
	if (ImGui::Checkbox("Vsync", (bool*) &settings->v_sync))
		updates->recreate_swapchain = VK_TRUE;
	// Various rendering settings
	if (settings->error_display == error_display_none)
		ImGui::DragFloat("Exposure", &settings->exposure_factor, 0.05f, 0.0f, 200.0f, "%.2f");
	ImGui::DragFloat("Material roughness", &settings->roughness, 0.01f, 0.0f, 1.0f, "%.2f");
	ImGui::DragFloat("Light inclination", &scene->light_inclination, 0.01f, 0.0f, M_PI_F, "%.2f");
	ImGui::DragFloat("Light azimuth", &scene->light_azimuth, 0.01f, -M_PI_F, M_PI_F, "%.2f");
	ImGui::ColorEdit3("Light irradiance", scene->light_irradiance);

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
