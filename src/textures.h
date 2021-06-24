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

/*! Loads 2D textures from files into GPU memory.
    \param textures Upon success, this object holds all loaded textures in the
        order specified through file_paths. The calling side takes
        responsibility to free it using destroy_images().
    \param device The used Vulkan device.
    \param texture_count The number of textures to be loaded.
    \param file_paths texture_count null-terminated strings providing paths to
        the texture files being loaded. The files must be in the *.vkt format,
        produced by the accompanying texture conversion tool.
    \param usage Usage flags that should be specified for the created images. A
        typical choice is VK_IMAGE_USAGE_SAMPLED_BIT.
        VK_IMAGE_USAGE_TRANSFER_DST_BIT is specified automatically.
    \return 0 upon success.
    \note Since this function always creates a new memory allocation, it is
        advisable to load many textures at once.*/
int load_2d_textures(images_t* textures, const device_t* device, uint32_t texture_count, const char* const* file_paths, VkBufferUsageFlags usage);
