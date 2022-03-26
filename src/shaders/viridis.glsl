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


//! Maps a value in the range from 0 to 1 to an sRGB color using the Viridis
//! color map as found in matplotlib
vec3 viridis(float x) {
	// Coefficients for a polynomial fit of degree 15
	const vec3 c[14] = {
		vec3(2.682392307e-01f, 3.896714036e-03f, 3.301680888e-01f),
		vec3(2.170843715e-02f, 1.368181901e+00f, 1.362350119e+00f),
		vec3(1.693640394e+01f, 7.044558960e+00f, 7.284166532e+00f),
		vec3(-4.132995594e+02f, -1.677316596e+02f, -2.045202573e+02f),
		vec3(4.682615804e+03f, 1.741809221e+03f, 2.322828494e+03f),
		vec3(-3.181313681e+04f, -1.065526507e+04f, -1.641440682e+04f),
		vec3(1.387452704e+05f, 4.206788283e+04f, 7.558448890e+04f),
		vec3(-4.022748555e+05f, -1.124041895e+05f, -2.333804435e+05f),
		vec3(7.896875015e+05f, 2.078838097e+05f, 4.920112867e+05f),
		vec3(-1.052555436e+06f, -2.668201884e+05f, -7.103843700e+05f),
		vec3(9.377588281e+05f, 2.334694599e+05f, 6.910842348e+05f),
		vec3(-5.344585973e+05f, -1.329108923e+05f, -4.329605329e+05f),
		vec3(1.761720975e+05f, 4.438576268e+04f, 1.576947004e+05f),
		vec3(-2.554722032e+04f, -6.597968113e+03f, -2.536209733e+04f),
	};
	x = clamp(x, 0.0f, 1.0f);
	// Evaluation using Horner's method
	return (x * (x * (x * (x * (x * (x * (x * (x * (x * (x * (x * (x * (x * c[13] + c[12]) + c[11]) + c[10]) + c[9]) + c[8]) + c[7]) + c[6]) + c[5]) + c[4]) + c[3]) + c[2]) + c[1]) + c[0]);
}
