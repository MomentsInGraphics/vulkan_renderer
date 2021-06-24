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


#include "polygon_sampling.glsl"
#include "cubic_solver.glsl"


/*! Samples the area of the given polygon uniformly.
	\param vertex_count The number of vertices in the given polygon.
	\param vertices The vertex locations. They can be given in any coordinate
		system as long as the areas match. The output will use the same.
	\param fan_areas At index i, this array holds the area of the triangle
		formed by vertices 0, i + 1 and i + 2 in x and the area of the triangle
		fan formed by vertices 0 to i + 2 in y. The last y-entry is always the
		total area.
	\param random_numbers Independent, uniform random numbers in [0,1) or an
		adequate low-discrepancy replacement.
	\return A position on the given polygon (not a direction towards it).
	\note This implementation subdivides the polygon using a triangle fan
		around vertex 0 and samples each triangle as described in:
		Greg Turk, 1990, Generating random points in triangles, Graphics Gems,
		pages 24-28, Academic Press
		The newer method of Heitz preserves stratification within a triangle
		better but introduces discontinuities between neighboring triangles.*/
vec3 sample_area_polygon_turk(uint vertex_count, vec3 vertices[MAX_POLYGON_VERTEX_COUNT], vec2 fan_areas[MAX_POLYGON_VERTEX_COUNT - 2], vec2 random_numbers) {
	// Decide which triangle needs to be sampled
	float target_area = fan_areas[MAX_POLYGON_VERTEX_COUNT - 3].y * random_numbers[0];
	float subtriangle_area = target_area;
	float triangle_area = fan_areas[0].x;
	vec3 triangle_vertices[3] = {
		vertices[1], vertices[0], vertices[2]
	};
	[[unroll]]
	for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 3; ++i) {
		if (i + 3 >= vertex_count || fan_areas[i].y >= target_area) break;
		subtriangle_area = target_area - fan_areas[i].y;
		triangle_area = fan_areas[i + 1].x;
		triangle_vertices[0] = vertices[i + 2];
		triangle_vertices[2] = vertices[i + 3];
	}
	// Renormalize the first random number
	random_numbers[0] = subtriangle_area / triangle_area;
	// Compute barycentric coordinates for the sample
	float sqrt_random_0 = sqrt(random_numbers[0]);
	vec3 barycentric = vec3(
		1.0f - sqrt_random_0,
		sqrt_random_0 * random_numbers[1],
		fma(-sqrt_random_0, random_numbers[1], sqrt_random_0)
	);
	return barycentric[0] * triangle_vertices[0] + barycentric[1] * triangle_vertices[1] + barycentric[2] * triangle_vertices[2];
}


/*! Given the result of uniform area sampling of an area light, this function
	computes the density with respect to the solid angle measure for a given
	shading point.
	\param out_normalized_dir The normalized direction from the shading
		position to the light sample is written to this vector.
	\param light_sample A uniform sample on the area of the light source.
	\param shading_position The location of the shading point.
	\param light_normal The unit normal vector of the light source at
		light_sample. If it is oriented wrongly, it is flipped implicitly.
	\param light_area The total area of the light source.
	\return The density with respect to solid angle measure.*/
float get_area_sample_density(out vec3 out_normalized_dir, vec3 light_sample, vec3 shading_position, vec3 light_normal, float light_area) {
	out_normalized_dir = light_sample - shading_position;
	float distance_squared = dot(out_normalized_dir, out_normalized_dir);
	float normalization = inversesqrt(distance_squared);
	out_normalized_dir *= normalization;
	float projected_area = abs(dot(light_normal, out_normalized_dir)) * light_area;
	return distance_squared / projected_area;
}


/*! Holds intermediate values for sampling the solid angle of rectangles
	uniformly. This code as well as the code for the methods
	prepare_solid_angle_rectangle_sampling_urena() and
	sample_solid_angle_rectangle_urena() is taken from Urena's supplementary
	material with minor optimizations. Identifiers also follow this paper:
	Carlos Urena, Marcos Fajardo, Alan King 2013, An Area-Preserving
	Parametrization for Spherical Rectangles, Computer Graphics Forum 32:4,
	https://doi.org/10.1111/cgf.12151
*/
struct solid_angle_rectangle_urena_t {
	// local reference system R
	vec3 o, x, y, z;
	float z0, z0sq;
	// rectangle coords in R
	float x0, y0, y0sq;
	float x1, y1, y1sq;
	// misc precomputed constants
	float b0, b1, b0sq, k;
	// solid angle of Q
	float solid_angle;
};


/*! Prepares sampling of a rectangle proportional to solid angle.
	\param s A corner of the rectangle.
	\param ex Offset from the corner s to an adjacent vertex.
	\param ey Offset from the corner s to the other adjacent vertex.
	\param exl Length of ex.
	\param eyl Length of ey.
	\param local_to_world_space A matrix with orthonormal columns. Column 0 is
		linearly dependent with ex and column 1 linearly dependent with ey. The
		columns point in the same direction as these vectors.
	\param o Location of the shading point.*/
solid_angle_rectangle_urena_t prepare_solid_angle_rectangle_sampling_urena(vec3 s, vec3 ex, vec3 ey, float exl, float eyl, mat3 local_to_world_space, vec3 o) {
	solid_angle_rectangle_urena_t squad;
	squad.o = o;
	// store the given coordinate frame
	squad.x = local_to_world_space[0];
	squad.y = local_to_world_space[1];
	squad.z = local_to_world_space[2];
	// compute rectangle coords in local reference system
	vec3 d = s - o;
	squad.z0 = dot(d, squad.z);
	// flip z to make it point against Q
	squad.z = (squad.z0 > 0) ? -squad.z : squad.z;
	squad.z0 = -abs(squad.z0);
	squad.z0sq = squad.z0 * squad.z0;
	squad.x0 = dot(d, squad.x);
	squad.y0 = dot(d, squad.y);
	squad.x1 = squad.x0 + exl;
	squad.y1 = squad.y0 + eyl;
	squad.y0sq = squad.y0 * squad.y0;
	squad.y1sq = squad.y1 * squad.y1;
	// create vectors to four vertices
	vec3 v00 = vec3(squad.x0, squad.y0, squad.z0);
	vec3 v01 = vec3(squad.x0, squad.y1, squad.z0);
	vec3 v10 = vec3(squad.x1, squad.y0, squad.z0);
	vec3 v11 = vec3(squad.x1, squad.y1, squad.z0);
	// compute normals to edges
	vec3 n0 = normalize(cross(v00, v10));
	vec3 n1 = normalize(cross(v10, v11));
	vec3 n2 = normalize(cross(v11, v01));
	vec3 n3 = normalize(cross(v01, v00));
	// compute internal angles (gamma_i)
	float g0 = acos(-dot(n0,n1));
	float g1 = acos(-dot(n1,n2));
	float g2 = acos(-dot(n2,n3));
	float g3 = acos(-dot(n3,n0));
	// compute predefined constants
	squad.b0 = n0.z;
	squad.b1 = n2.z;
	squad.b0sq = squad.b0 * squad.b0;
	squad.k = 2.0f * M_PI - g2 - g3;
	// compute solid angle from internal angles
	squad.solid_angle = g0 + g1 - squad.k;
	return squad;
}


