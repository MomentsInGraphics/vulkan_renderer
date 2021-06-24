cmake_minimum_required (VERSION 3.11)

# Define an executable target
project(vulkan_renderer)
add_executable(vulkan_renderer)
target_compile_definitions(vulkan_renderer
	PUBLIC _CRT_SECURE_NO_WARNINGS
	PUBLIC GLFW_INCLUDE_NONE)

# Specify the required C standard
set_target_properties(vulkan_renderer PROPERTIES C_STANDARD 99)
set_target_properties(vulkan_renderer PROPERTIES CMAKE_C_STANDARD_REQUIRED True)

# Add all of the source code for the renderer itself
add_subdirectory(src)

# Add dear imgui source code
include_directories(ext/imgui)
include_directories(ext/imgui/backends)
target_sources(vulkan_renderer PRIVATE
	ext/imgui/imgui.cpp
	ext/imgui/imgui_draw.cpp
	ext/imgui/imgui_widgets.cpp
	ext/imgui/imgui_tables.cpp
	ext/imgui/backends/imgui_impl_glfw.cpp
)

# Add Vulkan as dependency
find_package(Vulkan REQUIRED)

# Add GLFW as dependency that will be compiled alongside this project
set(GLFW_BUILD_DOCS False)
set(GLFW_BUILD_EXAMPLES False)
set(GLFW_BUILD_TESTS False)
set(GLFW_VULKAN_STATIC True)
add_subdirectory(ext/glfw)
target_link_libraries(vulkan_renderer PRIVATE Vulkan::Vulkan glfw)
