#  Copyright (C) 2022, Christoph Peters, Karlsruhe Institute of Technology
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https://www.gnu.org/licenses/>.


from struct import pack
import numpy as np
import bpy
from os import path
import re


def encode_normal_32_bit(normal):
    """Encodes the given normal vector into the returned pair of 16-bit
       integers using an octahedral map. The input is an array of shape
       (...,3), the output is a pair of arrays of shape (...,).
    """
    # Project the sphere onto the octahedron, and then onto the xy plane
    octahedral_normal = normal[..., 0:2] / np.linalg.norm(normal, ord=1, axis=normal.ndim-1)[:, np.newaxis]
    # Reflect the folds of the lower hemisphere over the diagonals
    sign_not_zero = np.where(octahedral_normal >= 0.0, 1.0, -1.0)
    octahedral_normal = np.where((normal[..., 2:3] <= 0.0).repeat(2, normal.ndim - 1),
                                 (1.0 - np.abs(octahedral_normal[..., ::-1])) * sign_not_zero,
                                 octahedral_normal)
    # These constants define a linear function concatenated with floor such
    # that:
    # - -1.0 is represented exactly through 1,
    # - 1.0 is represented exactly through 2^bit_count-1,
    # - 0.0 is represented exactly through 2^(bit_count-1),
    # - The floor operation effectively performs round to nearest.
    bit_count = 16
    factor = float((2 ** (bit_count - 1)) - 1)
    summand = factor + 1.5
    coordinates = np.asarray(octahedral_normal * factor + summand, dtype=np.uint16)
    return coordinates[..., 0], coordinates[..., 1]


def part_1_by_2(x):
    """Given an unsigned integer number, this function inserts two zero bits
       between any two of its bits. The least-significant bit stays least
       significant, bits beyond the 10-th are truncated away."""
    # x = ---- ---- ---- ---- ---- --98 7654 3210
    x &= 0x000003ff
    # x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x << 16)) & 0xff0000ff
    # x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x << 8)) & 0x0300f00f
    # x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x << 4)) & 0x030c30c3
    # x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    x = (x ^ (x << 2)) & 0x09249249
    return x


def get_morton_code_3d(position, bounding_box_min, bounding_box_max):
    """Given an array of shape (...,3) and a bounding box as two arrays of
       shape (3,), this function computes 30-bit morton codes for the given
       position within this bounding box and returns them as uint32 array of
       shape (...,).
    """
    factor = 2.0**10.0 / (bounding_box_max - bounding_box_min)
    summand = -bounding_box_min * factor
    factor = np.reshape(factor, [1] * (position.ndim - 1) + [3])
    summand = np.reshape(summand, [1] * (position.ndim - 1) + [3])
    quantized = np.asarray(np.clip(position * factor + summand, 0.0, 2.0**10.0 - 1.0), dtype=np.uint32)
    return sum([part_1_by_2(quantized[..., j]) << j for j in range(3)])


def matrix_to_quaternion(matrix):
    """
    Given an array of shape (..., 3, 3) holding special orthogonal matrices
    (i.e. rotation matrices), this function returns an array of shape (..., 4)
    providing the same rotations as unit quaternions. The w-entry is at index 3
    and chosen to be non-negative.
    """
    # This implementation is based on the matrix and quaternion FAQ, Q55
    # https://www.j3d.org/matrix_faq/matrfaq_latest.html
    trace = np.trace(matrix, axis1=-1, axis2=-2)
    diag = np.diagonal(matrix, axis1=-1, axis2=-2)
    largest_diag = np.argmax(diag, axis=-1)
    choose_trace = trace > -0.9
    root = 2.0 * np.sqrt(np.where(choose_trace, 1.0 + trace, 1.0 + 2.0 * np.max(diag, axis=1) - trace))
    rcp_root = 1.0 / root
    possible_solutions = [
        # Diagonal entry 0, 0 largest
        np.stack([
            0.25 * root,
            (matrix[..., 1, 0] + matrix[..., 0, 1]) * rcp_root,
            (matrix[..., 0, 2] + matrix[..., 2, 0]) * rcp_root,
            (matrix[..., 2, 1] - matrix[..., 1, 2]) * rcp_root], axis=-1),
        # Diagonal entry 1, 1 largest
        np.stack([
            (matrix[..., 1, 0] + matrix[..., 0, 1]) * rcp_root,
            0.25 * root,
            (matrix[..., 2, 1] + matrix[..., 1, 2]) * rcp_root,
            (matrix[..., 0, 2] - matrix[..., 2, 0]) * rcp_root], axis=-1),
        # Diagonal entry 2, 2 largest
        np.stack([
            (matrix[..., 0, 2] + matrix[..., 2, 0]) * rcp_root,
            (matrix[..., 2, 1] + matrix[..., 1, 2]) * rcp_root,
            0.25 * root,
            (matrix[..., 1, 0] - matrix[..., 0, 1]) * rcp_root], axis=-1),
        # Clearly positive trace
        np.stack([
            (matrix[..., 2, 1] - matrix[..., 1, 2]) * rcp_root,
            (matrix[..., 0, 2] - matrix[..., 2, 0]) * rcp_root,
            (matrix[..., 1, 0] - matrix[..., 0, 1]) * rcp_root,
            0.25 * root], axis=-1),
    ]
    # Pick one
    choice = np.where(choose_trace, 3, largest_diag)
    choice = np.repeat(choice[..., np.newaxis], 4, axis=-1)
    result = np.choose(choice, possible_solutions)
    # Make the real part positive
    result *= np.where(result[..., 3:] >= 0.0, 1.0, -1.0)
    return result


