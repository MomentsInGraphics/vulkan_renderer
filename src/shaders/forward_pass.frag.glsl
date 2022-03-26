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


#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "brdfs.glsl"
#include "viridis.glsl"
#include "srgb_utility.glsl"
#include "shared_constants.glsl"

//! Diffuse albedo map for each material
layout (binding = 1) uniform sampler2D g_material_textures[MATERIAL_COUNT];

#if ERROR_DISPLAY_NONE
//! The world space vertex position
layout (location = 0) in vec3 g_vertex_position;
//! The world space vertex normal
layout (location = 1) in vec3 g_vertex_normal;
//! The texture coordinate (used for the diffuse albedo map)
layout (location = 2) in vec2 g_tex_coord;
//! The index of the used material
layout (location = 3) flat in uint g_material_index;

#elif ERROR_DISPLAY_POSITIONS_LOGARITHMIC
//! The difference vector between the world space position computed with either
//! ground truth blend weights or decompressed blend weights
layout (location = 0) in vec3 g_vertex_position_error_world_space;

#endif

layout (location = 0) out vec4 g_out_color;


/*! This function turns a color given in Rec. 709 color space (i.e. linear
	sRGB) into the output color and sets g_out_color accordingly. It does not
	apply the exposure factor but supports HDR screenshots and sRGB
	conversions.*/
void write_output_color(vec3 color_rec_709) {
	g_out_color = vec4(color_rec_709, 1.0f);
	// Here is how we support HDR screenshots: Always rendering to an
	// intermediate HDR render target would be wasting resources, since we do
	// not take screenshots each frame. Instead, a HDR screenshot consists of
	// two LDR screenshots holding different bits of halfs.
	if (g_frame_bits > 0) {
		uint mask = (g_frame_bits == 1) ? 0xFF : 0xFF00;
		uint shift = (g_frame_bits == 1) ? 0 : 8;
		uvec2 half_bits = uvec2(packHalf2x16(g_out_color.rg), packHalf2x16(g_out_color.ba));
		g_out_color = vec4(
			((half_bits[0] & mask) >> shift) * (1.0f / 255.0f),
			((((half_bits[0] & 0xFFFF0000) >> 16) & mask) >> shift) * (1.0f / 255.0f),
			((half_bits[1] & mask) >> shift) * (1.0f / 255.0f),
			1.0f
		);
		// We just want to write bits to the render target, not colors. If the
		// graphics pipeline does linear to sRGB conversion for us, we do sRGB
		// to linear conversion here to counter that.
#if OUTPUT_LINEAR_RGB
		g_out_color.rgb = convert_srgb_to_linear_rgb(g_out_color.rgb);
#endif
	}
#if !OUTPUT_LINEAR_RGB
	// if g_frame_bits == 0, we output linear RGB or sRGB as requested
	else
		g_out_color.rgb = convert_linear_rgb_to_srgb(g_out_color.rgb);
#endif
}


void main() {
#if ERROR_DISPLAY_NONE
	// Prepare shading data
	shading_data_t shading_data;
	shading_data.position = g_vertex_position;
	shading_data.normal = normalize(g_vertex_normal);
	shading_data.diffuse_albedo = texture(g_material_textures[nonuniformEXT(g_material_index)], g_tex_coord).rgb;
	shading_data.fresnel_0 = vec3(0.02f);
	shading_data.roughness = g_roughness;
	// If necessary, manipulate the shading normal so that the viewing
	// direction is in the upper hemisphere
	shading_data.outgoing = normalize(g_camera_position_world_space - shading_data.position);
	float normal_offset = max(0.0f, 1.0e-3f - dot(shading_data.normal, shading_data.outgoing));
	shading_data.normal = fma(vec3(normal_offset), shading_data.outgoing, shading_data.normal);
	shading_data.normal = normalize(shading_data.normal);
	shading_data.lambert_outgoing = dot(shading_data.normal, shading_data.outgoing);
	// Implement Frostbite shading for a directional light
	vec3 brdf = evaluate_brdf(shading_data, g_light_direction_world_space);
	brdf = (shading_data.lambert_outgoing < 0.0f) ? vec3(0.0f) : brdf;
	float lambert_incoming = max(0.0f, dot(shading_data.normal, g_light_direction_world_space));
	vec3 final_color = g_light_irradiance * lambert_incoming * brdf;
	// Output the color in the appropriate color space
	write_output_color(final_color * g_exposure_factor);

#elif ERROR_DISPLAY_POSITIONS_LOGARITHMIC
	// Compute the error in meters
	float error = length(g_vertex_position_error_world_space);
	// Turn it into a value from 0 to 1 for display
	float mapped_error = log2(error) * g_error_factor + g_error_summand;
	// Apply a color map and color space conversions
	write_output_color(convert_srgb_to_linear_rgb(viridis(mapped_error)));
#endif
}