/*! Returns a direction sampled from the solid angle of the given rectangle. If
	the given random numbers are uniform on [0,1)^2, the returned sample is
	uniform within the solid angle. The output uses the same coordinate frame
	as the position s passed to the prepare method.*/
vec3 sample_solid_angle_rectangle_urena(solid_angle_rectangle_urena_t squad, vec2 random_numbers) {
	float u = random_numbers[0];
	float v = random_numbers[1];
	// 1. compute cu
	float au = fma(u, squad.solid_angle, squad.k);
	float fu = fma(cos(au), squad.b0, -squad.b1) / sin(au);
	float cu = inversesqrt(fma(fu, fu, squad.b0sq));
	cu = (fu > 0.0f) ? cu : -cu;
	cu = clamp(cu, -1.0f, 1.0f);
	// 2. compute xu
	float xu = -(cu * squad.z0) * inversesqrt(fma(-cu, cu, 1.0f));
	xu = clamp(xu, squad.x0, squad.x1);
	// 3. compute yv
	float d = sqrt(xu*xu + squad.z0sq);
	float h0 = squad.y0 * inversesqrt(fma(d, d, squad.y0sq));
	float h1 = squad.y1 * inversesqrt(fma(d, d, squad.y1sq));
	float hv = h0 + v * (h1 - h0);
	float mhv2_1 = fma(-hv, hv, 1.0f);
	float yv = (mhv2_1 >= 0.0f) ? ((hv * d) * inversesqrt(mhv2_1)) : squad.y1;
	// 4. transform (xu,yv,z0) to world coords
	return normalize(xu * squad.x + yv * squad.y + squad.z0 * squad.z);
}


/*! Holds intermediate values for solid angle sampling, just like
	solid_angle_polygon_t but for Arvo's method:
	James Arvo 1995, Stratified Sampling of Spherical Triangles, SIGGRAPH 1995
	https://doi.org/10.1145/218380.218500
*/
struct solid_angle_polygon_arvo_t {
	//! The number of vertices that form the polygon
	uint vertex_count;
	//! Normalized direction vectors from the shading point to each vertex
	vec3 vertex_dirs[MAX_POLYGON_VERTEX_COUNT];
	//! At index i, this array holds the solid angle of the triangle fan formed
	//! by vertices 0 to i + 2.
	float fan_solid_angles[MAX_POLYGON_VERTEX_COUNT - 2];
	//! At index i, this array holds the cosine and sine of the angle between
	//! the edges connecting vertex 0 and i + 1 and vertex i + 1 and i + 2.
	vec2 opposite_dirs[MAX_POLYGON_VERTEX_COUNT - 2];
	//! The total solid angle of the polygon (equals the last meaningful entry
	//! of fan_solid_angles)
	float solid_angle;
};


//! Like prepare_solid_angle_polygon_sampling() but uses Arvo's method, which
//! is less stable and slower than ours.
solid_angle_polygon_arvo_t prepare_solid_angle_polygon_sampling_arvo(uint vertex_count, vec3 vertices[MAX_POLYGON_VERTEX_COUNT], vec3 shading_position) {
	solid_angle_polygon_arvo_t polygon;
	// Normalize vertex directions
	[[unroll]]
	for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
		polygon.vertex_dirs[i] = normalize(vertices[i] - shading_position);
	}
	// Prepare sampling and compute solid angles for each triangle in the
	// triangle fan
	float solid_angle = 0.0f;
	[[unroll]]
	for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 2; ++i) {
		if (i >= 1 && i + 2 >= vertex_count) break;
		// Compute sine and cosine of the angle between two edges
		vec3 edge_normals[2] = {
			normalize(cross(polygon.vertex_dirs[i + 1] - polygon.vertex_dirs[0], polygon.vertex_dirs[0])),
			normalize(cross(polygon.vertex_dirs[i + 2] - polygon.vertex_dirs[i + 1], polygon.vertex_dirs[i + 1])),
		};
		polygon.opposite_dirs[i].x = -dot(edge_normals[0], edge_normals[1]);
		polygon.opposite_dirs[i].y = sqrt(max(0.0f, fma(-polygon.opposite_dirs[i].x, polygon.opposite_dirs[i].x, 1.0f)));
		// Arvo recommends to avoid inverse trigonometric functions without
		// going into detail. We need to know the solid angles for fan
		// selection and density computation, so we use the formula of Oosterom
		// and Strackee to compute it with a single atan.
		float dot_0_1 = dot(polygon.vertex_dirs[0], polygon.vertex_dirs[i + 1]);
		float dot_0_2 = dot(polygon.vertex_dirs[0], polygon.vertex_dirs[i + 2]);
		float dot_1_2 = dot(polygon.vertex_dirs[i + 1], polygon.vertex_dirs[i + 2]);
		float simplex_volume = determinant(mat3(polygon.vertex_dirs[0], polygon.vertex_dirs[i + 1], polygon.vertex_dirs[i + 2]));
		float tangent = abs(simplex_volume) / (1.0f + dot_0_1 + dot_0_2 + dot_1_2);
		solid_angle += 2.0f * positive_atan(tangent);
		polygon.fan_solid_angles[i] = solid_angle;
	}
	polygon.solid_angle = solid_angle;
	polygon.vertex_count = vertex_count;
	return polygon;
}


//! Like sample_solid_angle_polygon() but uses Arvo's method instead of ours,
//! which is less stable and slower
vec3 sample_solid_angle_polygon_arvo(solid_angle_polygon_arvo_t polygon, vec2 random_numbers) {
	// Decide which triangle needs to be sampled
	float target_solid_angle = polygon.solid_angle * random_numbers[0];
	float subtriangle_solid_angle = target_solid_angle;
	vec2 opposite_dir = polygon.opposite_dirs[0];
	vec3 triangle_vertices[3] = {
		polygon.vertex_dirs[1], polygon.vertex_dirs[0], polygon.vertex_dirs[2]
	};
	[[unroll]]
	for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 3; ++i) {
		if (i + 3 >= polygon.vertex_count || polygon.fan_solid_angles[i] >= target_solid_angle) break;
		subtriangle_solid_angle = target_solid_angle - polygon.fan_solid_angles[i];
		triangle_vertices[0] = polygon.vertex_dirs[i + 2];
		triangle_vertices[2] = polygon.vertex_dirs[i + 3];
		opposite_dir = polygon.opposite_dirs[i + 1];
	}
	// Sample the opening angle at vertex 0 (actually its cosine). The
	// computation of u and v is prone to underflow and appears to be the main
	// source of instabilities.
	vec2 subtriangle_solid_angle_dir = vec2(cos(subtriangle_solid_angle), sin(subtriangle_solid_angle));
	float p = subtriangle_solid_angle_dir.y * opposite_dir.x - subtriangle_solid_angle_dir.x * opposite_dir.y;
	float q = subtriangle_solid_angle_dir.y * opposite_dir.y + subtriangle_solid_angle_dir.x * opposite_dir.x;
	float u = q - opposite_dir.x;
	float v = p + opposite_dir.y * dot(triangle_vertices[0], triangle_vertices[1]);
	float s = ((v * q - u * p) * opposite_dir.x - v) / ((v * p + u * q) * opposite_dir.y);
	// Construct vertex 2 of the partial triangle (which has solid angle
	// subtriangle_solid_angle by construction)
	vec3 edge_tangent_2_0 = normalize(triangle_vertices[2] - dot(triangle_vertices[0], triangle_vertices[2]) * triangle_vertices[0]);
	vec3 vertex_2 = s * triangle_vertices[0] + sqrt(clamp(fma(-s, s, 1.0f), 0.0f, 1.0f)) * edge_tangent_2_0;
	// Sample the distance from vertex 0
	float z = 1.0f - random_numbers[1] * (1.0f - dot(vertex_2, triangle_vertices[1]));
	// Construct the sample location
	vec3 edge_tangent_2_1 = normalize(vertex_2 - dot(triangle_vertices[1], vertex_2) * triangle_vertices[1]);
	return z * triangle_vertices[1] + sqrt(clamp(fma(-z, z, 1.0f), 0.0f, 1.0f)) * edge_tangent_2_1;
}


