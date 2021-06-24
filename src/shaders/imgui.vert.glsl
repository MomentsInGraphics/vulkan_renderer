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

//! The position in viewport coordinates (unit pixels)
layout (location = 0) in vec2 g_position;
//! The texture coordinate
layout (location = 1) in vec2 g_tex_coord;
//! The sRGB color
layout (location = 2) in vec4 g_color;

//! Pass through for the texture coordinate
layout (location = 0) out vec2 g_out_tex_coord;
//! Pass through for the color
layout (location = 1) out vec4 g_out_color;

void main() {
	// To avoid another constant buffer, the viewport dimensions are passed as
	// define. The shader gets recompiled on swapchain resize anyway.
	gl_Position = vec4(fma(g_position, 2.0f / vec2(VIEWPORT_WIDTH, VIEWPORT_HEIGHT), vec2(-1.0f)), 0.0f, 1.0f);
	g_out_tex_coord = g_tex_coord;
	g_out_color = g_color;
}