def pack_transformations(matrix):
    """
    Given an array of shape (..., 3, 4) providing transformation matrices, which
    consist of orientation preserving, isotropic scaling, rotation and
    translation, this function generates a compact binary representation.
    :return: A pair constants, data where constants provides 16 floats of meta-
        data about the quantization and data provides 16 consecutive bytes per
        packed matrix.
    """
    # Flatten the first dimensions for convenience
    matrix = np.reshape(matrix, (-1, 3, 4))
    # Extract the scaling
    scaling = np.linalg.norm(matrix[:, :3, :3], ord="fro", axis=(1, 2)) * np.sqrt(1.0 / 3.0)
    # Form quaternions and grab translations
    quaternion = matrix_to_quaternion(matrix[:, :3, :3] / scaling[:, np.newaxis, np.newaxis])
    translations = matrix[:, :, 3]
    # The packed data consists of eight 16-bit fixed-point values. The first
    # four are the unit quaternion, followed by three entries for the position
    # and one for log2 of the scaling.
    log_scaling = np.log2(scaling)
    channels = np.hstack([quaternion, translations, log_scaling[:, np.newaxis]])
    # The fixed-point coordinates cover exactly the used range
    mins = np.min(channels, axis=0)
    maxs = np.max(channels, axis=0)
    factors = 0xffff / (maxs - mins)
    summands = -factors * mins + 0.5
    fixed = np.asarray(channels * factors + summands, dtype=np.uint16)
    inv_factors = 1.0 / factors
    inv_summands = -summands / factors
    return np.concatenate([inv_factors, inv_summands]), pack("H" * fixed.size, *fixed.flat)