/*! Holds intermediate values that only need to be computed once per polygon
	and shading point for clipped solid angle sampling with a warp in primary
	sample space. The warp is designed to produce a bilinear approximation of
	the cosine term as density, as proposed by Hart et al..*/
struct bilinear_cosine_warp_polygon_hart_t {
	//! Intermediate values to perform solid angle sampling for the polygon
	solid_angle_polygon_t polygon;
	//! (Identical) target densities for the corners where the second random
	//! number is zero
	float density_0;
	//! Target densities for the corners where the second random number is one
	vec2 density_1;
};


/*! Prepares sampling with a biliear warp in primary sample space as proposed
	by Hart et al.. The given polygon is supposed to be given in a coordinate
	frame where the normal is aligned with the z-axis and the shading point is
	in the origin.
	\see bilinear_cosine_warp_polygon_hart_t */
bilinear_cosine_warp_polygon_hart_t prepare_bilinear_cosine_warp_polygon_sampling_hart(uint vertex_count, vec3 vertices[MAX_POLYGON_VERTEX_COUNT]) {
	bilinear_cosine_warp_polygon_hart_t polygon;
	// Prepare solid angle sampling
	polygon.polygon = prepare_solid_angle_polygon_sampling(vertex_count, vertices, vec3(0.0f));
	// Evaluate the cosine term at the vertices that the corners of primary
	// sample space map to
	polygon.density_0 = max(0.0f, polygon.polygon.vertex_dirs[0].z);
	polygon.density_1[0] = max(0.0f, polygon.polygon.vertex_dirs[1].z);
	// Grabbing the last vertex, is a bit more complicated, because we have to
	// avoid register spilling
	polygon.density_1[1] = polygon.polygon.vertex_dirs[2].z;
	[[unroll]]
	for (uint i = 3; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
		polygon.density_1[1] = (i < vertex_count) ? polygon.polygon.vertex_dirs[i].z : polygon.density_1[1];
	}
	polygon.density_1[1] = max(0.0f, polygon.density_1[1]);
	// Normalize the densities and incorporate the solid angle
	float density_sum = 2.0f * polygon.density_0 + polygon.density_1[0] + polygon.density_1[1];
	float normalization = 4.0f / (polygon.polygon.solid_angle * density_sum);
	polygon.density_0 *= normalization;
	polygon.density_1 *= normalization;
	// If all the densities are zero (not impossible at all), use a uniform
	// density
	float inv_solid_angle = 1.0f / polygon.polygon.solid_angle;
	polygon.density_0 = (density_sum <= 0.0f) ? inv_solid_angle : polygon.density_0;
	polygon.density_1 = (density_sum <= 0.0f) ? vec2(inv_solid_angle) : polygon.density_1;
	return polygon;
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


/*! Takes a sample using clipped solid angle sampling and a bilinear warp of
	primary sample space.
	\param out_density Output for the density with respect to the solid angle
		measure.
	\param polygon Output of prepare_bilinear_cosine_warp_polygon_sampling_hart().
	\param random_numbers Two independent, uniform random numbers in [0,1).
	\return A unit direction vector towards the polygon.
	\see bilinear_cosine_warp_polygon_hart_t */
vec3 sample_bilinear_cosine_warp_polygon_hart(out float out_density, bilinear_cosine_warp_polygon_hart_t polygon, vec2 random_numbers) {
	// Warp the random numbers
	random_numbers[1] = linear_warp(random_numbers[1], 2.0f * polygon.density_0, dot(polygon.density_1, vec2(1.0f)));
	float density_0 = mix_fma(polygon.density_0, polygon.density_1[0], random_numbers[1]);
	float density_1 = mix_fma(polygon.density_0, polygon.density_1[1], random_numbers[1]);
	random_numbers[0] = linear_warp(random_numbers[0], density_0, density_1);
	// Compute the density
	out_density = mix_fma(density_0, density_1, random_numbers[0]);
	// Take a sample
	return sample_solid_angle_polygon(polygon.polygon, random_numbers);
}


//! Like bilinear_cosine_warp_polygon_hart_t but for the biquadratic density
//! approximation
struct biquadratic_cosine_warp_polygon_hart_t {
	//! Intermediate values to perform solid angle sampling for the polygon
	solid_angle_polygon_t polygon;
	//! (Identical) target densities for the corners where the second random
	//! number is zero
	float density_0;
	//! Target densities for the corners and the midpoint where the second
	//! random number is 0.5f
	vec3 density_1;
	//! Target densities for the corners and the midpoint where the second
	//! random number is 1.0f
	vec3 density_2;
};


//! Like prepare_bilinear_cosine_warp_polygon_sampling_hart() but with a
//! biquadratic density approximation
biquadratic_cosine_warp_polygon_hart_t prepare_biquadratic_cosine_warp_polygon_sampling_hart(uint vertex_count, vec3 vertices[MAX_POLYGON_VERTEX_COUNT]) {
	biquadratic_cosine_warp_polygon_hart_t polygon;
	// Prepare solid angle sampling
	polygon.polygon = prepare_solid_angle_polygon_sampling(vertex_count, vertices, vec3(0.0f));
	// Grab the last vertex without spilling registers
	vec3 last_vertex = polygon.polygon.vertex_dirs[2];
	[[unroll]]
	for (uint i = 3; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
		last_vertex = (i < vertex_count) ? polygon.polygon.vertex_dirs[i] : last_vertex;
	}
	// The cosine term for the four corners is simple, because solid angle
	// sampling just maps them to vertices
	vec3 vertex_0 = polygon.polygon.vertex_dirs[0];
	polygon.density_0 = max(0.0f, vertex_0.z);
	polygon.density_2[0] = max(0.0f, polygon.polygon.vertex_dirs[1].z);
	polygon.density_2[2] = max(0.0f, last_vertex.z);
	// Sample the midpoint with the second random number set to one
	vec3 sample_2_1 = sample_solid_angle_polygon(polygon.polygon, vec2(0.5f, 1.0f));
	polygon.density_2[1] = max(0.0f, sample_2_1.z);
	// To fill in the middle row, we reimplement the last bit of the sampling
	// procedure, which consumes random_numbers[1] (0.5f in this case)
	vec3 far_vertices[3] = { vertex_0, sample_2_1, last_vertex };
	[[unroll]]
	for (uint i = 0; i != 3; ++i) {
		float s2 = dot(vertex_0, far_vertices[i]);
		float s = fma(0.5f, s2, 0.5f);
		float t = sqrt(max(0.0f, fma(-s, s, 1.0f)));
		float t_axis_z = fma(-s2, vertex_0.z, far_vertices[i].z);
		float normalization_t_axis = inversesqrt(2.0f * fma(-s2, s2, 1.0f));
		float sample_1_i_z = s * vertex_0.z + (t * normalization_t_axis) * t_axis_z;
		polygon.density_1[i] = max(0.0f, sample_1_i_z);
	}
	// Normalize the densities and incorporate the solid angle
	float density_sum = 3.0f * polygon.density_0 + dot(polygon.density_1, vec3(1.0f)) + dot(polygon.density_2, vec3(1.0f));
	float normalization = 9.0f / (polygon.polygon.solid_angle * density_sum);
	polygon.density_0 *= normalization;
	polygon.density_1 *= normalization;
	polygon.density_2 *= normalization;
	// If all the densities are zero (not impossible at all), use a uniform
	// density
	float inv_solid_angle = 1.0f / polygon.polygon.solid_angle;
	polygon.density_0 = (density_sum <= 0.0f) ? inv_solid_angle : polygon.density_0;
	polygon.density_1 = (density_sum <= 0.0f) ? vec3(inv_solid_angle) : polygon.density_1;
	polygon.density_2 = (density_sum <= 0.0f) ? vec3(inv_solid_angle) : polygon.density_2;
	return polygon;
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


/*! Like sample_bilinear_cosine_warp_polygon_hart() but with a biquadratic
	density approximation.*/
vec3 sample_biquadratic_cosine_warp_polygon_hart(out float out_density, biquadratic_cosine_warp_polygon_hart_t polygon, vec2 random_numbers) {
	// Warp the random numbers
	random_numbers[1] = quadratic_warp(random_numbers[1], 3.0f * polygon.density_0, dot(polygon.density_1, vec3(1.0f)), dot(polygon.density_2, vec3(1.0f)));
	float density_0 = quadratic_bezier(polygon.density_0, polygon.density_1[0], polygon.density_2[0], random_numbers[1]);
	float density_1 = quadratic_bezier(polygon.density_0, polygon.density_1[1], polygon.density_2[1], random_numbers[1]);
	float density_2 = quadratic_bezier(polygon.density_0, polygon.density_1[2], polygon.density_2[2], random_numbers[1]);
	random_numbers[0] = quadratic_warp(random_numbers[0], density_0, density_1, density_2);
	// Compute the density
	out_density = quadratic_bezier(density_0, density_1, density_2, random_numbers[0]);
	// Take a sample
	return sample_solid_angle_polygon(polygon.polygon, random_numbers);
}


//! Holds information that Arvo's projected solid angle sampling technique
//! needs to store per edge of the polygon.
struct edge_arvo_t {
	//! The factor that is multiplied onto the atan() to obtain the CDF.
	//! Negative for inner edges, positive for outer edges. Denoted 2 eta_i in
	//! Arvo's article. Concidentally, this is a signed version of what
	//! 2.0f * get_ellipse_rsqrt_det() computes.
	float cdf_factor;
	//! The coefficients needed to evaluate the length of an edge of a
	//! subtriangle. Denoted c_1 and -c_2 in Arvo's article.
	vec2 length_coeffs;
	//! The coefficients needed to evaluate the elevation of an edge above the
	//! horizon for a given azimuthal direction. Denoted dot(B, N) and
	//! dot(D, N) in Arvo's article.
	vec2 elevations;
};


/*! Like projected_solid_angle_polygon_t but uses Arvo's method, which is
	substantially slower and less stable:
	Stratified Sampling of 2-Manifolds in State of the Art in Monte Carlo Ray
	Tracing for Realistic Image Synthesis, SIGGRAPH 2001 Course Notes,
	volume 29, August 2001
	http://citeseerx.ist.psu.edu/viewdoc/summary?doi=10.1.1.6.6683
	*/
struct projected_solid_angle_polygon_arvo_t {
	//! The number of vertices that form the polygon
	uint vertex_count;
	/*! An azimuthal coordinate for each vertex, i.e. the vertex is projected
		to the xy-plane and atan() is evaluated. The vertices are sorted
		counterclockwise.*/
	float vertex_azimuths[MAX_POLYGON_VERTEX_COUNT];
	/*! For each vertex in vertices, this array describes the next edge in
		counterclockwise direction. The last entry is meaningless, except in
		the central case. For vertex 0, it holds the outer edge.*/
	edge_arvo_t edges[MAX_POLYGON_VERTEX_COUNT];
	//! The inner edge adjacent to vertex 0. If the CDF factor is positive, the
	//! central case is present.
	edge_arvo_t inner_edge_0;
	/*! At index i, this array holds the projected solid angle of the polygon
		in the sector between (sorted) vertices i and (i + 1) % vertex_count
		In the central case, entry vertex_count - 1 is meaningful, otherwise
		not.*/
	float sector_projected_solid_angles[MAX_POLYGON_VERTEX_COUNT];
	//! The total projected solid angle of the polygon
	float projected_solid_angle;
};


//! Constructs an edge from the given two vertices. They must be in a
//! coordinate frame where the z-axis is aligned with the normal and
//! normalized.
edge_arvo_t prepare_edge_arvo(vec3 vertex_0, vec3 vertex_1) {
	edge_arvo_t edge;
	// Compute the unit outer edge normal (denoted Gamma_a by Arvo)
	vec3 normal_a = normalize(cross(vertex_0, vertex_1));
	// The CDF factor is simply the z-coordinate of the normal
	edge.cdf_factor = 0.5f * normal_a.z;
	// The CDF is defined in clockwise direction, so its azimuth angles have to
	// be relative to the vertex at the counterclockwise end of the edge
	vec3 ccw_vertex = (edge.cdf_factor > 0.0f) ? vertex_0 : vertex_1;
	// Compute the angle beta between an edge from the normal to ccw_vertex and
	// the given edge. normal_c is denoted Gamma_c by Arvo and implicitly has
	// zero as third entry. We only really need sine and cosine of the angle.
	vec2 normal_c = rotate_90(normalize(ccw_vertex.xy));
	float cos_beta = -dot(normal_a.xy, normal_c);
	float sin_beta_sq = fma(-cos_beta, cos_beta, 1.0f);
	float csc_beta = inversesqrt(max(0.0f, sin_beta_sq));
	// Compute the reciprocal sine (a.k.a. cosecant) of the length of the edge
	// connecting the normal to ccw_vertex
	float csc_c = inversesqrt(max(0.0f, fma(-ccw_vertex.z, ccw_vertex.z, 1.0f)));
	// Put it together to get the coefficients for length computation
	edge.length_coeffs[0] = sin_beta_sq;
	edge.length_coeffs[1] = dot(normal_a.xy, rotate_90(normal_c)) * cos_beta;
	edge.length_coeffs *= csc_beta * csc_c;
	// Store the elevations of ccw_vertex and an orthogonal point on the great
	// circle through the edge
	edge.elevations[0] = ccw_vertex.z;
	edge.elevations[1] = cross(ccw_vertex, normal_a).z;
	edge.elevations[1] = (edge.cdf_factor > 0.0f) ? -edge.elevations[1] : edge.elevations[1];
	return edge;
}


/*! Returns the projected solid angle of the spherical triangle formed by the
	normal vector and points on the great circle of the given edge with the
	given azimuthal coordinates.
	\param edge Output of prepare_edge_arvo().
	\param relative_azimuth_0, relative_azimuth_1 Azimuthal coordinates
		relative to the lesser of the two vertex azimuths for the given edge.
		They must be non-negative and relative_azimuth_1 must be at most M_PI
		radians greater than relative_azimuth_0.*/
float get_edge_projected_solid_angle_in_sector_arvo(edge_arvo_t edge, float relative_azimuth_0, float relative_azimuth_1) {
	vec2 dir_0 = vec2(cos(relative_azimuth_0), sin(relative_azimuth_0));
	vec2 point_0 = vec2(dot(edge.length_coeffs, dir_0), dir_0.y);
	vec2 dir_1 = vec2(cos(relative_azimuth_1), sin(relative_azimuth_1));
	vec2 point_1 = vec2(dot(edge.length_coeffs, dir_1), dir_1.y);
	// According to Arvo, we should compute the difference of arctangents for
	// the two given azimuths, but is more efficient to rotate the coordinate
	// frame such that point_0 becomes (0.0f, 1.0f) and to only compute the
	// arctangent for the rotated point_1
	vec2 rotated_point = vec2(
		point_0.x * point_1.x + point_0.y * point_1.y,
		point_0.x * point_1.y - point_0.y * point_1.x);
	float length = positive_atan(abs(rotated_point.y) / rotated_point.x);
	return edge.cdf_factor * length;
}


/*! Like get_edge_projected_solid_angle_in_sector_arvo() but additionally
	outputs the derivative with respect to relative_azimuth_1 in y.*/
vec2 get_edge_projected_solid_angle_in_sector_derivative_arvo(edge_arvo_t edge, float relative_azimuth_0, float relative_azimuth_1) {
	vec2 dir_0 = vec2(cos(relative_azimuth_0), sin(relative_azimuth_0));
	vec2 point_0 = vec2(dot(edge.length_coeffs, dir_0), dir_0.y);
	vec2 dir_1 = vec2(cos(relative_azimuth_1), sin(relative_azimuth_1));
	vec2 point_1 = vec2(dot(edge.length_coeffs, dir_1), dir_1.y);
	vec2 rotated_point = vec2(
		point_0.x * point_1.x + point_0.y * point_1.y,
		point_0.x * point_1.y - point_0.y * point_1.x);
	float quotient = abs(rotated_point.y) / rotated_point.x;
	float length = positive_atan(quotient);
	// The derivative of dir_1 is trivial and rotated_point depends on that
	// linearly
	vec2 dir_1_deriv = rotate_90(dir_1);
	vec2 point_1_deriv = vec2(dot(edge.length_coeffs, dir_1_deriv), dir_1_deriv.y);
	vec2 rotated_point_deriv = vec2(
		point_0.x * point_1_deriv.x + point_0.y * point_1_deriv.y,
		point_0.x * point_1_deriv.y - point_0.y * point_1_deriv.x);
	// Quotient rule
	float quotient_derivative = (rotated_point_deriv.y * rotated_point.x - rotated_point.y * rotated_point_deriv.x) / (rotated_point.x * rotated_point.x);
	// Flipping the sign due to abs()
	quotient_derivative = (rotated_point.y < 0.0f) ? (-quotient_derivative) : quotient_derivative;
	// Chain rule
	float length_deriv = quotient_derivative / fma(quotient, quotient, 1.0f);
	return edge.cdf_factor * vec2(length, length_deriv);
}


/*! Returns the elevation above the horizon of the given edge of a spherical
	polygon at the given azimuth (relative to the counter clockwise vertex of
	the edge).*/
float get_edge_elevation_arvo(edge_arvo_t edge, float relative_azimuth) {
	vec2 dir = vec2(cos(relative_azimuth), sin(relative_azimuth));
	vec2 point = vec2(dot(edge.length_coeffs, dir), dir.y);
	point = normalize(point);
	// cos(atan(point.y, point.x)) == point.x and similarly for sin
	return dot(point, edge.elevations);
}


/*! Swaps vertices lhs and rhs (along with corresponding edges) of the given
	polygon if the shorter path from lhs to rhs is clockwise.
	\note To avoid costly register spilling, lhs and rhs must be compile time
		constants.*/
void compare_and_swap_arvo(inout projected_solid_angle_polygon_arvo_t polygon, uint lhs, uint rhs) {
	float lhs_azimuth = polygon.vertex_azimuths[lhs];
	float flip = polygon.vertex_azimuths[lhs] - polygon.vertex_azimuths[rhs];
	polygon.vertex_azimuths[lhs] = (flip > 0.0f) ? polygon.vertex_azimuths[rhs] : lhs_azimuth;
	polygon.vertex_azimuths[rhs] = (flip > 0.0f) ? lhs_azimuth : polygon.vertex_azimuths[rhs];
	edge_arvo_t lhs_edge = polygon.edges[lhs];
	polygon.edges[lhs] = (flip > 0.0f) ? polygon.edges[rhs] : lhs_edge;
	polygon.edges[rhs] = (flip > 0.0f) ? lhs_edge : polygon.edges[rhs];
}


//! Sorts the vertices of the given convex polygon counterclockwise using a
//! special sorting network. For non-convex polygons, the method may fail.
void sort_convex_polygon_vertices_arvo(inout projected_solid_angle_polygon_arvo_t polygon) {
	if (polygon.vertex_count == 3) {
		compare_and_swap_arvo(polygon, 1, 2);
	}
#if MAX_POLYGON_VERTEX_COUNT >= 4
	else if (polygon.vertex_count == 4) {
		compare_and_swap_arvo(polygon, 1, 3);
	}
#endif
#if MAX_POLYGON_VERTEX_COUNT >= 5
	else if (polygon.vertex_count == 5) {
		compare_and_swap_arvo(polygon, 2, 4);
		compare_and_swap_arvo(polygon, 1, 3);
		compare_and_swap_arvo(polygon, 1, 2);
		compare_and_swap_arvo(polygon, 0, 3);
		compare_and_swap_arvo(polygon, 3, 4);
	}
#endif
#if MAX_POLYGON_VERTEX_COUNT >= 6
	else if (polygon.vertex_count == 6) {
		compare_and_swap_arvo(polygon, 3, 5);
		compare_and_swap_arvo(polygon, 2, 4);
		compare_and_swap_arvo(polygon, 1, 5);
		compare_and_swap_arvo(polygon, 0, 4);
		compare_and_swap_arvo(polygon, 4, 5);
		compare_and_swap_arvo(polygon, 1, 3);
	}
#endif
#if MAX_POLYGON_VERTEX_COUNT >= 7
	else if (polygon.vertex_count == 7) {
		compare_and_swap_arvo(polygon, 2, 5);
		compare_and_swap_arvo(polygon, 1, 6);
		compare_and_swap_arvo(polygon, 5, 6);
		compare_and_swap_arvo(polygon, 3, 4);
		compare_and_swap_arvo(polygon, 0, 4);
		compare_and_swap_arvo(polygon, 4, 6);
		compare_and_swap_arvo(polygon, 1, 3);
		compare_and_swap_arvo(polygon, 3, 5);
		compare_and_swap_arvo(polygon, 4, 5);
	}
#endif
#if MAX_POLYGON_VERTEX_COUNT >= 8
	else if (polygon.vertex_count == 8) {
		compare_and_swap_arvo(polygon, 2, 6);
		compare_and_swap_arvo(polygon, 3, 7);
		compare_and_swap_arvo(polygon, 1, 5);
		compare_and_swap_arvo(polygon, 0, 4);
		compare_and_swap_arvo(polygon, 4, 6);
		compare_and_swap_arvo(polygon, 5, 7);
		compare_and_swap_arvo(polygon, 6, 7);
		compare_and_swap_arvo(polygon, 4, 5);
		compare_and_swap_arvo(polygon, 1, 3);
	}
#endif
	// This comparison is shared by all sorting networks
	compare_and_swap_arvo(polygon, 0, 2);
#if MAX_POLYGON_VERTEX_COUNT >= 4
	if (polygon.vertex_count >= 4) {
		// This comparison is shared by all sorting networks except the one for
		// triangles
		compare_and_swap_arvo(polygon, 2, 3);
	}
#endif
	// This comparison is shared by all sorting networks
	compare_and_swap_arvo(polygon, 0, 1);
}


//! Like prepare_projected_solid_angle_polygon_sampling() but uses Arvo's
//! method.
projected_solid_angle_polygon_arvo_t prepare_projected_solid_angle_polygon_sampling_arvo(uint vertex_count, vec3 vertices[MAX_POLYGON_VERTEX_COUNT]) {
	projected_solid_angle_polygon_arvo_t polygon;
	// Normalize the vertices
	for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
		vertices[i] = normalize(vertices[i]);
	}
	// Compute vertex azimuths and assign edges
	polygon.vertex_count = vertex_count;
	polygon.inner_edge_0.cdf_factor = 1.0f;
	polygon.inner_edge_0.length_coeffs = polygon.inner_edge_0.elevations = vec2(0.0f);
	polygon.vertex_azimuths[0] = atan(vertices[0].y, vertices[0].x);
	polygon.edges[0] = prepare_edge_arvo(vertices[0], vertices[1]);
	edge_arvo_t previous_edge = polygon.edges[0];
	[[unroll]]
	for (uint i = 1; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
		polygon.vertex_azimuths[i] = atan(vertices[i].y, vertices[i].x);
		polygon.vertex_azimuths[i] -= (polygon.vertex_azimuths[i] > polygon.vertex_azimuths[0] + M_PI) ? (2.0f * M_PI): 0.0f;
		polygon.vertex_azimuths[i] += (polygon.vertex_azimuths[i] < polygon.vertex_azimuths[0] - M_PI) ? (2.0f * M_PI): 0.0f;
		if (i > 2 && i == polygon.vertex_count) break;
		edge_arvo_t edge = prepare_edge_arvo(vertices[i], vertices[(i + 1) % MAX_POLYGON_VERTEX_COUNT]);
		// If the edge is an inner edge, the order is going to flip
		polygon.edges[i] = (edge.cdf_factor >= 0.0f) ? edge : previous_edge;
		// In doing so, we drop one edge, unless we store it explicitly
		polygon.inner_edge_0 = (previous_edge.cdf_factor < 0.0f && edge.cdf_factor >= 0.0f) ? previous_edge : polygon.inner_edge_0;
		previous_edge = edge;
	}
	// Same thing for the first vertex (i.e. here we close the loop)
	edge_arvo_t edge = polygon.edges[0];
	polygon.edges[0] = (edge.cdf_factor >= 0.0f) ? edge : previous_edge;
	polygon.inner_edge_0 = (previous_edge.cdf_factor < 0.0f && edge.cdf_factor >= 0.0f) ? previous_edge : polygon.inner_edge_0;
	// Compute projected solid angles per sector and in total
	polygon.projected_solid_angle = 0.0f;
	if (polygon.inner_edge_0.cdf_factor > 0.0f) {
		// In the central case, we have polygon.vertex_count sectors, each
		// bounded by a single edge
		[[unroll]]
		for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
			if (i > 2 && i == polygon.vertex_count) break;
			polygon.sector_projected_solid_angles[i] = get_edge_projected_solid_angle_in_sector_arvo(polygon.edges[i], 0.0f, polygon.vertex_azimuths[(i + 1) % MAX_POLYGON_VERTEX_COUNT] - polygon.vertex_azimuths[i]);
			polygon.projected_solid_angle += polygon.sector_projected_solid_angles[i];
		}
	}
	else {
		// Sort vertices counter clockwise
		sort_convex_polygon_vertices_arvo(polygon);
		// There are polygon.vertex_count - 1 sectors, each bounded by an inner
		// and an outer edge
		edge_arvo_t inner_edge = polygon.inner_edge_0;
		float inner_azimuth = polygon.vertex_azimuths[0];
		edge_arvo_t outer_edge;
		float outer_azimuth = polygon.vertex_azimuths[0];
		[[unroll]]
		for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 1; ++i) {
			if (i > 1 && i + 1 == polygon.vertex_count) break;
			edge_arvo_t vertex_edge = polygon.edges[i];
			float vertex_azimuth = polygon.vertex_azimuths[i];
			if (i == 0) {
				outer_edge = vertex_edge;
			}
			else {
				inner_edge = (vertex_edge.cdf_factor >= 0.0f) ? inner_edge : vertex_edge;
				inner_azimuth = (vertex_edge.cdf_factor >= 0.0f) ? inner_azimuth : vertex_azimuth;
				outer_edge = (vertex_edge.cdf_factor >= 0.0f) ? vertex_edge : outer_edge;
				outer_azimuth = (vertex_edge.cdf_factor >= 0.0f) ? vertex_azimuth : outer_azimuth;
			}
			polygon.sector_projected_solid_angles[i] = get_edge_projected_solid_angle_in_sector_arvo(
				outer_edge, polygon.vertex_azimuths[i] - outer_azimuth, polygon.vertex_azimuths[i + 1] - outer_azimuth);
			polygon.sector_projected_solid_angles[i] += get_edge_projected_solid_angle_in_sector_arvo(
				inner_edge, polygon.vertex_azimuths[i] - inner_azimuth, polygon.vertex_azimuths[i + 1] - inner_azimuth);
			polygon.projected_solid_angle += polygon.sector_projected_solid_angles[i];
		}
	}
	return polygon;
}


