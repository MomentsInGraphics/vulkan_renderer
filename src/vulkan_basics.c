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


#include "vulkan_basics.h"
#include "string_utilities.h"
#include "math_utilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int create_vulkan_device(device_t* device, const char* application_internal_name, uint32_t physical_device_index, VkBool32 request_ray_tracing) {
	// Clear the object
	memset(device, 0, sizeof(device_t));
	// Initialize GLFW
	if (!glfwInit()) {
		printf("GLFW initialization failed.\n");
		return 1;
	}
	// Create a Vulkan instance
	VkApplicationInfo application_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = application_internal_name,
		.pEngineName = application_internal_name,
		.applicationVersion = 100,
		.engineVersion = 100,
		.apiVersion = VK_MAKE_VERSION(1, 2, 0),
	};
	uint32_t surface_extension_count;
	const char** surface_extension_names = glfwGetRequiredInstanceExtensions(&surface_extension_count);
	device->instance_extension_count = surface_extension_count;
	device->instance_extension_names = malloc(sizeof(char*) * device->instance_extension_count);
	for (uint32_t i = 0; i != surface_extension_count; ++i)
		device->instance_extension_names[i] = surface_extension_names[i];
	const char* layer_names[] = { "VK_LAYER_KHRONOS_validation" };
	VkInstanceCreateInfo instance_create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &application_info,
		.enabledExtensionCount = device->instance_extension_count,
		.ppEnabledExtensionNames = device->instance_extension_names,
#ifdef NDEBUG
		.enabledLayerCount = 0,
#else
		.enabledLayerCount = COUNT_OF(layer_names),
#endif
		.ppEnabledLayerNames = layer_names
	};
	VkResult result;
	if (result = vkCreateInstance(&instance_create_info, NULL, &device->instance)) {
		printf("Failed to create a Vulkan instance (error code %d) with the following extensions and layers:\n", result);
		for (uint32_t i = 0; i != instance_create_info.enabledExtensionCount; ++i)
			printf("%s\n", instance_create_info.ppEnabledExtensionNames[i]);
		for (uint32_t i = 0; i != instance_create_info.enabledLayerCount; ++i)
			printf("%s\n", instance_create_info.ppEnabledLayerNames[i]);
		printf("Please check that Vulkan is installed properly and try again. Or try running the release build, which disables validation layers.\n");
		destroy_vulkan_device(device);
		return 1;
	}
	// Figure out what physical device should be used
	if (vkEnumeratePhysicalDevices(device->instance, &device->physical_device_count, NULL)) {
		printf("Failed to enumerate physical devices (e.g. GPUs) to be used with Vulkan.\n");
		destroy_vulkan_device(device);
		return 1;
	}
	device->physical_devices = malloc(sizeof(VkPhysicalDevice) * device->physical_device_count);
	if (vkEnumeratePhysicalDevices(device->instance, &device->physical_device_count, device->physical_devices)) {
		destroy_vulkan_device(device);
		return 1;
	}
	printf("The following physical devices (GPUs) are available to Vulkan:\n");
	for (uint32_t i = 0; i != device->physical_device_count; ++i) {
		VkPhysicalDeviceProperties device_properties;
		vkGetPhysicalDeviceProperties(device->physical_devices[i], &device_properties);
		printf("%u - %s%s\n", i, device_properties.deviceName, (i == physical_device_index) ? " (used)" : "");
		if (i == physical_device_index) {
			device->physical_device_properties = device_properties;
		}
	}
	if (physical_device_index >= device->physical_device_count) {
		printf("The physical device with index %u is to be used but does not exist.\n", physical_device_index);
		destroy_vulkan_device(device);
		return 1;
	}
	device->physical_device = device->physical_devices[physical_device_index];
	// Enumerate available memory types
	vkGetPhysicalDeviceMemoryProperties(device->physical_device, &device->memory_properties);
	// Learn about available queues
	vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device, &device->queue_family_count, NULL);
	if (!device->queue_family_count) {
		printf("No Vulkan queue family available.\n");
		destroy_vulkan_device(device);
		return 1;
	}
	device->queue_family_properties = malloc(sizeof(VkQueueFamilyProperties) * device->queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device, &device->queue_family_count, device->queue_family_properties);
	// Pick a queue that supports graphics and compute
	uint32_t required_queue_flags = VK_QUEUE_GRAPHICS_BIT & VK_QUEUE_COMPUTE_BIT;
	for (device->queue_family_index = 0;
		device->queue_family_index < device->queue_family_count
		&& (device->queue_family_properties[device->queue_family_index].queueFlags & required_queue_flags);
		++device->queue_family_index)
	{
	}
	if (device->queue_family_index == device->queue_family_count) {
		printf("No Vulkan queue family supports graphics and compute.\n");
		destroy_vulkan_device(device);
		return 1;
	}
	// Figure out whether ray queries are supported
	if (request_ray_tracing) {
		uint32_t extension_count = 0;
		vkEnumerateDeviceExtensionProperties(device->physical_device, NULL, &extension_count, NULL);
		VkExtensionProperties* extensions = malloc(sizeof(VkExtensionProperties) * extension_count);
		if (vkEnumerateDeviceExtensionProperties(device->physical_device, NULL, &extension_count, extensions))
			extension_count = 0;
		for (uint32_t i = 0; i != extension_count; ++i)
			if (strcmp(extensions[i].extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0)
				device->ray_tracing_supported = VK_TRUE;
		free(extensions);
	}
	// Select device extensions
	const char* base_device_extension_names[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
		VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
	};
	const char* ray_tracing_device_extension_names[] = {
		VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_QUERY_EXTENSION_NAME,
	};
	device->device_extension_count = COUNT_OF(base_device_extension_names);
	if (device->ray_tracing_supported)
		device->device_extension_count += COUNT_OF(ray_tracing_device_extension_names);
	device->device_extension_names = malloc(sizeof(char*) * device->device_extension_count);
	for (uint32_t i = 0; i != COUNT_OF(base_device_extension_names); ++i)
		device->device_extension_names[i] = base_device_extension_names[i];
	if (device->ray_tracing_supported)
		for (uint32_t i = 0; i != COUNT_OF(ray_tracing_device_extension_names); ++i)
			device->device_extension_names[COUNT_OF(base_device_extension_names) + i] = ray_tracing_device_extension_names[i];
	// Create a device
	float queue_priorities[1] = { 0.0f };
	VkDeviceQueueCreateInfo queue_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueCount = 1,
		.pQueuePriorities = queue_priorities,
		.queueFamilyIndex = device->queue_family_index
	};
	VkPhysicalDeviceFeatures enabled_features = {
		.shaderSampledImageArrayDynamicIndexing = VK_TRUE,
		.samplerAnisotropy = VK_TRUE,
	};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
		.accelerationStructure = VK_TRUE,
	};
	VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
		.pNext = &acceleration_structure_features,
		.rayQuery = VK_TRUE,
	};
	VkPhysicalDeviceVulkan12Features enabled_new_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = device->ray_tracing_supported ? &ray_query_features : NULL,
		.descriptorIndexing = VK_TRUE,
		.uniformAndStorageBuffer8BitAccess = VK_TRUE,
		.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
		.bufferDeviceAddress = device->ray_tracing_supported,
	};
	VkDeviceCreateInfo device_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &enabled_new_features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queue_info,
		.enabledExtensionCount = device->device_extension_count,
		.ppEnabledExtensionNames = device->device_extension_names,
		.pEnabledFeatures = &enabled_features
	};
	if (vkCreateDevice(device->physical_device, &device_info, NULL, &device->device)) {
		printf("Failed to create a Vulkan device with the following extensions:\n");
		for (uint32_t i = 0; i != device_info.enabledExtensionCount; ++i) {
			printf("%s\n", device_info.ppEnabledExtensionNames[i]);
		}
		destroy_vulkan_device(device);
		return 1;
	}
	// Query acceleration structure properties
	if (device->ray_tracing_supported) {
		device->acceleration_structure_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
		VkPhysicalDeviceProperties2KHR device_properties = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,
			.pNext = &device->acceleration_structure_properties,
		};
		vkGetPhysicalDeviceProperties2(device->physical_device, &device_properties);
	}
	// Create a command pool for each queue
	VkCommandPoolCreateInfo command_pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = device->queue_family_index,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
	};
	if (vkCreateCommandPool(device->device, &command_pool_info, NULL, &device->command_pool)) {
		printf("Failed to create a command pool for a queue that supports graphics and compute.\n");
		destroy_vulkan_device(device);
		return 1;
	}
	// Grab the selected queue
	vkGetDeviceQueue(device->device, device->queue_family_index, 0, &device->queue);
	// Give feedback about ray tracing
	if (device->ray_tracing_supported)
		printf("Ray tracing is available.\n");
	else if (request_ray_tracing)
		printf("Ray tracing was requested but is unavailable. Try installing the latest GPU drivers or using a different physical device.\n");
	return 0;
}

