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


/*! Decodes a normal encoded into two 16-bit UNORM numbers using octahedral
	maps. The returned normal vector is guaranteed to be normalized.*/
vec3 decode_normal_32_bit(vec2 octahedral_normal) {
	// To be able to represent 0 exactly, -1.0f corresponds to the
	// second-smallest fixed point number. Compensate for that.
	const uint bit_count = 16;
	const float factor = 2.0f * (65534.0f / 65535.0f);
	const float summand = -(32768.0f / 65535.0f) * factor;
	octahedral_normal = fma(octahedral_normal, vec2(factor), vec2(summand));
	// Undo the octahedral map
	vec3 normal = vec3(octahedral_normal.xy, 1.0f - abs(octahedral_normal.x) - abs(octahedral_normal.y));
	vec2 sign_not_zero = vec2(
		(octahedral_normal.x >= 0.0f) ? 1.0f : -1.0f,
		(octahedral_normal.y >= 0.0f) ? 1.0f : -1.0f);
	normal.xy = (normal.z < 0.0f) ? ((1.0f - abs(normal.yx))*sign_not_zero) : normal.xy;
	return normalize(normal);
}


/*! Unpacks the given position that is quantized using 21-bits per coordinate
	and packed into two 32-bit unsigned integers.*/
vec3 decode_position_64_bit(uvec2 quantized_position, vec3 dequantization_factor, vec3 dequantization_summand) {
	vec3 position = vec3(
		quantized_position[0] & 0x1FFFFF,
		((quantized_position[0] & 0xFFE00000) >> 21) | ((quantized_position[1] & 0x3FF) << 11),
		(quantized_position[1] & 0x7FFFFC00) >> 10
	);
	return fma(position, dequantization_factor, dequantization_summand);
}
