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


#pragma once
#include "vulkan_basics.h"
#include "blend_attribute_compression.h"
#include <stdio.h>
#include <stdint.h>


//! This enumeration characterizes the buffers that are needed to store a mesh.
//! The numerical values represent the array indices of the respective buffers.
typedef enum mesh_buffer_type_e {
	//! \see mesh_t.vertices
	mesh_buffer_type_vertices,
	//! \see mesh_t.bone_index_table
	mesh_buffer_type_bone_index_table,
	//! \see mesh_t.material_indices
	mesh_buffer_type_material_indices,
	//! The number of buffers needed to represent a mesh
	mesh_buffer_count,
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
	//! The number of unique tuples of bone indices across all vertices. This
	//! is used to compute the buffer size. For the staging buffer it is an
	//! upper bound, for the device buffer it is exact.
	uint64_t max_tuple_count;
	//! The number of entries in the vectors provided by bone_index_table_view
	uint32_t tuple_vector_size;
	//! The compression scheme used for vertex blend attributes
	blend_attribute_compression_parameters_t compression_params;
	//! Whether ground truth bone indices and weights are part of the vertex
	//! buffer, even if compressed data is available
	VkBool32 store_ground_truth;
	union {
		struct {
			/*! All data that is stored per vertex in an interleaved format.
				There are three vertices per triangle. The first 128 bits per
				vertex are:
				- position: 64 bits with the following meaning (from least to
				  most significant):
				  xxxx xxxx xxxx xxxx xxxx xyyy yyyy yyyy
				  yyyy yyyy yyzz zzzz zzzz zzzz zzzz zzz-
				- normal: Two 16-bit UNORMs providing a coordinate in an
				  octahedral map for the object space normal vector.
				- texture coordinate: Two 16-bit UNORMs providing the texture
				  coordinate, divided by 8.
				The remaining bytes provide ground truth and/or compressed
				weights and indices for blending. See compression_params.
				\sa dequantization_factor, dequantization_summand */
			buffer_t vertices;
			//! A table holding unique tuples of uint16_t bone indices
			buffer_t bone_index_table;
			//! An 8-bit material index for each triangle
			buffer_t material_indices;
		};
		//! All buffers that make up this mesh
		buffer_t buffers[mesh_buffer_count];
	};
	union {
		struct {
			//! NULL
			VkBufferView no_vertices_view;
			//! View onto bone_index_table. NULL for staging.
			VkBufferView bone_index_table_view;
			//! View onto material_indices. NULL for staging.
			VkBufferView material_indices_view;
		};
		//! Views with appropriate formats onto all buffers. NULL for staging.
		VkBufferView buffer_views[mesh_buffer_count];
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


//! A densely sampled animation of bones for a skinned mesh. It gets uploaded
//! to the GPU as texture.
typedef struct animation_s {
	//! The time of the first sampled pose in seconds
	float time_start;
	//! The time in seconds between two sampled poses
	float time_step;
	//! The number of times at which poses have been sampled
	uint64_t time_sample_count;
	//! The number of bones for which animations are stored (including dummies)
	uint64_t bone_count;
	//! Constants specifying how entries of the animation texture should be
	//! dequantized from 16-bit uint to floats
	float dequantization_constants[16];
	/*! A big texture holding transformation matrices for each frame sample.
		Row j holds the j-th sample. Column i holds row i % 3 of the 3x4 matrix
		for bone i / 3.*/
	images_t texture;
	//! A sampler with nearest neighbor interpolation, used to read
	//! transformations
	VkSampler sampler;
} animation_t;


/*! A static scene that is ready to be rendered. It includes geometry and
	materials but no cameras or light sources.*/
typedef struct scene_s {
	//! The mesh that holds all scene geometry in world space
	mesh_t mesh;
	//! The materials used for the mesh
	materials_t materials;
	//! The animation of this scene
	animation_t animation;
} scene_t;


/*! Returns the suffix that is used for the file name of a material texture of
	the given type.*/
const char* get_material_texture_suffix(material_texture_type_t type);


/*! Loads a scene from the file at the given path. The calling side has to
	clean up using destroy_scene(). Textures are supposed to be in a directory
	at texture_path. Their names are <material name>_<type suffix>.vkt. Such
	*.vkt files have to be created beforehand using a Python script. Blend
	attributes get compressed using the given method. The maximal bone count is
	changed accordingly using reduce_bone_count() (it cannot be raised). If
	requested, ground truth blend attributes will be present, even when
	compressed attributes are also available.
	\return 0 on success.*/
int load_scene(scene_t* scene, const device_t* device, const char* file_path, const char* texture_path, const blend_attribute_compression_parameters_t* compression_params, VkBool32 force_ground_truth_blend_attributes);

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
