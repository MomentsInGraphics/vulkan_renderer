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


#include "linear_light.h"
#include <stdint.h>
#include <math.h>

void update_linear_light(linear_light_t* light) {
	float length_squared = 0.0f;
	for (uint32_t i = 0; i != 3; ++i) {
		light->begin_to_end[i] = light->end[i] - light->begin[i];
		length_squared += light->begin_to_end[i] * light->begin_to_end[i];
	}
	light->line_length = sqrtf(length_squared);
	for (uint32_t i = 0; i != 3; ++i)
		light->line_direction[i] = light->begin_to_end[i] / light->line_length;
}
