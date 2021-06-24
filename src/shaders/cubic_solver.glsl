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


/*! Computes all real roots of the cubic polynomial
	coeffs[0] + coeffs[1] * x  + coeffs[2] * x * x  + coeffs[1] * x * x * x.
	If there is only one root, it is written to out_roots.x. Roots with
	multiplicity are written multiple times.
	\return True if the polynomial has three real roots (counting
		multiplicity), false if it has only one.
	\note This implementation cuts some corners but it is based on the
		following paper:
		Blinn, James F.,2007, How to solve a cubic equation, part 5: Back to
		numerics, IEEE Computer Graphics and Applications, 27(3):78–89.
		http://doi.org/10.1109/MCG.2007.60
		*/
bool solve_cubic(out vec3 out_roots, vec4 coeffs) {
	// Normalize the polynomial
	coeffs.xyz /= coeffs[3];
	// Divide middle coefficients by three
	coeffs.yz /= 3.0f;
	// Compute the Hessian and the discrimant
	vec3 delta = vec3(
		fma(-coeffs[2], coeffs[2], coeffs[1]),
		fma(-coeffs[1], coeffs[2], coeffs[0]),
		coeffs[2] * coeffs[0] - coeffs[1] * coeffs[1]);
	float discriminant = 4.0f * delta[0] * delta[2] - delta[1] * delta[1];
	float sqrt_abs_discriminant = sqrt(abs(discriminant));
	// Compute coefficients of the depressed cubic (third is zero, fourth is
	// one)
	vec2 depressed = vec2(fma(-2.0f * coeffs[2], delta[0], delta[1]), delta[0]);
	// Three real roots
	if (discriminant >= 0.0f) {
		// Take the cubic root of a normalized complex number
		float theta = atan(sqrt_abs_discriminant, -depressed[0]) * (1.0f / 3.0f);
		vec2 cubic_root = vec2(cos(theta), sin(theta));
		// Compute the three roots, scale appropriately and undepress
		out_roots.xyz = vec3(
			cubic_root[0],
			fma(-sqrt(0.75f), cubic_root[1], -0.5f * cubic_root[0]),
			fma(+sqrt(0.75f), cubic_root[1], -0.5f * cubic_root[0]));
		out_roots.xyz = fma(vec3(2.0f * sqrt(-depressed[1])), out_roots, -vec3(coeffs[2]));
		return true;
	}
	// One real root and a pair of complex conjugate roots (which we do not
	// compute)
	else {
		// The depressed cubic gives rise to a quadratic, which we solve
		float signed_sqrt_discriminant = (depressed[0] < 0.0f) ? sqrt_abs_discriminant : -sqrt_abs_discriminant;
		float quadratic_root = 0.5f * (signed_sqrt_discriminant - depressed[0]);
		// Now take the cube root of both of them. pow() is undefined for
		// negative inputs, so we have to carry over the sign manually.
		vec2 cube_roots;
		cube_roots[0] = pow(abs(quadratic_root), 1.0f / 3.0f);
		cube_roots[0] = (quadratic_root < 0.0f) ? -cube_roots[0] : cube_roots[0];
		// The other root of the quadratic can be attained with a division
		cube_roots[1] = -depressed[1] / cube_roots[0];
		// Combine to obtain one root of the depressed cubic
		float cubic_root = cube_roots[0] + cube_roots[1];
		// Finally, undepress
		out_roots[0] = cubic_root - coeffs[2];
		return false;
	}
}
