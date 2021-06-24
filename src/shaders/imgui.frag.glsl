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
#include "srgb_utility.glsl"

//! The font and symbol texture
layout (binding = 0) uniform sampler2D g_font_texture;

//! Pass through for the texture coordinate
layout (location = 0) in vec2 g_tex_coord;
//! Pass through for the color
layout (location = 1) in vec4 g_color;
//! Color written to the swapchain image
layout (location = 0) out vec4 g_out_color;

void main() {
	float alpha = texture(g_font_texture, g_tex_coord).r;
	g_out_color = vec4(g_color.rgb, g_color.a * alpha);
#if OUTPUT_LINEAR_RGB
	g_out_color.rgb = convert_srgb_to_linear_rgb(g_out_color.rgb);
#endif
}