/*! This function evaluates the cubic interpolation polynomial through the
	points (x[i], y[i]) for i in {0, 1, 2, 3} at the location sample_x.*/
float evaluate_cubic_interpolation_polynomial(float sample_x, float x[4], float y[4]) {
	// Determine coefficients of the interpolation polynomial using the method
	// of divided differences:
	// https://en.wikipedia.org/wiki/Newton_polynomial
	float y01 = (y[0] - y[1]) / (x[0] - x[1]);
	float y12 = (y[1] - y[2]) / (x[1] - x[2]);
	float y23 = (y[2] - y[3]) / (x[2] - x[3]);
	float y012 = (y01 - y12) / (x[0] - x[2]);
	float y123 = (y12 - y23) / (x[1] - x[3]);
	float y0123 = (y012 - y123) / (x[0] - x[3]);
	// Evaluate the polynomial using Horner's method
	return fma(sample_x - x[0], fma(sample_x - x[1], fma(sample_x - x[2], y0123, y012), y01), y[0]);
}


/*! Like sample_sector_between_edges() but without an inner edge.*/
vec3 sample_sector_within_edge(vec2 random_numbers, float target_projected_solid_angle, edge_arvo_t outer_edge, float outer_azimuth, float azimuth_0, float azimuth_1, uint iteration_count) {
	// Split the sector into three equal parts and sample the four boundaries
	float azimuths[4] = {
		azimuth_0, mix_fma(azimuth_0, azimuth_1, 1.0f / 3.0f),
		mix_fma(azimuth_0, azimuth_1, 2.0f / 3.0f), azimuth_1
	};
	float projected_solid_angles[4];
	for (uint i = 0; i != 4; ++i) {
		projected_solid_angles[i] = get_edge_projected_solid_angle_in_sector_arvo(
			outer_edge, azimuth_0 - outer_azimuth, azimuths[i] - outer_azimuth);
	}
	// Approximate the inverse CDF with a cubic interpolation polynomial
	float sampled_azimuth = evaluate_cubic_interpolation_polynomial(target_projected_solid_angle, projected_solid_angles, azimuths);
	// Now refine the result using Newton-Raphson iterations
	[[dont_unroll]]
	for (uint i = 0; i != iteration_count; ++i) {
		// Evaluate the objective function and its derivative
		vec2 outer_psa = get_edge_projected_solid_angle_in_sector_derivative_arvo(
			outer_edge, azimuth_0 - outer_azimuth, sampled_azimuth - outer_azimuth);
		float error = outer_psa[0] - target_projected_solid_angle;
		float derivative = outer_psa[1];
		// Make a Newton-Raphson step
		sampled_azimuth -= error / derivative;
		// Clamp to the sector to avoid the worst instabilities
		sampled_azimuth = clamp(sampled_azimuth, azimuth_0, azimuth_1);
	}
	// Turn the azimuth into a complete sample
	vec3 sampled_dir;
	sampled_dir.x = cos(sampled_azimuth);
	sampled_dir.y = sin(sampled_azimuth);
	float outer_z = get_edge_elevation_arvo(outer_edge, sampled_azimuth - outer_azimuth);
	sampled_dir.z = sqrt(mix_fma(1.0f, outer_z * outer_z, random_numbers[1]));
	sampled_dir.xy *= sqrt(fma(-sampled_dir.z, sampled_dir.z, 1.0f));
	return sampled_dir;
}


