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


#include "polygonal_light.h"
#include "math_utilities.h"
#include "string_utilities.h"
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>


int set_polygonal_light_vertex_count(polygonal_light_t* light, uint32_t vertex_count) {
	if (vertex_count == light->vertex_count && light->vertices_plane_space && light->vertices_world_space && light->fan_areas)
		return 0;
	float* vertices = (float*) malloc(sizeof(float) * 4 * vertex_count);
	memset(vertices, 0, sizeof(float) * 4 * vertex_count);
	if (light->vertices_plane_space)
		memcpy(vertices, light->vertices_plane_space, sizeof(float) * 4 * ((vertex_count < light->vertex_count) ? vertex_count : light->vertex_count));
	free(light->vertices_plane_space);
	light->vertices_plane_space = vertices;
	free(light->vertices_world_space);
	light->vertices_world_space = (float*) malloc(sizeof(float) * 4 * vertex_count);
	memset(light->vertices_world_space, 0, sizeof(float) * 4 * vertex_count);
	free(light->fan_areas);
	light->fan_areas = (float*) malloc(sizeof(float) * 4 * (vertex_count - 2));
	memset(light->fan_areas, 0, sizeof(float) * 4 * (vertex_count - 2));
	light->vertex_count = vertex_count;
	return vertex_count != light->vertex_count;
}


void update_polygonal_light(polygonal_light_t* light) {
	// Invert scalings
	light->inv_scaling_x = 1.0f / light->scaling_x;
	light->inv_scaling_y = 1.0f / light->scaling_y;
	// Construct a rotation matrix from Euler angles
	float cx = cosf(light->rotation_angles[0]);
	float sx = sinf(light->rotation_angles[0]);
	float cy = cosf(light->rotation_angles[1]);
	float sy = sinf(light->rotation_angles[1]);
	float cz = cosf(light->rotation_angles[2]);
	float sz = sinf(light->rotation_angles[2]);
	float cxsy = cx * sy;
	float sxsy = sx * sy;
	float rotation[3][4] = {
		{cy * cz, -cy * sz, -sy, 0.0f},
		{-sxsy * cz + cx * sz, sxsy * sz + cx * cz, -sx * cy, 0.0f},
		{cxsy * cz + sx * sz, -cxsy * sz + sx * cz, cx * cy, 0.0f},
	};
	memcpy(light->rotation, rotation, sizeof(rotation));
	// Transform vertices to world space
	float scalings[2] = { light->scaling_x, light->scaling_y };
	for (uint32_t i = 0; i != light->vertex_count; ++i) {
		for (uint32_t j = 0; j != 3; ++j) {
			light->vertices_world_space[i * 4 + j] = light->translation[j];
			for (uint32_t k = 0; k != 2; ++k)
				light->vertices_world_space[i * 4 + j] += scalings[k] * rotation[j][k] * light->vertices_plane_space[i * 4 + k];
		}
	}
	// Construct the plane of the polygon
	light->plane[0] = rotation[0][2];
	light->plane[1] = rotation[1][2];
	light->plane[2] = rotation[2][2];
	light->plane[3] = -(rotation[0][2] * light->translation[0] + rotation[1][2] * light->translation[1] + rotation[2][2] * light->translation[2]);
	// Triangulate the polygon as triangle fan and compute individual areas
	float signed_area = 0.0f;
	for (uint32_t i = 0; i != light->vertex_count - 2; ++i) {
		float matrix[2][2] = {
			{light->vertices_plane_space[(i + 2) * 4 + 0] - light->vertices_plane_space[0], light->vertices_plane_space[(i + 1) * 4 + 0] - light->vertices_plane_space[0]},
			{light->vertices_plane_space[(i + 2) * 4 + 1] - light->vertices_plane_space[1], light->vertices_plane_space[(i + 1) * 4 + 1] - light->vertices_plane_space[1]},
		};
		float triangle_area = 0.5f * (matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0]);
		signed_area += triangle_area;
		light->fan_areas[4 * i + 0] = scalings[0] * scalings[1] * triangle_area;
		light->fan_areas[4 * i + 1] = scalings[0] * scalings[1] * signed_area;
		light->fan_areas[4 * i + 0] *= (triangle_area < 0.0f) ? -1.0f : 1.0f;
		light->fan_areas[4 * i + 1] *= (triangle_area < 0.0f) ? -1.0f : 1.0f;
	}
	// Turn radiant flux into radiance
	signed_area *= scalings[0] * scalings[1];
	float abs_area = (signed_area < 0.0f) ? -signed_area : signed_area;
	light->area = abs_area;
	light->rcp_area = 1.0f / abs_area;
	float flux_factor = 1.0f / (abs_area * M_PI_F);
	for (uint32_t i = 0; i != 3; ++i)
		light->surface_radiance[i] = light->radiant_flux[i] * flux_factor;
	// Flip the plane if the winding is the wrong way around
	for (uint32_t i = 0; i != 4; ++i)
		light->plane[i] = (signed_area > 0.0f) ? light->plane[i] : (-light->plane[i]);
}


polygonal_light_t duplicate_polygonal_light(const polygonal_light_t* light) {
	polygonal_light_t result = *light;
	result.texture_file_path = copy_string(light->texture_file_path);
	result.vertex_count = 0;
	result.vertices_plane_space = NULL;
	result.vertices_world_space = NULL;
	result.fan_areas = NULL;
	set_polygonal_light_vertex_count(&result, light->vertex_count);
	memcpy(result.vertices_plane_space, light->vertices_plane_space, sizeof(float) * 4 * light->vertex_count);
	return result;
}


void destroy_polygonal_light(polygonal_light_t* light) {
	free(light->vertices_plane_space);
	free(light->vertices_world_space);
	free(light->fan_areas);
	free(light->texture_file_path);
	memset(light, 0, sizeof(*light));
}
