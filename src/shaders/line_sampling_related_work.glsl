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


#include "line_sampling.glsl"
#include "cubic_solver.glsl"


/*! Holds intermediate values that only need to be computed once per line
	and shading point for clipped solid angle sampling with a warp in primary
	sample space. The warp is designed to produce a linear approximation of
	the cosine term as density, as proposed by Hart et al..*/
struct linear_cosine_warp_line_hart_t {
	//! Intermediate values to perform solid angle sampling for the line
	solid_angle_line_t line;
	//! Target density for the beginning of the line
	float density_0;
	//! Target density for the end of the line
	float density_1;
};


/*! Prepares sampling with a lienar warp in primary sample space as proposed by
	Hart et al.. The normal must be normalized. Other parameters forward to
	prepare_solid_angle_line_sampling(). The given line is clipped internally.
	\see linear_cosine_warp_line_hart_t */
linear_cosine_warp_line_hart_t prepare_linear_cosine_warp_line_sampling_hart(vec3 surface_normal, vec3 line_begin, vec3 line_direction, float line_length) {
	linear_cosine_warp_line_hart_t line;
	// Clip the line
	vec3 line_end;
	clip_line(vec3(0.0f), surface_normal, line_begin, line_end, line_direction, line_length);
	if (line_length <= 0.0f) {
		line.line.rcp_density = 0.0f;
		return line;
	}
	// Prepare solid angle sampling
	line.line = prepare_solid_angle_line_sampling(line_begin, line_direction, line_length);
	// Evaluate the cosine term at the endpoints of the line
	line.density_0 = max(0.0f, dot(line_begin, surface_normal) * inversesqrt(dot(line_begin, line_begin)));
	line.density_1 = max(0.0f, dot(line_end, surface_normal) * inversesqrt(dot(line_end, line_end)));
	// Normalize the densities and incorporate the solid angle
	float density_sum = line.density_0 + line.density_1;
	float normalization = 2.0f / (line.line.rcp_density * density_sum);
	line.density_0 *= normalization;
	line.density_1 *= normalization;
	return line;
}


/*! An implementation of mix() using two fused-multiply add instructions. Used
	because the native mix() implementation had stability issues in a few
	spots. Credit to Fabian Giessen's blog, see:
	https://fgiesen.wordpress.com/2012/08/15/linear-interpolation-past-present-and-future/
	*/
float mix_fma(float x, float y, float a) {
	return fma(a, y, fma(-a, x, x));
}


//! Turns the given uniformly distributed random number, into one distributed
//! according to a piecewise linear density that is density_0 at 0.0f,
//! density_1 at 1.0f and zero outside the unit interval.
float linear_warp(float random_number, float density_0, float density_1) {
	float lerped_density_sq = mix_fma(density_0 * density_0, density_1 * density_1, random_number);
	// The following formula was proposed by Hart et al. but it fails if
	// density_0 == density_1. Since we use clipping at the horizon, that
	// happens frequently.
	//return (density_0 - sqrt(lerped_density_sq)) / (density_0 - density_1);
	// We instead use an equivalent formulation based on Muller's method, which
	// avoids division by zero and generally relies more on addition than
	// subtraction, thus improving stability. However, it requires one
	// additional multiplication instruction and if both densities are zero or
	// the random number and density_0 are zero, this formulation also fails.
	float divisor = density_0 + sqrt(lerped_density_sq);
	return random_number * (density_0 + density_1) / divisor;
}


/*! Takes a sample using clipped solid angle sampling and a linear warp of
	primary sample space.
	\param out_density Output for the density with respect to the solid angle
		measure times radius.
	\param line Output of prepare_linear_cosine_warp_line_sampling_hart().
	\param random_number A uniform random number in [0,1).
	\return A unit direction vector towards the line.
	\see linear_cosine_warp_line_hart_t */
vec3 sample_linear_cosine_warp_line_hart(out float out_density, linear_cosine_warp_line_hart_t line, float random_number) {
	// Warp the random number
	random_number = linear_warp(random_number, line.density_0, line.density_1);
	// Compute the density
	out_density = mix_fma(line.density_0, line.density_1, random_number);
	// Take a sample
	return sample_solid_angle_line(line.line, random_number);
}


