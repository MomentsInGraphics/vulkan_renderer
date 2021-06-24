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
#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"


//! Uses imgui to define the complete user interface. Makes changes to the
//! given application in response to user input. Keeps record of necessary
//! changes that have to be performed by the calling side. Pass the frame time
//! so it can be displayed.
void specify_user_interface(application_updates_t* updates, application_t* app, float frame_time);

#ifdef __cplusplus
}
#endif
