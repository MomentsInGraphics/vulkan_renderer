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


#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct GLFWwindow;

//! Provides all meta-data needed for a single imgui draw command
typedef struct imgui_draw_s {
	//! The scissor rectangle in viewport coordinates (unit pixels)
	int32_t scissor_x, scissor_y;
	uint32_t scissor_width, scissor_height;
	//! The index of the first vertex in the vertex buffer to use
	size_t vertex_offset;
	//! The offset in the index buffer
	size_t index_offset;
	//! The number of triangles to be drawn
	size_t triangle_count;
} imgui_draw_t;


//! Data of a single vertex for rendering imgui interfaces
typedef struct imgui_vertex_s {
	//! The screen space position in pixels
	float x, y;
	//! The texture coordinate
	float u, v;
	//! The sRGB color and opacity of the vertex
	uint8_t color[4];
} imgui_vertex_t;


/*! This structure is used to exchange information for drawing the user
	interface in one frame between imgui and the renderer.*/
typedef struct imgui_frame_s {
	//! All vertices for the imgui interface. This typically a pointer to 
	//! mapped memory.
	imgui_vertex_t* vertices;
	//! All indices for the triangle lists rendered for the imgui interface.
	//! This typically a pointer to mapped memory.
	uint16_t* indices;
	//! List of draw commands for rendering the whole user interface
	imgui_draw_t* draws;
	//! The number of vertices/indices/draws that fit into the arrays above.
	//! If any one of these limits is violated, nothing can be drawn.
	size_t vertices_size, indices_size, draws_size;
	//! The number of vertices, indices and draws that are currently used
	size_t vertex_count, index_count, draw_count;
} imgui_frame_t;


//! An opaque handle for objects that hold global dear imgui state
typedef struct imgui_handle_s {
	void* handle;
} imgui_handle_t;


//! Initializes imgui and returns a handle to work with it. Invoke 
//! destroy_imgui() for cleanup.
imgui_handle_t init_imgui(struct GLFWwindow* window);

//! Destroys the imgui objects with the given handle
void destroy_imgui(imgui_handle_t imgui);

/*! Retrieves an image that is needed by imgui to render the user interface
	(contains rastered fonts among other things).
	\param image_alphas Pointer to an array that will be filled with one
		opacity per pixel in scanline order. Pass NULL to retrieve dimensions
		only.
	\param width, height Overwritten by the image dimensions. Can be NULL.*/
void get_imgui_image(uint8_t* image_alphas, uint32_t* width, uint32_t* height, imgui_handle_t imgui);

/*! Retrieves information needed to draw the imgui user interface in a single
	frame. Pointers and sizes in the given object have to be set by the calling
	side.
	\return 0 if the information was gathered successfully. 1 if the given
		arrays provide insufficient space and rendering of the GUI should not
		proceed.*/
int get_imgui_frame(imgui_frame_t* frame, imgui_handle_t imgui);

#ifdef __cplusplus
}
#endif