/*! Generates a sample between two edges of a polygon and in a specified
	sector. The sample is distributed uniformly with respect to projected solid
	angle.
	\param random_numbers A pair of independent uniform random numbers on [0,1]
	\param target_projected_solid_angle random_numbers[0] multiplied by the
		projected solid angle of the region to be sampled.
	\param inner_edge, outer_edge The inner and outer edges, as produced by
		produced by sample_edges_in_sector().
	\param inner_azimuth, outer_azimuth The azimuths for the vertices in
		counter clockwise direction of the edges.
	\param azimuth_0, azimuth_1 The azimuths bounding the sector to be sampled.
		They must satisfy azimuth_0 < azimuth_1 and
		azimuth_1 < azimuth_0 + M_PI.
	\param iteration_count The number of Newton iterations to perform.
	\return The sample in Cartesian coordinates.*/
vec3 sample_sector_between_edges(vec2 random_numbers, float target_projected_solid_angle, edge_arvo_t inner_edge, float inner_azimuth, edge_arvo_t outer_edge, float outer_azimuth, float azimuth_0, float azimuth_1, uint iteration_count) {
	// Split the sector into three equal parts and sample the four boundaries
	float azimuths[4] = {
		azimuth_0, mix_fma(azimuth_0, azimuth_1, 1.0f / 3.0f),
		mix_fma(azimuth_0, azimuth_1, 2.0f / 3.0f), azimuth_1
	};
	float projected_solid_angles[4];
	for (uint i = 0; i != 4; ++i) {
		projected_solid_angles[i] = get_edge_projected_solid_angle_in_sector_arvo(
			outer_edge, azimuth_0 - outer_azimuth, azimuths[i] - outer_azimuth);
		projected_solid_angles[i] += get_edge_projected_solid_angle_in_sector_arvo(
			inner_edge, azimuth_0 - inner_azimuth, azimuths[i] - inner_azimuth);
	}
	// Approximate the inverse CDF with a cubic interpolation polynomial
	float sampled_azimuth = evaluate_cubic_interpolation_polynomial(target_projected_solid_angle, projected_solid_angles, azimuths);
	// Now refine the result using Newton-Raphson iterations
	[[dont_unroll]]
	for (uint i = 0; i != iteration_count; ++i) {
		// Evaluate the objective function and its derivative
		vec2 outer_psa = get_edge_projected_solid_angle_in_sector_derivative_arvo(
			outer_edge, azimuth_0 - outer_azimuth, sampled_azimuth - outer_azimuth);
		vec2 inner_psa = get_edge_projected_solid_angle_in_sector_derivative_arvo(
			inner_edge, azimuth_0 - inner_azimuth, sampled_azimuth - inner_azimuth);
		float error = inner_psa[0] + outer_psa[0] - target_projected_solid_angle;
		float derivative = inner_psa[1] + outer_psa[1];
		// Make a Newton-Raphson step
		sampled_azimuth -= error / derivative;
		// Clamp to the sector to avoid the worst instabilities
		sampled_azimuth = clamp(sampled_azimuth, azimuth_0, azimuth_1);
	}
	// Turn the azimuth into a complete sample
	vec3 sampled_dir;
	sampled_dir.x = cos(sampled_azimuth);
	sampled_dir.y = sin(sampled_azimuth);
	float inner_z = get_edge_elevation_arvo(inner_edge, sampled_azimuth - inner_azimuth);
	float outer_z = get_edge_elevation_arvo(outer_edge, sampled_azimuth - outer_azimuth);
	sampled_dir.z = sqrt(mix_fma(inner_z * inner_z, outer_z * outer_z, random_numbers[1]));
	sampled_dir.xy *= sqrt(fma(-sampled_dir.z, sampled_dir.z, 1.0f));
	return sampled_dir;
}


