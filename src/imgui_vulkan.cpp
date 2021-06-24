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


#include "imgui_vulkan.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <iostream>

imgui_handle_t init_imgui(struct GLFWwindow* window) {
	ImGuiContext* context = ImGui::CreateContext();
	imgui_handle_t result = {context};
	// Register callbacks and set the font scale
	ImGui_ImplGlfw_InitForVulkan(window, true);
	ImGui::SetCurrentContext(context);
	ImGuiIO& io = ImGui::GetIO();
	io.FontDefault = io.Fonts->AddFontFromFileTTF("data/LinBiolinum_Rah.ttf", 26.0f);
	ImGui::GetStyle().ScaleAllSizes(1.5f);
	return result;
}


void destroy_imgui(imgui_handle_t imgui) {
	if (imgui.handle) ImGui::DestroyContext((ImGuiContext*) imgui.handle);
}


void get_imgui_image(uint8_t* image_alphas, uint32_t* width, uint32_t* height, imgui_handle_t imgui) {
	ImGui::SetCurrentContext((ImGuiContext*) imgui.handle);
	ImGuiIO& io = ImGui::GetIO();
	uint8_t* alphas;
	int imgui_width, imgui_height;
	io.Fonts->GetTexDataAsAlpha8(&alphas, &imgui_width, &imgui_height);
	if (image_alphas)
		memcpy(image_alphas, alphas, sizeof(uint8_t) * imgui_width * imgui_height);
	if (width) (*width) = (uint32_t) imgui_width;
	if (height) (*height) = (uint32_t) imgui_height;
}


int get_imgui_frame(imgui_frame_t* frame, imgui_handle_t imgui) {
	ImGui::SetCurrentContext((ImGuiContext*) imgui.handle);
	ImGuiIO& io = ImGui::GetIO();
	ImGui::Render();
	ImDrawData* draw_data = ImGui::GetDrawData();
	if (!draw_data->Valid) {
		frame->draw_count = frame->vertex_count = frame->index_count = 0;
		return 1;
	}
	// Early out if any of the limits is violated
	size_t total_draw_count = 0;
	for (int i = 0; i != draw_data->CmdListsCount; ++i)
		total_draw_count += (size_t) draw_data->CmdLists[i]->CmdBuffer.size();
	if (total_draw_count > frame->draws_size
		|| draw_data->TotalIdxCount > (int) frame->indices_size
		|| draw_data->TotalVtxCount > (int) frame->vertices_size)
	{
		std::cout << "Drawing the dear imgui interface requires "
			<< total_draw_count << " draws, " << draw_data->TotalVtxCount << " vertices and " << draw_data->TotalIdxCount << " indices but the allocated buffers allow only "
			<< frame->draws_size << " draws, " << frame->vertices_size << " vertices and " << frame->indices_size << " indices. Please increase these limits." << std::endl;
		frame->draw_count = frame->vertex_count = frame->index_count = 0;
		return 1;
	}
	// Iterate over all draw commands
	frame->draw_count = 0;
	size_t command_list_vertex_offset = 0;
	size_t command_list_index_offset = 0;
	for (int i = 0; i != draw_data->CmdListsCount; ++i) {
		ImDrawList* command_list = draw_data->CmdLists[i];
		for (int j = 0; j != command_list->CmdBuffer.size(); ++j) {
			const ImDrawCmd* source_draw = &draw_data->CmdLists[i]->CmdBuffer[j];
			if (frame->draw_count == frame->draws_size)
				return 1;
			// Copy draw meta-data
			imgui_draw_t* dest_draw = &frame->draws[frame->draw_count];
			dest_draw->scissor_x = (int32_t) (source_draw->ClipRect.x * draw_data->FramebufferScale.x);
			dest_draw->scissor_y = (int32_t) (source_draw->ClipRect.y * draw_data->FramebufferScale.y);
			dest_draw->scissor_width = (uint32_t) ((source_draw->ClipRect.z - source_draw->ClipRect.x) * draw_data->FramebufferScale.x);
			dest_draw->scissor_height = (uint32_t) ((source_draw->ClipRect.w - source_draw->ClipRect.y) * draw_data->FramebufferScale.y);
			dest_draw->triangle_count = source_draw->ElemCount;
			dest_draw->vertex_offset = command_list_vertex_offset + source_draw->VtxOffset;
			dest_draw->index_offset = command_list_index_offset + source_draw->IdxOffset;
			++frame->draw_count;
		}
		// Copy vertex and index data. Indices need an offset.
		memcpy(frame->vertices + command_list_vertex_offset, command_list->VtxBuffer.Data, sizeof(imgui_vertex_t) * (size_t) command_list->VtxBuffer.size());
		for (size_t j = 0; j != (size_t) command_list->IdxBuffer.size(); ++j)
			frame->indices[command_list_index_offset + j] = command_list->IdxBuffer[(int) j] + (uint16_t) command_list_vertex_offset;
		command_list_vertex_offset += command_list->VtxBuffer.size();
		command_list_index_offset += command_list->IdxBuffer.size();
	}
	return 0;
}
