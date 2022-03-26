//  Copyright (C) 2022, Christoph Peters, Karlsruhe Institute of Technology
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
#include <stdint.h>


//! How many bits can be saved per weight in the power-of-two AABBB method,
//! starting with the second largest one. Denominator:  2  3  4  5  6  7  8  9 10 11 12 13
static const uint32_t power_of_two_weight_savings[] = { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2 };


//! Quantizes a weight to an integer. Values that can be represented exactly
//! are multiples of 1.0f / ((1 << bit_count) - 1) in [0, 1]. It rounds to
//! nearest. bit_count should not exceed 24 (that is the float precision).
static inline uint32_t quantize_weight(float weight, uint32_t bit_count) {
	uint32_t max_value = (1 << bit_count) - 1;
	return (uint32_t) (weight * max_value + 0.5f);
}


//! Like quantize_weight() but for a value in [0, 0.5]. Values that can be
//! represented exactly are multiples of 0.5f / ((1 << bit_count) - 1). That is
//! consistent with power-of-two AABB. bit_count should not exceed 24 (that is
//! the float precision).
static inline uint32_t quantize_half_weight(float weight, uint32_t bit_count) {
	uint32_t max_value = 2 * ((1 << bit_count) - 1);
	return (uint32_t) (weight * max_value + 0.5f);
}


/*! This function inserts up to 32 bits into a binary data at an arbitrary
	position.
	\param output The location of the first bit that may be modified.
	\param insert The data to insert (up to 32 bits).
	\param offset The first bit in output to modifiy.
	\param bit_count The number of bits to insert. At most 32.*/
static inline void long_bitfield_insert(void* output, uint32_t insert, uint32_t offset, uint32_t bit_count) {
	uint32_t insert_mask = (1 << bit_count) - 1;
	insert &= insert_mask;
	uint32_t* dwords = (uint32_t*) output;
	uint32_t dest_dword = offset >> 5;
	uint32_t dest_offset = offset & 0x1f;
	// Set the bits that are going to be overwritten to zero
	dwords[dest_dword] &= ~(insert_mask << dest_offset);
	// Set them to whatever they are supposed to be
	dwords[dest_dword] |= insert << dest_offset;
	// We may need to touch a second dword
	uint32_t dest_end = dest_offset + bit_count;
	if (dest_end > 32) {
		++dest_dword;
		dest_end -= 32;
		uint32_t first_dword_bit_count = 32 - dest_offset;
		insert >>= first_dword_bit_count;
		// Set the bits that are going to be overwritten to zero
		dwords[dest_dword] &= (1 << dest_end) - 1;
		// Set them to whatever they are supposed to be
		dwords[dest_dword] |= insert;
	}
}