class Mesh:
    """Efficiently represents relevant data of a mesh after extracting it from 
       a Blender mesh.
    """

    def __init__(self):
        """Just initializes everything as empty array to have the list of
           attributes somewhere.
        """
        self.primitive_vertex_count = np.zeros(0, dtype=np.uint32)
        self.primitive_vertex_indices = np.zeros(0, dtype=np.uint32)
        self.primitive_material_index = np.zeros(0, dtype=np.uint32)
        self.primitive_vertex_uv = np.zeros(0, dtype=np.float32)
        self.vertex_position = np.zeros((0, 3), dtype=np.float32)
        self.vertex_normal = np.zeros((0, 3), dtype=np.float32)
        self.vertex_line_radius = np.zeros(0, dtype=np.float32)
        self.vertex_group_indices = np.zeros((0, 0), dtype=np.uint16)
        self.vertex_group_weights = np.zeros((0, 0), dtype=np.float32)

    @staticmethod
    def from_blender_mesh(mesh, line_radius):
        """
        Reads the data of the given mesh and returns a Mesh for it.
        :param mesh: A bpy.types.mesh object.
        :param line_radius: If there are no polygons, the mesh is treated as
            collection of lines lines with the given radius. Pass None to fail
            for such meshes.
        :return: If the mesh contains no geometry or incompatible geometry,
            "FAILURE" is returned and an explanation is printed. If the issue
            could be resolved by triangulating the mesh, "TRIANGULATE" is
            returned. Otherwise the mesh is returned.
        """
        result = Mesh()
        # Determine how many vertices there are per polygon and check if that 
        # is acceptable
        line_mode = (len(mesh.polygons) == 0)
        if line_mode and len(mesh.edges) == 0:
            print("Skipping mesh %s because it contains no polygons and no edges." % mesh.name)
            return "FAILURE"
        if line_mode and line_radius is None:
            print("Skipping mesh %s because it contains no polygons and line export is unavailable." % mesh.name)
            return "FAILURE"
        if line_mode:
            print("Treating mesh %s as collection of lines because there seem to be no polygons." % mesh.name)
            edge_count = len(mesh.edges)
            result.primitive_vertex_count = 2 * np.ones((edge_count,), dtype=np.uint32)
            # Generate a flat list of vertex indices for all primitives
            result.primitive_vertex_indices = np.asarray(
                [vertex_index for edge in mesh.edges for vertex_index in edge.vertices], dtype=np.uint32)
            if result.primitive_vertex_indices.size != 2 * edge_count:
                print("It seems that an edge in mesh %s does not have exactly two vertices. Aborting." % mesh.name)
                return "FAILURE"
            # By convention, all edges use material 0
            result.primitive_material_index = np.zeros((edge_count,), dtype=np.uint32)
            # By convention, all texture coordinates are zero (Blender does not 
            # provide any for edges)
            result.primitive_vertex_uv = np.zeros((result.get_primitive_count() * 2, 2), dtype=np.float32)
        else:
            result.primitive_vertex_count = np.asarray([len(polygon.vertices) for polygon in mesh.polygons],
                                                       dtype=np.uint32)
            if result.primitive_vertex_count.min() < 2:
                print("Skipping mesh %s because it has a polygon with only %d vertices." %
                      (mesh.name, result.primitive_vertex_count.min()))
                return "FAILURE"
            if result.primitive_vertex_count.max() > 3:
                return "TRIANGULATE"
            # Generate a flat list of vertex indices for all primitives
            result.primitive_vertex_indices = np.zeros(np.sum(result.primitive_vertex_count), dtype=np.uint32)
            mesh.polygons.foreach_get("vertices", result.primitive_vertex_indices)
            # Store the material index for each primitive
            result.primitive_material_index = np.zeros(len(mesh.polygons), dtype=np.uint32)
            mesh.polygons.foreach_get("material_index", result.primitive_material_index)
            # Grab the data of the first UV layer (making up dummy data if it 
            # does not exist)
            result.primitive_vertex_uv = np.zeros(result.primitive_vertex_indices.size * 2, dtype=np.float32)
            if len(mesh.uv_layers) > 0:
                uv_data = mesh.uv_layers[0].data
                uv_data.foreach_get("uv", result.primitive_vertex_uv)
            result.primitive_vertex_uv = result.primitive_vertex_uv.reshape((result.primitive_vertex_indices.size, 2))
        # Read vertex locations and normals into Numpy arrays. For lines the 
        # normal is irrelevant.
        result.vertex_position = np.zeros(3 * len(mesh.vertices), dtype=np.float32)
        mesh.vertices.foreach_get("co", result.vertex_position)
        result.vertex_position = result.vertex_position.reshape((len(mesh.vertices), 3))
        result.vertex_normal = np.zeros(3 * len(mesh.vertices), dtype=np.float32)
        if not line_mode:
            mesh.vertices.foreach_get("normal", result.vertex_normal)
        else:
            result.vertex_normal[0::3] = 1.0
        result.vertex_normal = result.vertex_normal.reshape((len(mesh.vertices), 3))
        # Read group indices and weights into rectangular arrays
        group_counts = np.asarray([len(vertex.groups) for vertex in mesh.vertices])
        max_group_count = np.max(group_counts)
        result.vertex_group_indices = np.zeros((len(mesh.vertices), max_group_count), dtype=np.uint16)
        result.vertex_group_weights = np.zeros((len(mesh.vertices), max_group_count), dtype=np.float32)
        for i, group_count in enumerate(group_counts):
            mesh.vertices[i].groups.foreach_get("group", result.vertex_group_indices[i, :group_count])
            mesh.vertices[i].groups.foreach_get("weight", result.vertex_group_weights[i, :group_count])
        # Remember the line radius
        if line_mode:
            result.vertex_line_radius = line_radius * np.ones((result.get_vertex_count(),), dtype=np.float32)
        return result

    @staticmethod
    def merge_meshes(meshes, material_slot_remaps, group_index_remaps):
        """
        Creates a new Mesh containing the geometry of all of the given Mesh
        objects. The slot/index remaps are one list per mesh providing a new
        slot/index for each old slot/index.
        """
        result = Mesh()
        # Concatenate vertex arrays
        result.vertex_position = np.vstack([mesh.vertex_position for mesh in meshes])
        result.vertex_normal = np.vstack([mesh.vertex_normal for mesh in meshes])
        result.vertex_line_radius = np.concatenate([mesh.vertex_line_radius for mesh in meshes])
        # Remap group indices
        group_indices_list = [np.asarray(remap)[mesh.vertex_group_indices] for mesh, remap in zip(meshes, group_index_remaps)]
        # Pad group indices and weights to the same group count
        max_group_count = np.max([mesh.vertex_group_indices.shape[1] for mesh in meshes])
        group_indices_list = [np.hstack([indices, np.zeros((indices.shape[0], max_group_count - indices.shape[1]), dtype=np.uint16)]) for indices in group_indices_list]
        group_weights_list = [np.hstack([mesh.vertex_group_weights, np.zeros((mesh.vertex_group_weights.shape[0], max_group_count - mesh.vertex_group_weights.shape[1]), dtype=np.uint16)]) for mesh in meshes]
        # Stack them
        result.vertex_group_indices = np.vstack(group_indices_list)
        result.vertex_group_weights = np.vstack(group_weights_list)
        # Concatenate primitive arrays, but with appropriate index offsets
        result.primitive_vertex_count = np.concatenate([mesh.primitive_vertex_count for mesh in meshes])
        index_offsets = np.cumsum([0] + [mesh.vertex_position.shape[0] for mesh in meshes])
        result.primitive_vertex_indices = np.concatenate([mesh.primitive_vertex_indices + index_offsets[i] for i, mesh in enumerate(meshes)])
        # Remap material slots
        material_index_list = [np.asarray(remap)[mesh.primitive_material_index] for mesh, remap in zip(meshes, material_slot_remaps)]
        result.primitive_material_index = np.concatenate(material_index_list)
        # Merge UVs
        result.primitive_vertex_uv = np.vstack([mesh.primitive_vertex_uv for mesh in meshes])
        return result

    def get_primitive_count(self):
        """Returns the number of primitives in this mesh. Lines, triangles and 
           quads all count as one primitive.
        """
        return self.primitive_vertex_count.size

    def get_vertex_count(self):
        """Returns the number of primitives in this mesh. Lines, triangles and 
           quads all count as one primitive.
        """
        return self.vertex_position.shape[0]

    def transform(self, matrix_3x4):
        """Multiplies vertex data from the left by the given matrix. The last 
           column is added to positions. Normals are multiplied by the inverse 
           transpose of the 3x3 block and renormalized.
          \note The line radius is not scaled.
        """
        inverse_transpose = np.linalg.inv(matrix_3x4[:3, :3]).T
        self.vertex_position = (matrix_3x4[:3, :3] @ self.vertex_position.T).T
        for j in range(3):
            self.vertex_position[:, j] += matrix_3x4[j, 3]
        self.vertex_normal = (inverse_transpose @ self.vertex_normal.T).T
        self.vertex_normal /= np.linalg.norm(self.vertex_normal, axis=1)[:, np.newaxis]
        # Flip the primitive orientation if the transform is not orientation 
        # preserving. We also change the primitive order hoping that nothing 
        # depends on it.
        if np.linalg.det(matrix_3x4[:3, :3]) < 0:
            self.primitive_vertex_count = self.primitive_vertex_count[::-1]
            self.primitive_vertex_indices = self.primitive_vertex_indices[::-1]
            self.primitive_material_index = self.primitive_material_index[::-1]
            self.primitive_vertex_uv = self.primitive_vertex_uv[::-1]

    def normalize_group_weights(self, default_bone_index=0):
        """
        Ensures that all group weights are non-negative and that they sum to
        one for each vertex. If all clamped weights for one vertex are zero, the
        first weight is set to one and the corresponding index is set to the
        given default.
        """
        self.vertex_group_weights = np.maximum(0.0, self.vertex_group_weights)
        weight_sums = self.vertex_group_weights.sum(axis=1)
        self.vertex_group_weights[:, 0] = np.where(weight_sums == 0.0, 1.0, self.vertex_group_weights[:, 0])
        self.vertex_group_indices[:, 0] = np.where(weight_sums == 0.0, default_bone_index, self.vertex_group_indices[:, 0])
        self.vertex_group_weights[weight_sums > 0.0, :] /= weight_sums[weight_sums > 0.0, np.newaxis]