void destroy_vulkan_device(device_t* device) {
	if (device->command_pool) vkDestroyCommandPool(device->device, device->command_pool, NULL);
	free(device->queue_family_properties);
	if (device->device) vkDestroyDevice(device->device, NULL);
	free(device->physical_devices);
	if (device->instance) vkDestroyInstance(device->instance, NULL);
	free(device->instance_extension_names);
	free(device->device_extension_names);
	glfwTerminate();
	// Mark the object as cleared
	memset(device, 0, sizeof(*device));
}


/*! As a helper for swapchain resize and destroy_swapchain(), this function
	destroys and clears the given swapchain object, except that it keeps the
	window, swapchain and surface.*/
void partially_destroy_old_swapchain(swapchain_t* swapchain, const device_t* device) {
	if(swapchain->image_views)
		for (uint32_t i = 0; i != swapchain->image_count; ++i)
			vkDestroyImageView(device->device, swapchain->image_views[i], NULL);
	free(swapchain->image_views);
	free(swapchain->images);
	free(swapchain->present_modes);
	free(swapchain->surface_formats);
	// Mark the object as cleared except for swapchain and window
	swapchain_t cleared = {
		.window = swapchain->window,
		.swapchain = swapchain->swapchain,
		.surface = swapchain->surface
	};
	(*swapchain) = cleared;
}


