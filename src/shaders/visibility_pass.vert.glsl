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
#include "mesh_quantization.glsl"
#include "shared_constants.glsl"

//! The quantized world space position from the vertex buffer
layout (location = 0) in uvec2 g_quantized_vertex_position;

layout (location = 0) flat out uint g_out_primitive_index;

void main() {
	vec3 vertex_position_world_space = decode_position_64_bit(g_quantized_vertex_position, g_mesh_dequantization_factor, g_mesh_dequantization_summand);
	gl_Position = g_world_to_projection_space * vec4(vertex_position_world_space, 1.0f);
	// Without index buffer, the primitive index is the vertex index divided by
	// three
	g_out_primitive_index = gl_VertexIndex / 3;
}
