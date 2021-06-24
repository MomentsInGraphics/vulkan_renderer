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


//! Converts linear RGB values (rec. 709) between 0 and 1 to sRGB between 0 and
//! 1. Includes clamping of out of gamut colors. This function applies to one
//! component only, use convert_linear_rgb_to_srgb() for a complete color.
float convert_linear_to_srgb(float linear_channel) {
    linear_channel = clamp(linear_channel, 0.0f, 1.0f);
	return (linear_channel <= 0.0031308f)
		? (12.92f * linear_channel)
		: (1.055f * pow(linear_channel, 1.0f / 2.4f) - 0.055f);
}

//! Vector version of convert_linear_to_srgb()
vec3 convert_linear_rgb_to_srgb(vec3 linear_rgb) {
	return vec3(
		convert_linear_to_srgb(linear_rgb.r),
		convert_linear_to_srgb(linear_rgb.g),
		convert_linear_to_srgb(linear_rgb.b)
	);
}


//! Converts sRGB values between 0 and 1 to linear RGB values (rec. 709)
//! between 0 and 1. Includes clamping of out of gamut colors. This function
//! applies to one component only, use convert_srgb_to_linear_rgb() for a
//! complete color.
float convert_srgb_to_linear(float srgb_channel) {
    srgb_channel = clamp(srgb_channel, 0.0f, 1.0f);
	return (srgb_channel <= 0.04045f)
		? ((1.0f / 12.92f) * srgb_channel)
		: pow(fma(srgb_channel, 1.0f / 1.055f, 0.055f / 1.055f), 2.4f);
}
//! Three-channel version of convert_srgb_to_linear()
vec3 convert_srgb_to_linear_rgb(vec3 srgb) {
	return vec3(
		convert_srgb_to_linear(srgb.r),
		convert_srgb_to_linear(srgb.g),
		convert_srgb_to_linear(srgb.b));
}