int create_or_resize_swapchain(swapchain_t* swapchain, const device_t* device, VkBool32 resize,
	const char* application_display_name, uint32_t width, uint32_t height, VkBool32 use_vsync) 
{
	swapchain_t old_swapchain = {0};
	if (resize) {
		partially_destroy_old_swapchain(swapchain, device);
		old_swapchain = *swapchain;
	}
	memset(swapchain, 0, sizeof(*swapchain));
	// Create a window
	if (resize) {
		swapchain->window = old_swapchain.window;
		old_swapchain.window = NULL;
	}
	else {
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		swapchain->window = glfwCreateWindow(width, height, application_display_name, NULL, NULL);
		if (!swapchain->window) {
			printf("Window creation with GLFW failed.\n");
			destroy_swapchain(&old_swapchain, device);
			destroy_swapchain(swapchain, device);
			return 1;
		}
	}
	// Create a surface for the swap chain
	if (resize) {
		swapchain->surface = old_swapchain.surface;
		old_swapchain.surface = NULL;
	}
	else {
		if (glfwCreateWindowSurface(device->instance, swapchain->window, NULL, &swapchain->surface)) {
			const char* error_message = NULL;
			glfwGetError(&error_message);
			printf("Failed to create a surface:\n%s\n", error_message);
			destroy_swapchain(&old_swapchain, device);
			destroy_swapchain(swapchain, device);
			return 1;
		}
	}
	// Abort if the surface and the chosen queue does not support presentation
	VkBool32 presentation_supported;
	if (vkGetPhysicalDeviceSurfaceSupportKHR(device->physical_device, device->queue_family_index, swapchain->surface, &presentation_supported)
		|| presentation_supported == VK_FALSE)
	{
		printf("Failed to ascertain that the used surface supports presentation on screen.\n");
		destroy_swapchain(&old_swapchain, device);
		destroy_swapchain(swapchain, device);
		return 1;
	}
	// Determine an appropriate surface format
	if (vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical_device, swapchain->surface, &swapchain->surface_format_count, NULL)) {
		printf("Failed to query available surface formats.\n");
		destroy_swapchain(&old_swapchain, device);
		destroy_swapchain(swapchain, device);
		return 1;
	}
	swapchain->surface_formats = malloc(sizeof(VkSurfaceFormatKHR) * swapchain->surface_format_count);
	swapchain->format = VK_FORMAT_UNDEFINED;
	if (vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical_device, swapchain->surface, &swapchain->surface_format_count, swapchain->surface_formats)) {
		printf("Failed to query available surface formats.\n");
		destroy_swapchain(&old_swapchain, device);
		destroy_swapchain(swapchain, device);
		return 1;
	}
	if (swapchain->surface_format_count == 1 && swapchain->surface_formats[0].format == VK_FORMAT_UNDEFINED)
		swapchain->format = VK_FORMAT_B8G8R8A8_UNORM;
	for (uint32_t i = 0; i != swapchain->surface_format_count; ++i) {
		if (swapchain->surface_formats[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			continue;
		VkFormat format = swapchain->surface_formats[i].format;
		if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB
			|| format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB
			|| format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || format == VK_FORMAT_A2R10G10B10_UNORM_PACK32)
		{
			swapchain->format = format;
		}
	}
	if (swapchain->format == VK_FORMAT_UNDEFINED) {
		printf("Unable to determine an appropriate surface format. Only R8G8B8A8, B8G8R8A8, A2R10G10B10 or A2B10G10R10 formats are supported.\n");
		destroy_swapchain(swapchain, device);
		return 1;
	}
	// Query surface capabilities and present modes
	VkSurfaceCapabilitiesKHR surface_capabilities;
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical_device, swapchain->surface, &surface_capabilities)) {
		printf("Failed to query surface capabilities of the physical device.\n");
		destroy_swapchain(&old_swapchain, device);
		destroy_swapchain(swapchain, device);
		return 1;
	}
	swapchain->present_mode_count;
	if (vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical_device, swapchain->surface, &swapchain->present_mode_count, NULL)) {
		printf("Failed to query presentation modes of the physical device.\n");
		destroy_swapchain(&old_swapchain, device);
		destroy_swapchain(swapchain, device);
		return 1;
	}
	swapchain->present_modes = malloc(sizeof(VkPresentModeKHR) * swapchain->present_mode_count);
	if (vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical_device, swapchain->surface, &swapchain->present_mode_count, swapchain->present_modes)) {
		printf("Failed to query presentation modes of the physical device.\n");
		destroy_swapchain(&old_swapchain, device);
		destroy_swapchain(swapchain, device);
		return 1;
	}
	int window_width = 0, window_height = 0;
	glfwGetFramebufferSize(swapchain->window, &window_width, &window_height);
	swapchain->extent.width = (surface_capabilities.currentExtent.width != 0xFFFFFFFF) ? surface_capabilities.currentExtent.width : window_width;
	swapchain->extent.height = (surface_capabilities.currentExtent.height != 0xFFFFFFFF) ? surface_capabilities.currentExtent.height : window_height;
	if (swapchain->extent.width * swapchain->extent.height == 0) {
		destroy_swapchain(&old_swapchain, device);
		return 2;
	}
	if (width != swapchain->extent.width || height != swapchain->extent.height)
		printf("The swapchain resolution is %ux%u.\n", swapchain->extent.width, swapchain->extent.height);
	// Find a supported composite alpha mode (one of these is guaranteed to be
	// set)
	VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	VkCompositeAlphaFlagBitsKHR composite_alpha_flags[4] = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
	};
	for (uint32_t i = 0; i < COUNT_OF(composite_alpha_flags); ++i) {
		if (surface_capabilities.supportedCompositeAlpha & composite_alpha_flags[i]) {
			composite_alpha = composite_alpha_flags[i];
			break;
		}
	}
	VkPresentModeKHR no_vsync_present_mode = VK_PRESENT_MODE_FIFO_KHR;
	for (uint32_t i = 0; i != swapchain->present_mode_count; ++i) {
		if (swapchain->present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR
		&& no_vsync_present_mode == VK_PRESENT_MODE_FIFO_KHR)
			no_vsync_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		if (swapchain->present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
			no_vsync_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
	}
	if (no_vsync_present_mode == VK_PRESENT_MODE_FIFO_KHR)
		printf("No presentation mode without vertical synchronization is available. Enabling v-sync instead.\n");
	uint32_t requested_image_count = 2;
	if (requested_image_count < surface_capabilities.minImageCount)
		requested_image_count = surface_capabilities.minImageCount;
	if (requested_image_count > surface_capabilities.maxImageCount && surface_capabilities.maxImageCount != 0)
		requested_image_count = surface_capabilities.maxImageCount;
	VkSwapchainCreateInfoKHR swapchain_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = swapchain->surface,
		.minImageCount = requested_image_count,
		.imageFormat = swapchain->format,
		.imageExtent = swapchain->extent,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = composite_alpha,
		.imageArrayLayers = 1,
		.presentMode = use_vsync ? VK_PRESENT_MODE_FIFO_KHR : no_vsync_present_mode,
		.clipped = VK_FALSE,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	};
	if (resize)
		swapchain_info.oldSwapchain = old_swapchain.swapchain;
	if (vkCreateSwapchainKHR(device->device, &swapchain_info, NULL, &swapchain->swapchain)) {
		const char* error_message = NULL;
		glfwGetError(&error_message);
		printf("Failed to create a swap chain. Vulkan reports:\n%s\n", error_message);
		destroy_swapchain(&old_swapchain, device);
		destroy_swapchain(swapchain, device);
		return 1;
	}
	destroy_swapchain(&old_swapchain, device);
	swapchain->image_count = 0;
	vkGetSwapchainImagesKHR(device->device, swapchain->swapchain, &swapchain->image_count, NULL);
	if (swapchain->image_count == 0) {
		printf("The created swap chain has no images.\n");
		destroy_swapchain(swapchain, device);
		return 1;
	}
	swapchain->images = malloc(swapchain->image_count * sizeof(VkImage));
	if (vkGetSwapchainImagesKHR(device->device, swapchain->swapchain, &swapchain->image_count, swapchain->images)) {
		printf("Failed to retrieve swapchain images.\n");
		destroy_swapchain(swapchain, device);
		return 1;
	}
	swapchain->image_views = malloc(swapchain->image_count * sizeof(VkImageView));
	for (uint32_t i = 0; i < swapchain->image_count; i++) {
		VkImageViewCreateInfo color_image_view = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain->images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = swapchain->format,
			.components.r = VK_COMPONENT_SWIZZLE_R,
			.components.g = VK_COMPONENT_SWIZZLE_G,
			.components.b = VK_COMPONENT_SWIZZLE_B,
			.components.a = VK_COMPONENT_SWIZZLE_A,
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.levelCount = 1,
			.subresourceRange.layerCount = 1
		};
		if (vkCreateImageView(device->device, &color_image_view, NULL, &swapchain->image_views[i])) {
			printf("Failed to create a view onto swapchain image %u.\n", i);
			destroy_swapchain(swapchain, device);
			return 1;
		}
	}
	return 0;
}