//! Like sample_projected_solid_angle_polygon() but using Arvo's method. The
//! iteration count for Newton's method has to be given explicitly.
vec3 sample_projected_solid_angle_polygon_arvo(projected_solid_angle_polygon_arvo_t polygon, vec2 random_numbers, uint iteration_count) {
	float target_projected_solid_angle = random_numbers[0] * polygon.projected_solid_angle;
	// Distinguish between the central case
	float sector_projected_solid_angle;
	edge_arvo_t outer_edge;
	float outer_azimuth;
	float azimuth_1;
	if (polygon.inner_edge_0.cdf_factor > 0.0f) {
		// Select a sector and copy the relevant attributes
		[[unroll]]
		for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
			if ((i > 2 && i == polygon.vertex_count) || (i > 0 && target_projected_solid_angle < 0.0f))
				break;
			sector_projected_solid_angle = polygon.sector_projected_solid_angles[i];
			target_projected_solid_angle -= sector_projected_solid_angle;
			outer_edge = polygon.edges[i];
			outer_azimuth = polygon.vertex_azimuths[i];
			azimuth_1 = polygon.vertex_azimuths[(i + 1) % MAX_POLYGON_VERTEX_COUNT];
		}
		azimuth_1 = (azimuth_1 < outer_azimuth) ? (azimuth_1 + 2.0f * M_PI) : azimuth_1;
		target_projected_solid_angle += sector_projected_solid_angle;
		random_numbers[0] = target_projected_solid_angle / sector_projected_solid_angle;
		random_numbers[0] = clamp(random_numbers[0], 0.0f, 1.0f);
		return sample_sector_within_edge(random_numbers, target_projected_solid_angle, outer_edge, outer_azimuth, outer_azimuth, azimuth_1, iteration_count);
	}
	// And the decentral case
	else {
		// Select a sector and copy the relevant attributes
		edge_arvo_t inner_edge = polygon.inner_edge_0;
		float inner_azimuth = polygon.vertex_azimuths[0];
		float azimuth_0;
		[[unroll]]
		for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 1; ++i) {
			if ((i > 1 && i + 1 == polygon.vertex_count) || (i > 0 && target_projected_solid_angle < 0.0f))
				break;
			sector_projected_solid_angle = polygon.sector_projected_solid_angles[i];
			target_projected_solid_angle -= sector_projected_solid_angle;
			edge_arvo_t vertex_edge = polygon.edges[i];
			float vertex_azimuth = polygon.vertex_azimuths[i];
			if (i == 0) {
				outer_edge = vertex_edge;
				outer_azimuth = vertex_azimuth;
			}
			else {
				inner_edge = (vertex_edge.cdf_factor >= 0.0f) ? inner_edge : vertex_edge;
				inner_azimuth = (vertex_edge.cdf_factor >= 0.0f) ? inner_azimuth : vertex_azimuth;
				outer_edge = (vertex_edge.cdf_factor >= 0.0f) ? vertex_edge : outer_edge;
				outer_azimuth = (vertex_edge.cdf_factor >= 0.0f) ? vertex_azimuth : outer_azimuth;
			}
			azimuth_0 = polygon.vertex_azimuths[i];
			azimuth_1 = polygon.vertex_azimuths[i + 1];
		}
		target_projected_solid_angle += sector_projected_solid_angle;
		// Sample it
		random_numbers[0] = target_projected_solid_angle / sector_projected_solid_angle;
		random_numbers[0] = clamp(random_numbers[0], 0.0f, 1.0f);
		return sample_sector_between_edges(random_numbers, target_projected_solid_angle, inner_edge, inner_azimuth, outer_edge, outer_azimuth, azimuth_0, azimuth_1, iteration_count);
	}
}


