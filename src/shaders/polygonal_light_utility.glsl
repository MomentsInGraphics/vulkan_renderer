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


//! Constants from polygon_texturing_technique_t
#define polygon_texturing_none 0
#define polygon_texturing_area 1
#define polygon_texturing_portal 2
#define polygon_texturing_ies_profile 3

/*! This struct represents a convex polygonal light source. The polygon is
	planar but oriented arbitrarily in 3D space. Shaders do not care about the
 	plane space or transformation attributes but the C code does.*/
struct polygonal_light_t {
	//! Euler angles describing the rotation of this polygon from plane to
	//! world space
	vec3 rotation_angles;
	//! The scaling along the plane-space x-axis that is used to transform from
	//! plane to world space
	float scaling_x;
	//! The translation applied to this polygon after rotating it to get from
	//! plane to world space
	vec3 translation;
	//! Scaling along the plane-space y-axis
	float scaling_y;
	//! The total radiant flux emitted by this polygon. Textures take away from
	//! this radiant flux.
	vec3 radiant_flux;
	//! Reciprocal of scaling_x
	float inv_scaling_x;
	//! By default the polygon acts as Lambertian emitter, which emits this
	//! radiance at each surface point. The radiance may get scaled by a
	//! texture.
	vec3 surface_radiance;
	//! Reciprocal of scaling_y
	float inv_scaling_y;
	//! The dot product between this vector and a point in homogeneous
	//! coordinates is zero, iff the point is on the plane of this light
	//! source. plane.xyz has unit length.
	vec4 plane;
	//! The number of vertices that make up this polygon. At most
	//! MAX_POLYGONAL_LIGHT_VERTEX_COUNT.
	uint vertex_count;
	//! The way in which the light texture is used.
	//! \see polygon_texturing_technique_t
	uint texturing_technique;
	//! The index of the texture handle holding the texture for this polygonal
	//! light
	uint texture_index;
	//! A rotation matrix from plane space to world space as expressed by the
	//! Euler angles above
	mat3 rotation;
	//! The area of the polygon in world space and its reciprocal
	float area, rcp_area;
#ifdef MAX_POLYGONAL_LIGHT_VERTEX_COUNT
	//! The 2D vertex locations of the polygon within the xy-plane. These
	//! double as UV-coordinates for textured polygons. If vertex_count 
	//! < MAX_POLYGONAL_LIGHT_VERTEX_COUNT, the first vertex is repeated at
	//! that index.
	vec2 vertices_plane_space[MAX_POLYGONAL_LIGHT_VERTEX_COUNT];
	//! The 3D vertex locations of the polygon in world space. If vertex_count 
	//! < MAX_POLYGONAL_LIGHT_VERTEX_COUNT, the first vertex is repeated at
	//! that index.
	vec3 vertices_world_space[MAX_POLYGONAL_LIGHT_VERTEX_COUNT];
	/*! At index i, this array holds the area of the triangle formed by
		vertices 0, i + 1 and i + 2 in x and the area of the triangle fan
		formed by vertices 0 to i + 2 in y. Areas are computed in world space.
		The last y-entry is always the total area.*/
	vec2 fan_areas[MAX_POLYGONAL_LIGHT_VERTEX_COUNT - 2];
#endif
};


#ifdef MAX_POLYGONAL_LIGHT_VERTEX_COUNT

/*! This function performs an intersection test for the polygonal light and the
	line segment connecting ray_origin to ray_end. ray_end is given in
	homogeneous coordinates, so if you want a specific point, set w to 1.0f, if
	you want a semi-infinite ray, pass the direction and set w to 0.0f.
	\return true iff an intersection exists.*/
bool polygonal_light_ray_intersection(polygonal_light_t light, vec3 ray_origin, vec4 ray_end) {
	// Check whether the ray begins and ends on opposite sides of the plane
	if (dot(light.plane, vec4(ray_origin, 1.0f)) * dot(light.plane, ray_end) > 0.0f)
		return false;
	// Check whether the ray is on the same side of each edge
	vec3 ray_dir = ray_end.xyz - ray_end.w * ray_origin;
	float previous_sign = 0.0f;
	bool result = true;
	[[unroll]]
	for (uint i = 0; i != MAX_POLYGONAL_LIGHT_VERTEX_COUNT; ++i) {
		float sign = determinant(mat3(
			ray_dir,
			light.vertices_world_space[i] - ray_origin,
			light.vertices_world_space[(i + 1) % MAX_POLYGONAL_LIGHT_VERTEX_COUNT] - ray_origin
		));
		result = result && ((i >= 3 && i >= light.vertex_count) || previous_sign * sign >= 0.0f);
		previous_sign = sign;
	}
	return result;
}

#endif
