This is a toy renderer written in C using Vulkan. It is intentionally
minimalist. It has been developed and used for the papers "BRDF Importance 
Sampling for Polygonal Lights" and "BRDF Importance Sampling for Linear
Lights." This branch holds a variant with animated models for the paper
"Permutation Coding for Vertex-Blend Attribute Compression". For more
information see:
https://momentsingraphics.de/ToyRendererOverview.html


## Downloading Data

Scenes and other data that are needed to run the renderer are available on the
website for the paper:
http://momentsingraphics.de/I3D2022.html


## Building the Renderer

The renderer is written in C99 with few dependencies. Imgui and GLFW are
bundled with this package. However, you need the latest Vulkan SDK (version
1.2.176.1 or later) available here:
https://vulkan.lunarg.com/sdk/home

On Linux the Vulkan SDK should be available via your package manager.
Validation layers are needed for a debug build but not for a release build.
Vulkan on macOS is notoriously problematic but it may work with the latest SDK.

Once all dependencies are available, use CMake to create project files and
build. Run the binary with current working directory vulkan_renderer.


## Running the Renderer

Get data files first (see above). Run the binary with current working directory 
vulkan_renderer. 


## Important Code Files

The GLSL implementation of our technique is found in:
src/shaders/blend_attribute_compression.glsl

A C port is found in:
src/permutation_coding.h

The read me in the root directory has more info on how to use it. Other aspects
of the compression method (especially table construction) are found in:
src/blend_attribute_compression.c

The vertex shader that uses it all is:
src/shaders/forward_pass.vert.glsl


## Licenses

Most code in this package is licensed under the terms of the GPLv3. However,
you have the option to use the core of the compression methods under the BSD
license. See the comments at the top of each file for details.