//! Like linear_cosine_warp_line_hart_t but for the quadratic density
//! approximation
struct quadratic_cosine_warp_line_hart_t {
	//! Intermediate values to perform solid angle sampling for the line
	solid_angle_line_t line;
	//! Target density for the beginning of the line
	float density_0;
	//! Target density for the middle of the line (split in half by solid
	//! angle)
	float density_1;
	//! Target density for the end of the line
	float density_2;
};


//! Like prepare_linear_cosine_warp_line_sampling_hart() but with a
//! quadratic density approximation
quadratic_cosine_warp_line_hart_t prepare_quadratic_cosine_warp_line_sampling_hart(vec3 surface_normal, vec3 line_begin, vec3 line_direction, float line_length) {
	quadratic_cosine_warp_line_hart_t line;
	// Clip the line
	vec3 line_end;
	clip_line(vec3(0.0f), surface_normal, line_begin, line_end, line_direction, line_length);
	if (line_length <= 0.0f) {
		line.line.rcp_density = 0.0f;
		return line;
	}
	// Prepare solid angle sampling
	line.line = prepare_solid_angle_line_sampling(line_begin, line_direction, line_length);
	// Evaluate the cosine term at the endpoints of the line
	line.density_0 = max(0.0f, dot(line_begin, surface_normal) * inversesqrt(dot(line_begin, line_begin)));
	line.density_2 = max(0.0f, dot(line_end, surface_normal) * inversesqrt(dot(line_end, line_end)));
	// Sample the middle of the line and evaluate the cosine term
	vec3 middle = sample_solid_angle_line(line.line, 0.5f);
	line.density_1 = max(0.0f, dot(middle, surface_normal));
	// Normalize the densities and incorporate the solid angle
	float density_sum = line.density_0 + line.density_1 + line.density_2;
	float normalization = 3.0f / (line.line.rcp_density * density_sum);
	line.density_0 *= normalization;
	line.density_1 *= normalization;
	line.density_2 *= normalization;
	return line;
}


//! Turns the given uniformly distributed random number, into one distributed
//! according to a quadratic Bezier density. It interpolates density_0 at 0.0f,
//! density_2 at 1.0f and approximates density_1 at 0.5f. Outside the unit
//! interval, it is zero.
float quadratic_warp(float random_number, float density_0, float density_1, float density_2) {
	// Construct the quadratic Bezier spline
	vec3 quadratic = vec3(density_0, 2.0f * (density_1 - density_0), density_0 - 2.0f * density_1 + density_2);
	// Compute its indefinite integral
	vec4 cubic = vec4(0.0f, quadratic[0], 0.5f * quadratic[1], (1.0f / 3.0f) * quadratic[2]);
	// Scale the random number such that one maps to the maximal integral
	random_number *= dot(cubic.yzw, vec3(1.0f));
	// Look for an intersection between the cubic and the scaled random number
	cubic[0] = -random_number;
	vec3 roots;
	if (solve_cubic(roots, cubic)) {
		// Select the unique root between zero and one
		float result = roots[0];
		result = (roots[1] >= 0.0f && roots[1] <= 1.0f) ? roots[1] : result;
		result = (roots[2] >= 0.0f && roots[2] <= 1.0f) ? roots[2] : result;
		return result;
	}
	else {
		// There is only one real root
		return roots[0];
	}
}


//! Evaluates the quadratic Bezier spline with control points b_0_0, b_0_1 and
//! b_0_2 (for locations 0.0f, 0.5f, 1.0f, respectively) at the given location
//! using de Casteljau's algorithm.
float quadratic_bezier(float b_0_0, float b_0_1, float b_0_2, float location) {
	float b_1_0 = mix_fma(b_0_0, b_0_1, location);
	float b_1_1 = mix_fma(b_0_1, b_0_2, location);
	return mix_fma(b_1_0, b_1_1, location);
}


//! Like sample_linear_cosine_warp_line_hart() but with a quadratic
//! density approximation.
vec3 sample_quadratic_cosine_warp_line_hart(out float out_density, quadratic_cosine_warp_line_hart_t line, float random_number) {
	// Warp the random number
	random_number = quadratic_warp(random_number, line.density_0, line.density_1, line.density_2);
	// Compute the density
	out_density = quadratic_bezier(line.density_0, line.density_1, line.density_2, random_number);
	// Take a sample
	return sample_solid_angle_line(line.line, random_number);
}


/*! Like projected_solid_angle_line_li_t but uses the method of Li et al.:
	Tzu-Mao Li, Miika  Aittala, Fredo Durand and Jaakko Lehtinen,
	Differentiable Monte Carlo ray tracing through edge sampling,
	ACM Transactions on Graphics 37:6, https://doi.org/10.1145/3272127.3275109
	*/
