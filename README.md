This is a toy renderer written in C using Vulkan. It is intentionally
minimalist. It has been developed and used for the papers "BRDF Importance 
Sampling for Polygonal Lights" and "BRDF Importance Sampling for Linear
Lights." Correspondingly, there are two branches. For more information see:
https://momentsingraphics.de/ToyRendererOverview.html


## Downloading Data
Scenes and other data that are needed to run the renderer are available on the
websites for the papers:
http://momentsingraphics.de/Siggraph2021.html
http://momentsingraphics.de/HPG2021.html


## Building the Renderer

The renderer is written in C99 with few dependencies. Imgui and GLFW are
specified as submodules, so you should be able to clone them alongside this
repository (use git clone --recurse-submodules). You also need the latest
Vulkan SDK (version 1.2.176.1 or later) available here:
https://vulkan.lunarg.com/sdk/home

On Linux the Vulkan SDK should be available via your package manager.
Validation layers are needed for a debug build but not for a release build.
You may have to use beta drivers, depending on what your package repositories
provide otherwise.
https://developer.nvidia.com/vulkan-driver

Vulkan on macOS is notoriously problematic but it may work with the latest SDK.

Once all dependencies are available, use CMake to create project files and
build.


## Running the Renderer

Get data files first (see above). Run the binary with current working directory 
vulkan_renderer. 

Ray  tracing uses the extension VK_KHR_ray_query. The latest NVIDIA drivers for
Windows support it:
https://www.nvidia.com/download/index.aspx

To use ray tracing, you will also need a GPU that supports it. That includes
all GPUs with RTX in the name. Pascal or Turing GPUs do not support the
required extensions. AMD GPUs of the Radeon RX 6000 series should also work but
have not been tested.

You can run the application without ray tracing but your driver has to support
Vulkan 1.2. If the demo does not start, take a look at its command line output.


## Important Code Files

The GLSL implementation of our techniques is found in:
src/shaders/polygon_sampling.glsl
src/shaders/line_sampling.glsl (on the corresponding branch)

The complete shading pass, including our multiple importance sampling, is part
of:
src/shaders/shading_pass.frag.glsl


## Licenses

Most code in this package is licensed under the terms of the GPLv3. However,
you have the option to use the core of the sampling methods in
polygon_sampling.glsl and line_sampling.glsl under the BSD license. See the 
comments at the top of each file for details.