void destroy_swapchain(swapchain_t* swapchain, const device_t* device) {
	partially_destroy_old_swapchain(swapchain, device);
	if (swapchain->swapchain)
		vkDestroySwapchainKHR(device->device, swapchain->swapchain, NULL);
	if (swapchain->surface)
		vkDestroySurfaceKHR(device->instance, swapchain->surface, NULL);
	if (swapchain->window)
		glfwDestroyWindow(swapchain->window);
	// Mark the object as cleared
	memset(swapchain, 0, sizeof(*swapchain));
}


int find_memory_type(uint32_t* type_index, const device_t* device, uint32_t memory_type_bits, VkMemoryPropertyFlags property_mask) {
	for (uint32_t i = 0; i < device->memory_properties.memoryTypeCount; ++i) {
		if (memory_type_bits & (1 << i)) {
			if ((device->memory_properties.memoryTypes[i].propertyFlags & property_mask) == property_mask) {
				*type_index = i;
				return 0;
			}
		}
	}
	return 1;
}


void print_image_requests(const image_request_t* image_requests, uint32_t image_count) {
	printf("A description of each requested image follows:\n");
	for (uint32_t i = 0; i != image_count; ++i) {
		const VkImageCreateInfo* image_info = &image_requests[i].image_info;
		uint32_t mip_map_count = image_info->mipLevels;
		if (mip_map_count == 0)
			mip_map_count = get_mipmap_count_3d(image_info->extent);
		printf("%u: %ux%ux%u, %u layers, %u mipmaps, format %d.\n",
			i, image_info->extent.width,image_info->extent.height,image_info->extent.depth,
			image_info->arrayLayers, mip_map_count, image_info->format);
	}
}