struct projected_solid_angle_line_li_t{
	//! Beginning of the line in a coordinate frame where the shading point is
	//! the origin
	vec3 begin;
	//! Normalized vector from the beginning to the end of the line
	vec3 line_direction;
	//! The closest point to the shading point on the infinitely extended line
	vec3 closest_point;
	//! x: dot(surface_normal, line_direction),
	//! y: dot(surface_normal, closest_point), which differs from our technique
	vec2 normal;
	//! length(closest_point)
	float min_distance;
	//! dot(line_direction, begin)
	float begin_l;
	//! dot(line_direction, end) where end points from the shading point to the
	//! end of the line
	float end_l;
	//! The value of get_line_sampling_unnormalized_cdf_li() for begin_l
	float cdf_begin;
	//! The projected solid angle per radius. Equivalently, the difference of
	//! get_line_sampling_unnormalized_cdf_li() for end_l and begin_l.
	float rcp_density;
};


//! Version of get_line_sampling_cdf_li() without proper normalization
float get_line_sampling_unnormalized_cdf_li(projected_solid_angle_line_li_t line, float l) {
	float angle = atan(l / line.min_distance);
	float d = line.min_distance;
	float d2 = d * d;
	float s = l / (d * fma(l, l, d2));
	return (s + angle / d2) * line.normal.t + l * s * line.normal.s;
}


//! Evaluates the sampling CDF for the method of Li et al. as proposed by Heitz
//! and Hill. The input is a dot product between a point on the line and the
//! line direction vector.
float get_line_sampling_cdf_li(projected_solid_angle_line_li_t line, float l) {
	return (get_line_sampling_unnormalized_cdf_li(line, l) - line.cdf_begin) / line.rcp_density;
}


//! Returns the derivative of get_line_sampling_cdf_li(), i.e. a probability
//! density function
float get_line_sampling_pdf_li(projected_solid_angle_line_li_t line, float l) {
	float dist_sq = l * l + line.min_distance * line.min_distance;
	return 2.0f * line.min_distance * (line.normal.t + l * line.normal.s) / (line.rcp_density * dist_sq * dist_sq);
}


//! Like prepare_projected_solid_angle_line_sampling_without_clipping() but
//! uses the method of Li et al.
projected_solid_angle_line_li_t prepare_projected_solid_angle_line_sampling_li_without_clipping(vec3 surface_normal, vec3 line_begin, vec3 line_direction, float line_length) {
	projected_solid_angle_line_li_t line;
	// Copy inputs
	line.begin = line_begin;
	line.line_direction = line_direction;
	// Represent beginning and end in local coordinates
	line.begin_l = dot(line.line_direction, line.begin);
	line.end_l = line.begin_l + line_length;
	// Determine the closest point on the line and the reciprocal distance
	line.closest_point = fma(vec3(-line.begin_l), line.line_direction, line.begin);
	line.min_distance = length(line.closest_point);
	// Represent the normal vector in local coordinates
	line.normal.s = dot(surface_normal, line.line_direction);
	line.normal.t = dot(surface_normal, line.closest_point);
	// Compute the range of the unnormalized CDF
	line.cdf_begin = get_line_sampling_unnormalized_cdf_li(line, line.begin_l);
	float cdf_end = get_line_sampling_unnormalized_cdf_li(line, line.end_l);
	line.rcp_density = cdf_end - line.cdf_begin;
	return line;
}


//! Like sample_projected_solid_angle_line() but uses the method of Li et al.
vec3 sample_projected_solid_angle_line_li(projected_solid_angle_line_li_t line, float random_number) {
	// Apply up to 20 iterations of Newton's method with fallback to bisection
	float left = line.begin_l;
	float right = line.end_l;
	float current = 0.5f * (left + right);
	for (uint i = 0; i != 20; ++i) {
		bool no_bisection = (current >= left && current <= right);
		current = no_bisection ? current : (0.5f * (left + right));
		float error = get_line_sampling_cdf_li(line, current) - random_number;
		if (abs(error) < 1.0e-5f || i == 19) break;
		left = (error > 0.0f) ? left : current;
		right = (error > 0.0f) ? current : right;
		float derivative = get_line_sampling_pdf_li(line, current);
		current -= error / derivative;
	}
	return normalize(fma(vec3(current), line.line_direction, line.closest_point));
}
