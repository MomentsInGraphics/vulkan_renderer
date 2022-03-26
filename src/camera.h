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

//! Needed for keyboard and mouse input
typedef struct GLFWwindow GLFWwindow;

/*! Holds state for a first person camera that characterizes the world to
	projection space transform completely, except for the aspect ratio. It also
	provides enough information to update the camera interactively. It does not
	store any transforms or other redundant information. Such information has
	to be computed as needed.*/
typedef struct first_person_camera_s {
	//! The position of the camera in world space
	float position_world_space[3];
	//! The rotation of the camera around the global z-axis in radians
	float rotation_z;
	//! The rotation of the camera around the local x-axis in radians. Without
	//! rotation the camera looks into the negative z-direction.
	float rotation_x;
	//! The vertical field of view (top to bottom) in radians
	float vertical_fov;
	//! The distance of the near plane and the far plane to the camera position
	float near, far;
	//! The default speed of this camera in meters per second when it moves
	//! along a single axis
	float speed;
	//! 1 iff mouse movements are currently used to rotate the camera
	int rotate_camera;
	//! The rotation that the camera would have if the mouse cursor were moved
	//! to coordinate (0, 0) with rotate_camera enabled
	float rotation_x_0, rotation_z_0;
} first_person_camera_t;


//! Constructs the world to view space transform for the given camera
void get_world_to_view_space(float world_to_view_space[4][4], const first_person_camera_t* camera);

//! Constructs the view to projection space transform for the given camera and
//! the given width / height ratio
void get_view_to_projection_space(float view_to_projection_space[4][4], const first_person_camera_t* camera, float aspect_ratio);

//! Constructs the world to projection space transform for the given camera and
//! the given width / height ratio
void get_world_to_projection_space(float world_to_projection_space[4][4], const first_person_camera_t* camera, float aspect_ratio);

/*! Implements camera controls based on keyboard and mouse input obtained from
	GLFW.
	\param camera The camera that will be updated.
	\param window The window whose input is used for controlling the camera.
	\return The time in seconds since the last call. This should be used to
		control the time step for other time dependent processes.*/
float control_camera(first_person_camera_t* camera, GLFWwindow* window);