class Scene:
    """
    Handles all relevant objects in a Blender scene, representing their
    relevant data through objects like Mesh.
    """

    def __init__(self, scene, selection_only, add_triangulate_modifier, split_angle, line_radius, frame_step):
        """
        Loads objects from the given scene and transforms all geometry to world
        space.
        :param scene: A bpy.types.scene object.
        :param selection_only: Whether all objects in the scene or only
            selected objects are considered.
        :param split_angle: If this is not None, an edge split modifier is
            added to the end of each modifier stack (unless there is an edge
            split modifier in the stack already). The split angle is set
            accordingly and sharp edges are split as well.
        :param line_radius: Forwards to Mesh.from_blender_mesh().
        :param frame_step: The number of frames between two successive samples.
            Can be a float.
        """
        # Get the list of potentially relevant objects
        if selection_only:
            object_list = bpy.context.selected_objects
        else:
            object_list = scene.objects
        object_list = [(np.eye(4, 4), obj) for obj in object_list]
        object_list = self.handle_instanced_collections(object_list)

        # If we decide to add a triangulate modifier, we need to attempt
        # conversion of a Blender object to a Mesh twice, so we have this 
        # little helper
        def blender_object_to_mesh(obj, first_try):
            if split_angle is not None and first_try:
                has_edge_split = len([mod for mod in obj.modifiers if isinstance(mod, bpy.types.EdgeSplitModifier)]) > 0
                if not has_edge_split:
                    edge_split = obj.modifiers.new("EdgeSplit", "EDGE_SPLIT")
                    if edge_split is not None:
                        print("Added an edge split modifier for object %s." % obj.name)
                        edge_split.use_edge_angle = True
                        edge_split.use_edge_sharp = True
                        edge_split.split_angle = split_angle
            # Disable armature modifiers temporarily
            armature_modifiers = [modifier for modifier in obj.modifiers if modifier.type == "ARMATURE"]
            if len(armature_modifiers):
                armature_show_viewport = armature_modifiers[0].show_viewport
                armature_modifiers[0].show_viewport = False
            # Apply modifiers (except armature modifiers), etc.
            dependencies_graph = bpy.context.evaluated_depsgraph_get()
            evaluated_object = obj.evaluated_get(dependencies_graph)
            try:
                blender_mesh = evaluated_object.to_mesh()
            except RuntimeError:
                # Only message if this outcome was not entirely foreseeable
                if evaluated_object.type not in ["EMPTY", "GPENCIL", "CAMERA", "LIGHT", "SPEAKER", "LIGHT_PROBE", "ARMATURE"]:
                    print("Skipping object %s since it cannot be converted to a mesh." % obj.name)
                return "FAILURE"
            if blender_mesh is None:
                return "FAILURE"
            result = Mesh.from_blender_mesh(blender_mesh, line_radius)
            blender_mesh_name = blender_mesh.name
            blender_mesh = None
            evaluated_object.to_mesh_clear()
            # Reenable armature modifiers
            if len(armature_modifiers):
                armature_modifiers[0].show_viewport = armature_show_viewport
            if result == "TRIANGULATE":
                if not add_triangulate_modifier:
                    print(("Skipping mesh %s because it has a polygon with more than three vertices. "
                           + "Consider allowing the exporter to add triangulate modifiers.") % blender_mesh_name)
                    return "FAILURE"
                elif first_try:
                    print("Adding triangulate modifier for object %s." % obj.name)
                    obj.modifiers.new("Triangulate", "TRIANGULATE")
                    return "RETRY"
                else:
                    print("Mesh %s has been triangulated but it is still incompatible. Skipping it."
                          % blender_mesh_name)
                    return "FAILURE"
            return result

        # This is a dictionary mapping unique bones to unique indices. The keys
        # are tuples of transforms (flattened into tuples), armature object
        # names and bone names
        self.bone_dict = dict()
        # This is a dummy bone with identity transform
        dummy_bone_key = None, None, None
        self.bone_dict[dummy_bone_key] = 0

        # Try to create a Mesh for each of them
        self.mesh_list = list()
        for collection_to_world_space, obj in object_list:
            result = blender_object_to_mesh(obj, True)
            if result == "FAILURE":
                continue
            elif result == "RETRY":
                result = blender_object_to_mesh(obj, False)
            if result == "FAILURE":
                continue
            else:
                mesh = result
            # Check if there is an armature. If so assign unique indices to its
            # bones and bind them to vertex groups.
            armature_modifiers = [modifier for modifier in obj.modifiers if modifier.type == "ARMATURE"]
            if len(armature_modifiers) > 0 and armature_modifiers[0].object is not None:
                armature = armature_modifiers[0].object
                mesh.group_bone_keys = list()
                for group in obj.vertex_groups:
                    key = (tuple(collection_to_world_space.flat), armature.name, group.name)
                    mesh.group_bone_keys.append(key)
                    if key not in self.bone_dict:
                        self.bone_dict[key] = len(self.bone_dict)
            else:
                # Transform the vertex data to world space and associate the
                # mesh with the dummy bone
                mesh.group_bone_keys = [dummy_bone_key]
                transform = collection_to_world_space @ np.asarray(obj.matrix_world)
                mesh.transform(transform[:3, :])
            # Annotate the mesh with the materials used for the various slots
            mesh.slot_material_name = list()
            for material_slot in obj.material_slots:
                if material_slot.material is None:
                    mesh.slot_material_name.append("")
                else:
                    mesh.slot_material_name.append(material_slot.material.name)
            if len(mesh.slot_material_name) == 0:
                mesh.slot_material_name = ["no_material_assigned"]
            self.mesh_list.append(mesh)

        # Define times at which poses are sampled
        self.frame_step = float(frame_step)
        self.frame_start = float(scene.frame_start)
        frame_end = float(scene.frame_end)
        frame_times = np.arange(self.frame_start, frame_end + 1.0e-10, self.frame_step)
        self.frame_count = len(frame_times)
        # Record animations for all bones by playing them back
        old_frame = scene.frame_current, scene.frame_subframe
        self.bone_transforms = np.zeros((self.frame_count, len(self.bone_dict), 3, 4), dtype=np.float32)
        for i, frame in enumerate(frame_times):
            scene.frame_set(int(np.floor(frame)), subframe=frame - np.floor(frame))
            for transform_tuple, armature_name, bone_name in self.bone_dict.keys():
                bone_index = self.bone_dict[transform_tuple, armature_name, bone_name]
                if transform_tuple is None:
                    self.bone_transforms[i, bone_index, :, :3] = np.eye(3)
                    continue
                armature = scene.objects[armature_name]
                if bone_name in armature.pose.bones:
                    pose_bone = armature.pose.bones[bone_name]
                    self.bone_transforms[i, bone_index, :, :] = np.asarray(pose_bone.matrix)[:3, :]
                else:
                    self.bone_transforms[i, bone_index, :, :3] = np.eye(3)
        scene.frame_set(old_frame[0], subframe=old_frame[1])
        # Currently, we have bone to armature space transforms. Now we turn that
        # into object to world space transforms
        for transform_tuple, armature_name, bone_name in self.bone_dict.keys():
            bone_index = self.bone_dict[transform_tuple, armature_name, bone_name]
            if transform_tuple is not None:
                armature = scene.objects[armature_name]
                collection_to_world_space = np.asarray(transform_tuple).reshape(4, 4)
                armature_to_collection_space = armature.matrix_world
                armature_to_world_space = collection_to_world_space @ armature_to_collection_space
                if bone_name in armature.data.bones:
                    bone = armature.data.bones[bone_name]
                    object_to_bone_space = np.linalg.inv(bone.matrix_local)
                else:
                    object_to_bone_space = np.eye(4)
                self.bone_transforms[:, bone_index, :, :] = np.einsum("jk, ikl, lm -> ijm", armature_to_world_space[:3, :3], self.bone_transforms[:, bone_index, :, :], object_to_bone_space)
                self.bone_transforms[:, bone_index, :, 3] += armature_to_world_space[np.newaxis, :3, 3]

    def handle_instanced_collections(self, object_list):
        """Given a list of pairs with 4x4 numpy collection to world space 
           transforms and bpy.types.obj, this function returns a new list of 
           the same form that additionally contains instances that should 
           exist according to the instance_collection flag.
        """
        if len(object_list) == 0:
            return list()
        instanced_object_list = list()
        for collection_to_world_space, obj in object_list:
            instantiator_object_to_world_space = np.asarray(obj.matrix_world)
            instanced_collection_to_world_space = collection_to_world_space @ instantiator_object_to_world_space
            instanced_collection = obj.instance_collection
            if obj.instance_type == "COLLECTION" and instanced_collection is not None:
                for instanced_object in instanced_collection.all_objects:
                    instanced_object_list.append((instanced_collection_to_world_space, instanced_object))
        return object_list + self.handle_instanced_collections(instanced_object_list)

    def get_material_set(self):
        """Returns a frozenset with the names of all materials used in this 
           scene.
        """
        material_list = list()
        for mesh in self.mesh_list:
            material_list.extend(mesh.slot_material_name)
        return frozenset(material_list)

    def get_merged_mesh(self):
        """This function merges all meshes in this scene into a single mesh.
           Material indices are updated as needed. The returned Mesh has a valid
           slot_material_name providing material names for each slot index.
           Group indices are all made to match self.bone_dict."""
        if len(self.mesh_list) == 0:
            return None
        # Merge material lists
        used_material_list = sorted(list(self.get_material_set()))
        material_index_dict = dict([(name, i) for i, name in enumerate(used_material_list)])
        # Merge meshes sequentially
        material_remap_list = [[material_index_dict[name] for name in mesh.slot_material_name] for mesh in self.mesh_list]
        group_remap_list = [[self.bone_dict[key] for key in mesh.group_bone_keys] for mesh in self.mesh_list]
        merged_mesh = Mesh.merge_meshes(self.mesh_list, material_remap_list, group_remap_list)
        merged_mesh.slot_material_name = used_material_list
        merged_mesh.group_bone_keys = [None] * len(self.bone_dict)
        for key, index in self.bone_dict.items():
            merged_mesh.group_bone_keys[index] = key
        return merged_mesh