//! Like compute_projected_solid_angle_polygon_sampling_error() but for Arvo's
//! method. Returns the backward error and backward error times projected solid
//! angle only.
vec2 compute_projected_solid_angle_polygon_sampling_error_arvo(projected_solid_angle_polygon_arvo_t polygon, vec2 random_numbers, vec3 sampled_dir) {
	float target_projected_solid_angle = random_numbers[0] * polygon.projected_solid_angle;
	// In the central case, the sampling procedure is exact except for rounding
	// error
	if (polygon.inner_edge_0.cdf_factor > 0.0f) {
		return vec2(0.0f);
	}
	// In the decentral case, we repeat some computations to find the error
	else {
		// Select a sector and copy the relevant attributes
		edge_arvo_t outer_edge;
		edge_arvo_t inner_edge = polygon.inner_edge_0;
		float inner_azimuth = polygon.vertex_azimuths[0];
		float outer_azimuth;
		float sector_projected_solid_angle;
		float azimuth_0;
		[[unroll]]
		for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 1; ++i) {
			if ((i > 1 && i + 1 == polygon.vertex_count) || (i > 0 && target_projected_solid_angle < 0.0f))
				break;
			sector_projected_solid_angle = polygon.sector_projected_solid_angles[i];
			target_projected_solid_angle -= sector_projected_solid_angle;
			edge_arvo_t vertex_edge = polygon.edges[i];
			float vertex_azimuth = polygon.vertex_azimuths[i];
			if (i == 0) {
				outer_edge = vertex_edge;
				outer_azimuth = vertex_azimuth;
			}
			else {
				inner_edge = (vertex_edge.cdf_factor >= 0.0f) ? inner_edge : vertex_edge;
				inner_azimuth = (vertex_edge.cdf_factor >= 0.0f) ? inner_azimuth : vertex_azimuth;
				outer_edge = (vertex_edge.cdf_factor >= 0.0f) ? vertex_edge : outer_edge;
				outer_azimuth = (vertex_edge.cdf_factor >= 0.0f) ? vertex_azimuth : outer_azimuth;
			}
			azimuth_0 = polygon.vertex_azimuths[i];
		}
		target_projected_solid_angle += sector_projected_solid_angle;
		// Compute the area in the sector from sampled_dir and dir_0 between
		// the two ellipses
		float sampled_azimuth = atan(sampled_dir.y, sampled_dir.x);
		float outer_psa = get_edge_projected_solid_angle_in_sector_derivative_arvo(
			outer_edge, azimuth_0 - outer_azimuth, sampled_azimuth - outer_azimuth)[0];
		float inner_psa = get_edge_projected_solid_angle_in_sector_derivative_arvo(
			inner_edge, azimuth_0 - inner_azimuth, sampled_azimuth - inner_azimuth)[0];
		float sampled_projected_solid_angle = outer_psa + inner_psa;
		// Convert to a random number
		return vec2(
			(target_projected_solid_angle - sampled_projected_solid_angle) / polygon.projected_solid_angle,
			target_projected_solid_angle - sampled_projected_solid_angle);
	}
}
