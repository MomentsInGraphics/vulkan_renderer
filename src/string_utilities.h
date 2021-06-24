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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

//! If ARRAY is an object of array type (not a pointer!), this define evaluates
//! to the number of entries in the array. It uses a ratio of sizeof results.
#define COUNT_OF(ARRAY) (sizeof(ARRAY) / sizeof(ARRAY[0]))

/*! mallocs memory for a concatenation of the null-terminated strings in the
	given list and concatenates all of them. Returns an empty string, if no
	strings are given. The calling side has to free().*/
static inline char* concatenate_strings(uint32_t string_count, const char* const* strings) {
	size_t output_size = 1;
	for (uint32_t i = 0; i != string_count; ++i) {
		output_size += strlen(strings[i]);
	}
	char* result = (char*) malloc(output_size);
	size_t output_length = 0;
	for (uint32_t i = 0; i != string_count; ++i) {
		size_t length = strlen(strings[i]);
		memcpy(result + output_length, strings[i], sizeof(char) * length);
		output_length += length;
	}
	result[output_length] = 0;
	return result;
}


//! Returns a copy of the given null-terminated string that must be freed by
//! the calling side
static inline char* copy_string(const char* string) {
	size_t size = strlen(string) + 1;
	char* result = (char*) malloc(size);
	memcpy(result, string, size);
	return result;
}


//! Returns a string constructed from format_string using the given integer as
//! argument. There should be a single %u in format_string. The calling side
//! has to free the returned pointer.
static inline char* format_uint(const char* format_string, uint32_t integer) {
	int size = snprintf(NULL, 0, format_string, integer) + 1;
	char* result = (char*) malloc(size);
	sprintf(result, format_string, integer);
	return result;
}

//! Like format_uint() but with two %u in format_string
static inline char* format_uint2(const char* format_string, uint32_t integer_0, uint32_t integer_1) {
	int size = snprintf(NULL, 0, format_string, integer_0, integer_1) + 1;
	char* result = (char*) malloc(size);
	sprintf(result, format_string, integer_0, integer_1);
	return result;
}

//! Like format_uint() but with a single %f in format_string
static inline char* format_float(const char* format_string, float scalar) {
	int size = snprintf(NULL, 0, format_string, scalar) + 1;
	char* result = (char*) malloc(size);
	sprintf(result, format_string, scalar);
	return result;
}
