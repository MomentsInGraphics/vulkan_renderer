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
#include "vulkan_basics.h"
#include <stdio.h>
#include <stdint.h>


//! This enumeration characterizes the buffers that are needed to store a mesh.
//! The numerical values represent the array indices of the respective buffers.
typedef enum mesh_buffer_type_e {
	//! \see mesh_t.positions
	mesh_buffer_type_positions,
	//! \see mesh_t.normals_and_tex_coords
	mesh_buffer_type_normals_and_tex_coords,
	//! \see mesh_t.material_indices
	mesh_buffer_type_material_indices,
	//! The number of buffers needed to represent a mesh, excluding the screen-
	//! filling triangle
	mesh_buffer_count,
	//! \see mesh_t.triangle
	mesh_buffer_type_triangle = mesh_buffer_count,
	//! The number of buffers needed to represent a mesh, including the screen-
	//! filling triangle
	mesh_buffer_count_full
} mesh_buffer_type_t;


/*! Holds Vulkan objects representing the geometry of a scene. It is a simple
	triangle mesh without index buffer and a material assignment per triangle.
	It has normals and texture coordinates. Thanks to unions, you can easily
	iterate over all held buffers.*/
typedef struct mesh_s {
	//! The number of triangles in this mesh
	uint64_t triangle_count;
	/*! Positions are quantized in 21 bits per coordinate. To turn these 21-bit
		unsigned integers into world-space coordinates, multiply by this factor
		and add the summand component-wise.*/
	float dequantization_factor[3], dequantization_summand[3];
	union {
		struct {
			/*! A buffer of 2*3*triangle_count uint32_t storing three vertex
				positions for each triangle. The bits of these two uints from
				least significant to most significant are:
				xxxx xxxx xxxx xxxx xxxx xyyy yyyy yyyy
				yyyy yyyy yyzz zzzz zzzz zzzz zzzz zzz-
				\sa dequantization_factor, dequantization_summand */
			buffer_t positions;
			/*! 3*triangle_count normal vectors and texture coordinate pairs
				for the vertices of this mesh. Normal vectors are stored in two
				16-bit UNORMs using an octahedral map, followed by two 16-bit
				UNORMs for the texture coordinate. Texture coordinates need to
				be multiplied by 8 to support wrapping within a triangle.*/
			buffer_t normals_and_tex_coords;
			//! A 16-bit material index for each triangle
			buffer_t material_indices;
			//! A vertex buffer providing a screen-filling triangle. It is not
			//! related to the scene but we want to have it in the same memory
			//! allocation for convenience.
			buffer_t triangle;
		};
		//! All buffers that make up this mesh
		buffer_t buffers[mesh_buffer_count_full];
	};
	union {
		struct {
			//! View onto positions. NULL for staging.
			VkBufferView positions_view;
			//! View onto normals_and_tex_coords. NULL for staging.
			VkBufferView normals_and_tex_coords_view;
			//! View onto material_indices. NULL for staging.
			VkBufferView material_indices_view;
			//! View onto triangle. NULL for staging.
			VkBufferView triangle_view;
		};
		//! Views with appropriate formats onto all buffers. NULL for staging.
		VkBufferView buffer_views[mesh_buffer_count_full];
	};
	//! The memory allocation used for all of the buffers above
	VkDeviceMemory memory;
	//! The size in bytes of the allocated memory
	VkDeviceSize size;
} mesh_t;


/*! Each material is defined by a fixed number of textures. These textures, and
	their meanings are specified by this enum.*/
typedef enum material_texture_type_e {
	//! The diffuse albedo of the surface. May also impact the specular albedo.
	material_texture_type_base_color,
	/*! A texture with parameters for the specular BRDF.
		- Red holds a (currently unused) occlusion coefficient,
		- Green holds a linear roughness parameter,
		- Blue holds metalicity, which controls how the base color affects the
			reflected color at 0 degrees inclination.*/
	material_texture_type_specular,
	//! The tangent space normal vector in Cartesian coordinates. The texture
	//! is unsigned such that the geometric normal is (0.5, 0.5, 1).
	material_texture_type_normal,
	//! The number of textures used to describe one material
	material_texture_count
} material_texture_type_t;


/*! A list of materials to be used in a scene. The material model is fairly, 
	simplistic characterizing each material by a fixed set of textures. This
	object handles the corresponding images and descriptors.*/
typedef struct materials_s {
	//! Number of held materials
	uint64_t material_count;
	//! An array of material_count null-terminated strings providing the name
	//! for each material
	char** material_names;
	/*! material_texture_count * material_count textures characterizing all
		materials. The textures for material i start at index 
		i * material_texture_count and are indexed by material_texture_t
		entries.*/
	images_t textures;
	//! A sampler used for all material textures
	VkSampler sampler;
} materials_t;

/*! Combines a single bottom level acceleration structure with a single top
	level acceleration structure holding only one instance of this bottom level
	acceleration structure.*/
typedef struct acceleration_structure_s {
	union {
		struct {
			//! The bottom level acceleration structure holding all scene
			//! geometry in world space
			VkAccelerationStructureKHR bottom_level;
			//! The top level acceleration structure with one instance of
			//! bottom_level
			VkAccelerationStructureKHR top_level;
		};
		VkAccelerationStructureKHR levels[2];
	};
	//! The buffers that hold the bottom and top-level acceleration structures
	buffers_t buffers;
} acceleration_structure_t;

/*! A static scene that is ready to be rendered. It includes geometry and
	materials but no cameras or light sources.*/
typedef struct scene_s {
	//! The mesh that holds all scene geometry in world space
	mesh_t mesh;
	//! The materials used for the mesh
	materials_t materials;
	//! Acceleration structures for ray tracing in this scene or a bunch of
	//! NULL handles if no acceleration structure was requested
	acceleration_structure_t acceleration_structure;
} scene_t;


/*! Returns the suffix that is used for the file name of a material texture of
	the given type.*/
const char* get_material_texture_suffix(material_texture_type_t type);


/*! Loads a scene from the file at the given path. The calling side has to
	clean up using destroy_scene(). Textures are supposed to be in a directory
	at texture_path. Their names are <material name>_<type suffix>.vkt. Such
	*.vkt files have to be created beforehand using a Python script. If ray
	tracing is supported by the given device, an acceleration structure will be
	created on request. Otherwise, the method succeeds without creating one.
	\return 0 on success.*/
int load_scene(scene_t* scene, const device_t* device, const char* file_path, const char* texture_path, VkBool32 request_acceleration_structure);

//! Frees and nulls the given scene
void destroy_scene(scene_t* scene, const device_t* device);


/*! Writes a layout binding description to the given pointer to describe a
	binding at the given index for all given material textures. The layout uses
	the given binding index, the size will be sufficient for creating the given
	number of descriptor sets.*/
void get_materials_descriptor_layout(VkDescriptorSetLayoutBinding* layout_binding, uint32_t binding_index, const materials_t* materials);

/*! Produces infos that let you bind all textures of the given materials in a
	single array binding of a descriptor set.
	\param texture_count Overwritten with the number of textures of all
		materials.
	\param binding_index The binding index to which all materials should be
		bound.
	\param materials The materials to be bound.
	\return An array of texture_count descriptor infos that has to be freed by
		the calling side.*/
VkDescriptorImageInfo* get_materials_descriptor_infos(uint32_t* texture_count, const materials_t* materials);
