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


#include <stdint.h>

//! A union for convenient access to bits of a float
typedef union fp32_u {
	uint32_t u;
	float f;
} fp32_t;


//! Used to be float_to_half_fast3. Credit to Fabian Giessen:
//! https://gist.githubusercontent.com/rygorous/2156668/raw/ef8408efac2ff0db549252883dd4c99dddfcc929/gistfile1.cpp
static inline uint16_t float_to_half(float input_float) {
    fp32_t f = {.f = input_float};
	fp32_t f32_infty = { 255 << 23 };
	fp32_t f16_infty = { 31 << 23 };
	fp32_t magic = { 15 << 23 };
	uint32_t sign_mask = 0x80000000u;
	uint32_t round_mask = ~0xfffu; 
	uint16_t o;

	uint32_t sign = f.u & sign_mask;
	f.u ^= sign;

	// NOTE all the integer compares in this function can be safely
	// compiled into signed compares since all operands are below
	// 0x80000000. Important if you want fast straight SSE2 code
	// (since there's no unsigned PCMPGTD).

	if (f.u >= f32_infty.u) // Inf or NaN (all exponent bits set)
		o = (f.u > f32_infty.u) ? 0x7e00 : 0x7c00; // NaN->qNaN and Inf->Inf
	else { // (De)normalized number or zero
		f.u &= round_mask;
		f.f *= magic.f;
		f.u -= round_mask;
        // Clamp to signed infinity if overflowed
		if (f.u > f16_infty.u) f.u = f16_infty.u;
        // Take the bits!
		o = f.u >> 13;
	}

	o |= sign >> 16;
	return o;
}