int create_images(images_t* images, const device_t* device,
	const image_request_t* requests, uint32_t image_count, VkMemoryPropertyFlags memory_properties)
{
	// Mark the output cleared
	memset(images, 0, sizeof(*images));
	images->memory_properties = memory_properties;
	if (image_count == 0)
		// That was easy
		return 0;
	// Create the images
	images->images = malloc(sizeof(image_t) * image_count);
	memset(images->images, 0, sizeof(image_t) * image_count);
	images->image_count = image_count;
	for (uint32_t i = 0; i != image_count; ++i) {
		image_t* image = &images->images[i];
		image->image_info = requests[i].image_info;
		if (image->image_info.mipLevels == 0)
			image->image_info.mipLevels = get_mipmap_count_3d(image->image_info.extent);
		if (vkCreateImage(device->device, &image->image_info, NULL, &image->image)) {
			printf("Failed to create image %u.\n", i);
			print_image_requests(requests, image_count);
			destroy_images(images, device);
			return 1;
		}
	}
	// See which images prefer dedicated allocations
	uint32_t dedicated_count = 0;
	for (uint32_t i = 0; i != image_count; ++i) {
		VkMemoryDedicatedRequirements memory_dedicated_requirements = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS
		};
		VkMemoryRequirements2 memory_requirements_2 = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
			.pNext = &memory_dedicated_requirements
		};
		VkImageMemoryRequirementsInfo2 memory_requirements_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
			.image = images->images[i].image
		};
		vkGetImageMemoryRequirements2(device->device, &memory_requirements_info, &memory_requirements_2);
		images->images[i].dedicated_allocation = memory_dedicated_requirements.prefersDedicatedAllocation;
		if (images->images[i].dedicated_allocation == VK_TRUE)
			++dedicated_count;
	}
	// Get ready to allocate memory
	uint32_t shared_count = (dedicated_count == image_count) ? 0 : 1;
	images->memory_count = shared_count + dedicated_count;
	images->memories = malloc(sizeof(VkDeviceMemory) * images->memory_count);
	memset(images->memories, 0, sizeof(VkDeviceMemory) * images->memory_count);
	// Make all dedicated allocations and bind
	uint32_t allocation_index = shared_count;
	for (uint32_t i = 0; i != image_count; ++i) {
		image_t* image = &images->images[i];
		if (!image->dedicated_allocation)
			continue;
		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(device->device, image->image, &memory_requirements);
		VkMemoryDedicatedAllocateInfo dedicated_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
			.image = image->image
		};
		image->memory_size = memory_requirements.size;
		VkMemoryAllocateInfo memory_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = &dedicated_info,
			.allocationSize = memory_requirements.size
		};
		if (find_memory_type(&memory_info.memoryTypeIndex, device, memory_requirements.memoryTypeBits, images->memory_properties)) {
			printf("Failed to find an acceptable memory type for image %u.\n", i);
			print_image_requests(requests, image_count);
			destroy_images(images, device);
			return 1;
		}
		if (vkAllocateMemory(device->device, &memory_info, NULL, &images->memories[allocation_index])) {
			printf("Failed to allocate memory for image %u.\n", i);
			print_image_requests(requests, image_count);
			destroy_images(images, device);
			return 1;
		}
		// Bind memory
		image->memory_index = allocation_index;
		if (vkBindImageMemory(device->device, image->image, images->memories[allocation_index], 0)) {
			printf("Failed to bind memory for image %u.\n", i);
			print_image_requests(requests, image_count);
			destroy_images(images, device);
			return 1;
		}
		++allocation_index;
	}
	// Combine requirements for the shared allocation and figure out memory
	// offsets and allocation size
	uint32_t shared_memory_types = 0xFFFFFFFF;
	VkDeviceSize current_size = 0;
	for (uint32_t i = 0; i != image_count; ++i) {
		image_t* image = &images->images[i];
		if (image->dedicated_allocation)
			continue;
		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(device->device, image->image, &memory_requirements);
		image->memory_size = memory_requirements.size;
		shared_memory_types &= memory_requirements.memoryTypeBits;
		image->memory_offset = align_memory_offset(current_size, memory_requirements.alignment);
		current_size = image->memory_offset + memory_requirements.size;
	}
	// Perform the shared allocation
	if (shared_count) {
		VkMemoryAllocateInfo memory_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = current_size
		};
		if (find_memory_type(&memory_info.memoryTypeIndex, device, shared_memory_types, images->memory_properties)) {
			printf("Failed to find an acceptable memory type for images sharing memory. Check your requests and consider using two separate pools.\n");
			print_image_requests(requests, image_count);
			destroy_images(images, device);
			return 1;
		}
		if (vkAllocateMemory(device->device, &memory_info, NULL, &images->memories[0])) {
			printf("Failed to allocate %llu bytes of memory for images sharing memory.\n", memory_info.allocationSize);
			print_image_requests(requests, image_count);
			destroy_images(images, device);
			return 1;
		}
		// Bind the memory
		for (uint32_t i = 0; i != image_count; ++i) {
			image_t* image = &images->images[i];
			if (image->dedicated_allocation)
				continue;
			image->memory_index = 0;
			if (vkBindImageMemory(device->device, image->image, images->memories[0], image->memory_offset)) {
				printf("Failed to bind memory for image %u.\n", i);
				print_image_requests(requests, image_count);
				destroy_images(images, device);
				return 1;
			}
		}
	}
	// Create the views
	for (uint32_t i = 0; i != image_count; ++i) {
		image_t* image = &images->images[i];
		image->view_info = requests[i].view_info;
		image->view_info.format = requests[i].image_info.format;
		image->view_info.image = image->image;
		if (image->view_info.subresourceRange.layerCount == 0)
			image->view_info.subresourceRange.layerCount = image->image_info.arrayLayers - image->view_info.subresourceRange.baseArrayLayer;
		if (image->view_info.subresourceRange.levelCount == 0)
			image->view_info.subresourceRange.levelCount = image->image_info.mipLevels - image->view_info.subresourceRange.baseMipLevel;
		if (image->view_info.sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO) {
			if (vkCreateImageView(device->device, &image->view_info, NULL, &image->view)) {
				printf("Failed to create a view for image %u.\n", i);
				print_image_requests(requests, image_count);
				destroy_images(images, device);
				return 1;
			}
		}
	}
	return 0;
}


void destroy_images(images_t* images, const device_t* device) {
	// Destroy the individual images
	for (uint32_t i = 0; i != images->image_count; ++i) {
		image_t* image = &images->images[i];
		if (image->view) vkDestroyImageView(device->device, image->view, NULL);
		if (image->image) vkDestroyImage(device->device, image->image, NULL);
	}
	free(images->images);
	// Free the memory allocations
	for (uint32_t i = 0; i != images->memory_count; ++i) {
		if (images->memories[i])
			vkFreeMemory(device->device, images->memories[i], NULL);
	}
	free(images->memories);
	// Mark the object cleared
	memset(images, 0, sizeof(*images));
}