def export_scene(blender_scene, scene_file_path, selection_only, add_triangulate_modifier, split_angle, sort_triangles, frame_step):
    """
    Exports the given bpy.types.scene to the *.vks file at the given path. Some
    parameters forward to Scene.__init__().
    """
    print()
    print("-###- Beginning Vulkan renderer export to %s. -###-" % scene_file_path)
    if path.splitext(scene_file_path)[1] == ".blend":
        scene_file_path = path.splitext(scene_file_path)[0] + ".vks"
    # Bring the scene into Python objects
    scene = Scene(blender_scene, selection_only, add_triangulate_modifier, split_angle, None, frame_step)
    if len(scene.mesh_list) == 0:
        print("No meshes are available to export. Aborting.")
        return
    # Merge together all meshes of the scene with updated material indices
    mesh = scene.get_merged_mesh()
    mesh.normalize_group_weights()
    used_material_list = mesh.slot_material_name
    # Abort if something does not use triangles
    triangle_count = np.count_nonzero(mesh.primitive_vertex_count == 3)
    if triangle_count != mesh.primitive_vertex_count.size:
        print("Some geometry does not consist of triangles only (%d primitives but only %d triangles). "
              % (mesh.primitive_vertex_count.size, triangle_count)
              + "Allow the exporter to add triangulate modifiers to export anyway.")
        return
    # Sort triangles by the Morton code of their centroid
    if sort_triangles:
        centroids = np.zeros((triangle_count, 3), dtype=np.float32)
        for j in range(3):
            coordinate = mesh.vertex_position[:, j][mesh.primitive_vertex_indices]
            centroids[:, j] = coordinate.reshape((triangle_count, 3)).mean(axis=1)
        morton = get_morton_code_3d(centroids, centroids.min(axis=0), centroids.max(axis=0))
        triangle_permutation = np.argsort(morton)
        for j in range(3):
            mesh.primitive_vertex_indices[j::3] = mesh.primitive_vertex_indices[j::3][triangle_permutation]
            mesh.primitive_vertex_uv[j::3] = mesh.primitive_vertex_uv[j::3][triangle_permutation]
        mesh.primitive_material_index = mesh.primitive_material_index[triangle_permutation]
    # Open the output file
    file = open(scene_file_path, "wb")
    # Write file format marker and version
    file.write(pack("II", 0x00abcabc, 2))
    # Write the number of materials, primitives and vertices
    file.write(pack("QQ", len(used_material_list), triangle_count))
    # Quantize vertex positions to 21 bits per coordinate
    box_min = mesh.vertex_position.min(axis=0)[np.newaxis, :]
    box_max = mesh.vertex_position.max(axis=0)[np.newaxis, :]
    quantization_factor = (2.0**21.0 / (box_max - box_min))
    quantization_offset = -box_min * quantization_factor
    quantized_positions = np.asarray(mesh.vertex_position * quantization_factor + quantization_offset, dtype=np.uint32)
    quantized_positions = np.minimum(2**21 - 1, quantized_positions)
    # Write the constants needed for dequantization
    dequantization_factor = 1.0 / quantization_factor
    dequantization_summand = box_min + 0.5 * dequantization_factor
    file.write(pack("fff", *dequantization_factor.flat))
    file.write(pack("fff", *dequantization_summand.flat))
    # Write the number of bone influences per vertex and the number of unique
    # bone index tuples
    sorted_indices = np.take_along_axis(mesh.vertex_group_indices, np.argsort(mesh.vertex_group_weights, axis=1), axis=1)
    max_tuple_count = np.unique(sorted_indices, axis=0).shape[0]
    file.write(pack("QQ", mesh.vertex_group_indices.shape[1], max_tuple_count))
    # Write meta data about animations
    frame_start_in_secs = scene.frame_start / blender_scene.render.fps
    frame_step_in_secs = scene.frame_step / blender_scene.render.fps
    file.write(pack("ffQQ", frame_start_in_secs, frame_step_in_secs, scene.frame_count, scene.bone_transforms.shape[1]))

    # Make a few changes to material names to support ORCA assets
    used_material_list = [re.sub(r"\.[0-9][0-9][0-9]$", "", name) for name in used_material_list]
    used_material_list = [name.replace(".DoubleSided", "") for name in used_material_list]
    # Write the material names as null-terminated strings, preceded by their
    # lengths
    for material_name in used_material_list:
        file.write(pack("Q", len(material_name)))
        file.write(material_name.encode("utf-8"))
        file.write(pack("b", 0))
    # Write the vertex positions
    indices = mesh.primitive_vertex_indices
    packed_positions = np.zeros((mesh.get_vertex_count(), 2), dtype=np.uint32)
    packed_positions[:, 0] = quantized_positions[:, 0]
    packed_positions[:, 0] += (quantized_positions[:, 1] & 0x7FF) << 21
    packed_positions[:, 1] = (quantized_positions[:, 1] & 0x1FF800) >> 11
    packed_positions[:, 1] += quantized_positions[:, 2] << 10
    triangle_list_positions = packed_positions[indices, :]
    file.write(pack("I" * triangle_list_positions.size, *triangle_list_positions.flat))
    # Pack texture coordinate pairs into 32 bit. We allow a texture to
    # repeat up to eight times within one triangle.
    triangle_vertex_uv = mesh.primitive_vertex_uv.reshape((indices.size // 3, 3, 2))
    triangle_min_uv = triangle_vertex_uv.min(axis=1)
    triangle_vertex_uv -= np.floor(triangle_min_uv)[:, np.newaxis, :]
    if np.max(triangle_vertex_uv) > 8.0:
        print("A mesh has %d triangles where the UV coordinates imply more than seven repetitions. "
              % np.count_nonzero(np.any(triangle_vertex_uv > 8.0, axis=(1, 2)))
              + "In the most extreme case, there are %f repetitions. " % np.max(triangle_vertex_uv)
              + "The used quantization does not support that. Coordinates will be clipped.")
    normal_and_uv = np.zeros((indices.size, 4), dtype=np.uint16)
    packed_uv = triangle_vertex_uv * ((2.0**16 - 1.0) / 8.0) + 0.5
    normal_and_uv[:, 2:4] = np.asarray(np.clip(packed_uv.reshape((-1, 2)), 0.0, 2.0**16.0 - 1.0), dtype=np.uint16)
    # Pack normals into 32 bit
    packed_normal_0, packed_normal_1 = encode_normal_32_bit(mesh.vertex_normal)
    normal_and_uv[:, 0] = packed_normal_0[indices]
    normal_and_uv[:, 1] = packed_normal_1[indices]
    # Write normal vectors and texture coordinates
    file.write(pack("H" * normal_and_uv.size, *normal_and_uv.flat))
    # Write uncompressed bone indices and weights
    triangle_list_group_indices = mesh.vertex_group_indices[indices, :]
    file.write(pack("H" * triangle_list_group_indices.size, *triangle_list_group_indices.flat))
    triangle_list_group_weights = mesh.vertex_group_weights[indices, :]
    file.write(pack("f" * triangle_list_group_weights.size, *triangle_list_group_weights.flat))
    # Write the material index for each primitive
    file.write(pack("B" * triangle_count, *mesh.primitive_material_index))
    # Write the sampled animations
    transform_meta_data, transform_data = pack_transformations(scene.bone_transforms)
    file.write(pack("f" * transform_meta_data.size, *transform_meta_data))
    file.write(transform_data)
    # Write an end of file marker
    file.write(pack("I", 0x00e0fe0f))
    file.close()
    print("Wrote %d materials, %d primitives, %d bones and %d bones per vertex." % (len(used_material_list), triangle_count, len(scene.bone_dict), mesh.vertex_group_indices.shape[1]))
    print("-###- Export completed. -###-")
    print()


# Provide meta information for Blender
bl_info = {
    "name": "Vulkan renderer animated scene exporter (*.vks)",
    "author": "Christoph Peters",
    "version": (2, 0, 0),
    "blender": (2, 82, 0),
    "location": "file > Export",
    "description": "This addon exports animated scenes from Blender to the Vulkan renderer.",
    "warning": "",
    "category": "Import-Export"
}


class VulkanExportOperator(bpy.types.Operator):
    """This operator exports Blender scenes to the Vulkan renderer (*.vks).
    """

    def invoke(self, context, _event):
        """Shows a file selection dialog for the *.vks file."""
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        """Performs the export using the current settings and context."""
        export_path = bpy.path.abspath(self.filepath)
        export_scene(context.scene, export_path, self.selection_only, self.add_triangulate_modifier,
                     self.edge_split_angle if self.add_edge_split_modifier else None,
                     self.sort_triangles, self.frame_step)
        return {"FINISHED"}

    # Settings of this operator
    filepath: bpy.props.StringProperty(name="file path",
                                       description="Path to the *.vks file. "
                                                   + "Additional files are written with the same path prefix",
                                       subtype="FILE_PATH")
    selection_only: bpy.props.BoolProperty(name="Selection only", description="Export selected objects only",
                                           default=True)
    add_triangulate_modifier: bpy.props.BoolProperty(name="Add triangulate modifier",
                                                     description="Add triangulate modifiers to meshes that cannot be "
                                                                 + "exported otherwise. "
                                                                 + "THIS CHANGES YOUR SCENE PERMANENTLY",
                                                     default=False)
    add_edge_split_modifier: bpy.props.BoolProperty(name="Add edge split modifier",
                                                    description="Add edge split modifiers to meshes that do not have "
                                                                + "them yet. "
                                                                + "THIS CHANGES YOUR SCENE PERMANENTLY",
                                                    default=False)
    edge_split_angle: bpy.props.FloatProperty(name="Split angle",
                                              description="The split angle set for new edge split modifiers.",
                                              subtype="ANGLE", unit="ROTATION", min=0.0, max=np.pi,
                                              default=30.0 * np.pi / 180.0)
    frame_step: bpy.props.FloatProperty(name="Frame step",
                                        description="The distance in frames between two successive samples of the animation.",
                                        subtype="TIME", unit="TIME", min=0.01, max=100.0,
                                        default=0.5)
    sort_triangles: bpy.props.BoolProperty(name="Sort triangles",
                                           description="Sort triangles of the mesh by the Morton code of their "
                                                       + "centroid. This may improve memory coherence in rendering",
                                           default=True)
    # Controls file extension filters in the dialog
    filter_glob: bpy.props.StringProperty(default="*.vks", options={'HIDDEN'})
    # Some more meta-data
    bl_idname = "export_scene.vulkan_renderer"
    bl_label = "Export Vulkan renderer scene"
    bl_options = {"PRESET"}


def add_export_operator_to_menu(self, _context):
    """Adds the exporter to the export menu.
    """
    self.layout.operator(VulkanExportOperator.bl_idname, text="Vulkan renderer scene (.vks)")


def register():
    bpy.utils.register_class(VulkanExportOperator)
    bpy.types.TOPBAR_MT_file_export.append(add_export_operator_to_menu)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(add_export_operator_to_menu)
    bpy.utils.unregister_class(VulkanExportOperator)


if __name__ == "__main__":
    register()