int create_aligned_buffers(buffers_t* buffers, const device_t* device, const VkBufferCreateInfo* buffer_infos, uint32_t buffer_count, VkMemoryPropertyFlags memory_properties, VkDeviceSize alignment) {
	memset(buffers, 0, sizeof(*buffers));
	buffers->buffer_count = buffer_count;
	if (buffer_count == 0)
		return 0;
	// Prepare the output data structure
	buffers->memory = NULL;
	buffers->buffers = malloc(sizeof(buffer_t) * buffers->buffer_count);
	memset(buffers->buffers, 0, sizeof(buffer_t) * buffers->buffer_count);
	// Create buffers
	VkMemoryAllocateFlags memory_allocate_flags = 0;
	for (uint32_t i = 0; i != buffers->buffer_count; ++i) {
		if (vkCreateBuffer(device->device, &buffer_infos[i], NULL, &buffers->buffers[i].buffer)) {
			printf("Failed to create a buffer of size %llu.\n", buffer_infos[i].size);
			destroy_buffers(buffers, device);
			return 1;
		}
		if (buffer_infos[i].usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
			memory_allocate_flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
	}
	// Allocate memory with proper alignment
	VkDeviceSize current_size = 0;
	uint32_t memory_type_bits = 0xFFFFFFFF;
	for (uint32_t i = 0; i != buffers->buffer_count; ++i) {
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(device->device, buffers->buffers[i].buffer, &memory_requirements);
		memory_type_bits &= memory_requirements.memoryTypeBits;
		buffers->buffers[i].size = buffer_infos[i].size;
		VkDeviceSize combined_alignment = least_common_multiple(alignment, memory_requirements.alignment);
		buffers->buffers[i].offset = align_memory_offset(current_size, combined_alignment);
		current_size = buffers->buffers[i].offset + memory_requirements.size;
	}
	VkMemoryAllocateFlagsInfo allocation_flags = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		.flags = memory_allocate_flags,
		.deviceMask = 1,
	};
	VkMemoryAllocateInfo allocation_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = (memory_allocate_flags != 0) ? &allocation_flags : NULL,
		.allocationSize = current_size
	};
	buffers->size = allocation_info.allocationSize;
	if (find_memory_type(&allocation_info.memoryTypeIndex, device, memory_type_bits, memory_properties)) {
		printf("Failed to find an appropirate memory type for %u buffers with memory properties %u.\n", buffers->buffer_count, memory_properties);
		destroy_buffers(buffers, device);
		return 1;
	}
	if (vkAllocateMemory(device->device, &allocation_info, NULL, &buffers->memory)) {
		printf("Failed to allocate %llu bytes of memory for %u buffers.\n", allocation_info.allocationSize, buffers->buffer_count);
		destroy_buffers(buffers, device);
		return 1;
	}
	// Bind memory
	for (uint32_t i = 0; i != buffers->buffer_count; ++i) {
		if (vkBindBufferMemory(device->device, buffers->buffers[i].buffer, buffers->memory, buffers->buffers[i].offset)) {
			destroy_buffers(buffers, device);
			return 1;
		}
	}
	return 0;
}


void destroy_buffers(buffers_t* buffers, const device_t* device) {
	for (uint32_t i = 0; i != buffers->buffer_count; ++i)
		if (buffers->buffers && buffers->buffers[i].buffer)
			vkDestroyBuffer(device->device, buffers->buffers[i].buffer, NULL);
	if(buffers->memory) vkFreeMemory(device->device, buffers->memory, NULL);
	free(buffers->buffers);
	memset(buffers, 0, sizeof(*buffers));
}


int copy_buffers_and_images(const device_t* device,
	uint32_t buffer_count, const VkBuffer* source_buffers, const VkBuffer* destination_buffers, VkBufferCopy* buffer_regions,
	uint32_t image_count, const VkImage* source_images, const VkImage* destination_images, VkImageLayout source_layout,
	VkImageLayout destination_layout_before, VkImageLayout destination_layout_after, VkImageCopy* image_regions,
	uint32_t buffer_to_image_count, const VkBuffer* image_source_buffers, const VkImage* buffer_destination_images,
	VkImageLayout buffer_destination_layout_before, VkImageLayout buffer_destination_layout_after, VkBufferImageCopy* buffer_to_image_regions)
{
	VkCommandBufferAllocateInfo command_buffer_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool = device->command_pool,
		.commandBufferCount = 1
	};
	VkCommandBuffer command_buffer;
	if (vkAllocateCommandBuffers(device->device, &command_buffer_info, &command_buffer)) {
		return 1;
	}
	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	if (vkBeginCommandBuffer(command_buffer, &begin_info)) {
		vkFreeCommandBuffers(device->device, device->command_pool, 1, &command_buffer);
		return 1;
	}
	// Transition all images to transfer source/destination layout
	VkImageMemoryBarrier* barriers = NULL;
	uint32_t barrier_count = 0;
	VkImageLayout intermediate_source_layout = source_layout;
	if (image_count + buffer_to_image_count) {
		barriers = malloc(sizeof(VkImageMemoryBarrier) * (2 * image_count + buffer_to_image_count));
		for (uint32_t i = 0; i != 2; ++i) {
			uint32_t count = (i == 0) ? image_count : buffer_to_image_count;
			for (uint32_t j = 0; j != count; ++j) {
				VkImageSubresourceLayers subresource = (i == 0) ? image_regions[j].dstSubresource : buffer_to_image_regions[j].imageSubresource;
				VkImageMemoryBarrier barrier = {
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.oldLayout = (i == 0) ? destination_layout_before : buffer_destination_layout_before,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.image = (i == 0) ? destination_images[j] : buffer_destination_images[j],
					.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
					.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.subresourceRange = {
						.aspectMask = subresource.aspectMask,
						.baseMipLevel = subresource.mipLevel,
						.levelCount = 1,
						.baseArrayLayer = subresource.baseArrayLayer,
						.layerCount = subresource.layerCount
					}
				};
				barriers[barrier_count++] = barrier;
				if (i == 0 && source_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && source_layout != VK_IMAGE_LAYOUT_GENERAL) {
					barrier.oldLayout = source_layout;
					barrier.newLayout = intermediate_source_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					barrier.image = source_images[j];
					barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
					barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					barriers[barrier_count++] = barrier;
				}
			}
		}
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 0, NULL, 0, NULL, barrier_count, barriers);
	}
	// Do the copies
	for (uint32_t i = 0; i != buffer_count; ++i)
		vkCmdCopyBuffer(command_buffer, source_buffers[i], destination_buffers[i], 1, &buffer_regions[i]);
	for (uint32_t i = 0; i != image_count; ++i)
		vkCmdCopyImage(command_buffer, source_images[i], intermediate_source_layout, destination_images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_regions[i]);
	for (uint32_t i = 0; i != buffer_to_image_count; ++i)
		vkCmdCopyBufferToImage(command_buffer, image_source_buffers[i], buffer_destination_images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_to_image_regions[i]);
	// Transition destination images to the requested layout and source images
	// back to the original layout
	if (image_count + buffer_to_image_count) {
		barrier_count = 0;
		for (uint32_t i = 0; i != 2; ++i) {
			uint32_t count = (i == 0) ? image_count : buffer_to_image_count;
			for (uint32_t j = 0; j != count; ++j) {
				VkImageSubresourceLayers subresource = (i == 0) ? image_regions[j].dstSubresource : buffer_to_image_regions[j].imageSubresource;
				VkImageMemoryBarrier barrier = {
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.newLayout = (i == 0) ? destination_layout_after : buffer_destination_layout_after,
					.image = (i == 0) ? destination_images[j] : buffer_destination_images[j],
					.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
					.subresourceRange = {
						.aspectMask = subresource.aspectMask,
						.baseMipLevel = subresource.mipLevel,
						.levelCount = 1,
						.baseArrayLayer = subresource.baseArrayLayer,
						.layerCount = subresource.layerCount
					}
				};
				if (barrier.oldLayout != barrier.newLayout)
					barriers[barrier_count++] = barrier;
				if (i == 0 && source_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && source_layout != VK_IMAGE_LAYOUT_GENERAL) {
					barrier.oldLayout = intermediate_source_layout;
					barrier.newLayout = source_layout;
					barrier.image = source_images[j];
					barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
					barriers[barrier_count++] = barrier;
				}
			}
		}
		if (barrier_count)
			vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0, 0, NULL, 0, NULL, barrier_count, barriers);
	}
	free(barriers);
	// Transfer all images to the requested layouts
	vkEndCommandBuffer(command_buffer);
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &command_buffer
	};
	VkResult result = vkQueueSubmit(device->queue, 1, &submit_info, NULL);
	result |= vkQueueWaitIdle(device->queue);
	vkFreeCommandBuffers(device->device, device->command_pool, 1, &command_buffer);
	return result;
}


/*! Returns the standardized name for the given shader stage, e.g. "vert" or
	"frag". Only one bit of VkShaderStageFlagBits can be set in the input.*/
const char* get_shader_stage_name(VkShaderStageFlags stage) {
	switch (stage) {
	case VK_SHADER_STAGE_VERTEX_BIT: return "vert";
	case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return "tesc";
	case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return "tese";
	case VK_SHADER_STAGE_GEOMETRY_BIT: return "geom";
	case VK_SHADER_STAGE_FRAGMENT_BIT: return "frag";
	case VK_SHADER_STAGE_COMPUTE_BIT: return "comp";
	case VK_SHADER_STAGE_RAYGEN_BIT_KHR: return "rgen";
	case VK_SHADER_STAGE_INTERSECTION_BIT_KHR: return "rint";
	case VK_SHADER_STAGE_ANY_HIT_BIT_KHR: return "rahit";
	case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR: return "rchit";
	case VK_SHADER_STAGE_MISS_BIT_KHR: return "rmiss";
	case VK_SHADER_STAGE_CALLABLE_BIT_KHR: return "rcall";
	case VK_SHADER_STAGE_TASK_BIT_NV: return "task";
	case VK_SHADER_STAGE_MESH_BIT_NV: return "mesh";
	default: return NULL;
	};
}


int compile_glsl_shader(shader_t* shader, const device_t* device, const shader_request_t* request) {
	if (!get_shader_stage_name(request->stage)) {
		printf("Invalid stage specification %u passed for shader %s.", request->stage, request->shader_file_path);
		return 1;
	}
	// Verify that the shader file exists by opening and closing it
#ifndef NDEBUG
	FILE* shader_file = fopen(request->shader_file_path, "r");
	if (!shader_file) {
		printf("The shader file at path %s does not exist or cannot be opened.\n", request->shader_file_path);
		return 1;
	}
	fclose(shader_file);
#endif
	// Delete the prospective output file such that we can verify its existence
	// to see if the compiler did anything
	const char* spirv_path_pieces[] = {request->shader_file_path, ".spv"};
	char* spirv_path = concatenate_strings(COUNT_OF(spirv_path_pieces), spirv_path_pieces);
	remove(spirv_path);
	// Build the part of the command line for defines
	const char** define_pieces = malloc(sizeof(char*) * 2 * request->define_count);
	for (uint32_t i = 0; i != request->define_count; ++i) {
		define_pieces[2 * i + 0] = " -D";
		define_pieces[2 * i + 1] = request->defines[i];
	}
	char* concatenated_defines = concatenate_strings(2 * request->define_count, define_pieces);
	free(define_pieces);
	// Construct the command line
	const char* command_line_pieces[] = {
		"glslangValidator -V100 --target-env spirv1.5 ",
		"-S ", get_shader_stage_name(request->stage),
#ifndef NDEBUG
		" -g -Od ",
#endif
		concatenated_defines,
		" -I\"", request->include_path, "\" ",
		"--entry-point ", request->entry_point,
		" -o \"", spirv_path,
		"\" \"", request->shader_file_path, "\""
	};
	char* command_line = concatenate_strings(COUNT_OF(command_line_pieces), command_line_pieces);
	free(concatenated_defines);
	// Check whether command processing is available at all
#ifndef NDEBUG
	if (!system(NULL)) {
		printf("No command processor is available. Cannot invoke the compiler to compile the shader at path %s.\n", request->shader_file_path);
		free(command_line);
		free(spirv_path);
		return 1;
	}
#endif
	// Invoke the command line and see whether it produced an output file
	system(command_line);
	FILE* file = fopen(spirv_path, "rb");
	if (!file) {
		printf("glslangValidator failed to compile the shader at path %s. The full command line is:\n%s\n", request->shader_file_path, command_line);
		free(command_line);
		free(spirv_path);
		return 1;
	}
	free(command_line);
	free(spirv_path);
	// Read the SPIR-V code from the file
	if (fseek(file, 0, SEEK_END) || (shader->spirv_size = ftell(file)) < 0) {
		printf("Failed to determine the file size for the compiled shader %s.", spirv_path);
		fclose(file);
		return 1;
	}
	shader->spirv_code = malloc(shader->spirv_size);
	fseek(file, 0, SEEK_SET);
	shader->spirv_size = fread(shader->spirv_code, sizeof(char), shader->spirv_size, file);
	fclose(file);
	// Create the Vulkan shader module
	VkShaderModuleCreateInfo module_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = shader->spirv_size,
		.pCode = shader->spirv_code
	};
	if (vkCreateShaderModule(device->device, &module_info, NULL, &shader->module)) {
		printf("Failed to create a shader module from %s.\n", request->shader_file_path);
		destroy_shader(shader, device);
	}
	return 0;
}


int compile_glsl_shader_with_second_chance(shader_t* shader, const device_t* device, const shader_request_t* request) {
	while (compile_glsl_shader(shader, device, request)) {
		printf("Try again (Y/n)? ");
		char response;
		scanf("%1c", &response);
		if (response == 'N' || response == 'n') {
			printf("\nGiving up.\n");
			return 1;
		}
		else
			printf("\nTrying again.\n");
	}
	return 0;
}


void destroy_shader(shader_t* shader, const device_t* device) {
	if(shader->module) vkDestroyShaderModule(device->device, shader->module, NULL);
	free(shader->spirv_code);
	memset(shader, 0, sizeof(*shader));
}


int create_descriptor_sets(pipeline_with_bindings_t* pipeline, const device_t* device, const descriptor_set_request_t* request, uint32_t descriptor_set_count) {
	memset(pipeline, 0, sizeof(*pipeline));
	// Copy and complete the bindings
	VkDescriptorSetLayoutBinding* bindings = malloc(sizeof(VkDescriptorSetLayoutBinding) * request->binding_count);
	for (uint32_t i = 0; i != request->binding_count; ++i) {
		bindings[i] = request->bindings[i];
		bindings[i].binding = i;
		bindings[i].stageFlags |= request->stage_flags;
		bindings[i].descriptorCount = (bindings[i].descriptorCount < request->min_descriptor_count)
			? request->min_descriptor_count
			: bindings[i].descriptorCount;
	}
	// Create the descriptor set layout
	VkDescriptorSetLayoutCreateInfo descriptor_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = request->binding_count,
		.pBindings = bindings
	};
	if (vkCreateDescriptorSetLayout(device->device, &descriptor_layout_info, NULL, &pipeline->descriptor_set_layout)) {
		printf("Failed to create a descriptor set layout.\n");
		free(bindings);
		destroy_pipeline_with_bindings(pipeline, device);
		return 1;
	}

	// Create the pipeline layout using only this descriptor layout
	VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &pipeline->descriptor_set_layout
	};
	if (vkCreatePipelineLayout(device->device, &pipeline_layout_info, NULL, &pipeline->pipeline_layout)) {
		printf("Failed to create a pipeline layout from a single descriptor set layout.\n");
		free(bindings);
		destroy_pipeline_with_bindings(pipeline, device);
		return 1;
	}

	// Create a descriptor pool with the requested number of descriptor sets
	VkDescriptorPoolSize* descriptor_pool_sizes = malloc(request->binding_count * sizeof(VkDescriptorPoolSize));
	for (uint32_t i = 0; i != request->binding_count; ++i) {
		descriptor_pool_sizes[i].type = bindings[i].descriptorType;
		descriptor_pool_sizes[i].descriptorCount = bindings[i].descriptorCount * descriptor_set_count;
	}
	free(bindings);
	VkDescriptorPoolCreateInfo descriptor_pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = descriptor_set_count,
		.poolSizeCount = request->binding_count,
		.pPoolSizes = descriptor_pool_sizes
	};
	if (vkCreateDescriptorPool(device->device, &descriptor_pool_info, NULL, &pipeline->descriptor_pool)) {
		printf("Failed to create a descriptor pool to allocate %u descriptor sets.\n", descriptor_set_count);
		free(descriptor_pool_sizes);
		destroy_pipeline_with_bindings(pipeline, device);
		return 1;
	}
	free(descriptor_pool_sizes);
	VkDescriptorSetLayout* layouts = malloc(sizeof(VkDescriptorSetLayout) * descriptor_set_count);
	for (uint32_t i = 0; i != descriptor_set_count; ++i)
		layouts[i] = pipeline->descriptor_set_layout;
	VkDescriptorSetAllocateInfo descriptor_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pipeline->descriptor_pool,
		.descriptorSetCount = descriptor_set_count,
		.pSetLayouts = layouts
	};
	pipeline->descriptor_sets = malloc(sizeof(VkDescriptorSet) * descriptor_set_count);
	if (vkAllocateDescriptorSets(device->device, &descriptor_alloc_info, pipeline->descriptor_sets)) {
		printf("Failed to create %u descriptor sets.\n", descriptor_alloc_info.descriptorSetCount);
		free(layouts);
		destroy_pipeline_with_bindings(pipeline, device);
		return 1;
	}
	free(layouts);
	return 0;
}


void complete_descriptor_set_write(uint32_t write_count, VkWriteDescriptorSet* writes, const descriptor_set_request_t* request) {
	for (uint32_t i = 0; i != write_count; ++i) {
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		if (writes[i].dstBinding < request->binding_count) {
			writes[i].descriptorType = request->bindings[writes[i].dstBinding].descriptorType;
			writes[i].descriptorCount = request->bindings[writes[i].dstBinding].descriptorCount;
		}
		writes[i].descriptorCount = (writes[i].descriptorCount < request->min_descriptor_count)
			? request->min_descriptor_count
			: writes[i].descriptorCount;
	}
}


void destroy_pipeline_with_bindings(pipeline_with_bindings_t* pipeline, const device_t* device) {
	if (pipeline->pipeline)
		vkDestroyPipeline(device->device, pipeline->pipeline, NULL);
	if (pipeline->descriptor_pool)
		vkDestroyDescriptorPool(device->device, pipeline->descriptor_pool, NULL);
	free(pipeline->descriptor_sets);
	if (pipeline->pipeline_layout)
		vkDestroyPipelineLayout(device->device, pipeline->pipeline_layout, NULL);
	if (pipeline->descriptor_set_layout)
		vkDestroyDescriptorSetLayout(device->device, pipeline->descriptor_set_layout, NULL);
	memset(pipeline, 0, sizeof(*pipeline));
}
