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


#include "math_constants.glsl"

//! All information about a shadoing point that is needed to perform shading
//! using world space coordinates
struct shading_data_t {
	//! The position of the shading point in world space
	vec3 position;
	//! The normalized world space shading normal
	vec3 normal;
	//! The normalized world-space direction towards the eye (or the outgoing
	//! light direction if global illumination is considered)
	vec3 outgoing;
	//! dot(normal, outgoing)
	float lambert_outgoing;
	//! The RGB diffuse albedo. Dependent on the BRDF an additional direction-
	//! dependent factor may come on top of that
	vec3 diffuse_albedo;
	//! The color of specular reflection at zero degrees inclination
	vec3 fresnel_0;
	//! Roughness coefficient for the GGX distribution of normals
	float roughness;
};


/*! An implementation of the Schlick approximation for the Fresnel term.*/
vec3 fresnel_schlick(vec3 fresnel_0, vec3 fresnel_90, float cos_theta) {
	float flipped = 1.0f - cos_theta;
	float flipped_squared = flipped * flipped;
	return fresnel_0 + (fresnel_90 - fresnel_0) * (flipped_squared * flipped * flipped_squared);
}


/*! Evaluates the full BRDF with both diffuse and specular terms (unless they
	are disabled by the given booleans). The diffuse BRDF is Disney diffuse.
	The specular BRDF is Frostbite specular, i.e. a microfacet BRDF with GGX
	normal distribution function, Smith masking-shadowing function and Fresnel-
	Schlick approximation. The material model as a whole, agrees with the model
	proposed for Frostbite:
	https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
	https://dl.acm.org/doi/abs/10.1145/2614028.2615431 */
vec3 evaluate_brdf(shading_data_t data, vec3 incoming_light_direction, bool diffuse, bool specular) {
	// A few computations are shared between diffuse and specular evaluation
	vec3 half_vector = normalize(incoming_light_direction + data.outgoing);
	float lambert_incoming = dot(data.normal, incoming_light_direction);
	float outgoing_dot_half = dot(data.outgoing, half_vector);
	vec3 brdf = vec3(0.0f);

	// Disney diffuse BRDF
	if (diffuse) {
		float fresnel_90 = fma(outgoing_dot_half * outgoing_dot_half, 2.0f * data.roughness, 0.5f);
		float fresnel_product =
			fresnel_schlick(vec3(1.0f), vec3(fresnel_90), data.lambert_outgoing).x
			* fresnel_schlick(vec3(1.0f), vec3(fresnel_90), lambert_incoming).x;
		brdf += fresnel_product * data.diffuse_albedo;
	}
	// Frostbite specular BRDF
	if (specular) {
		float normal_dot_half = dot(data.normal, half_vector);
		// Evaluate the GGX normal distribution function
		float roughness_squared = data.roughness * data.roughness;
		float ggx = fma(fma(normal_dot_half, roughness_squared, -normal_dot_half), normal_dot_half, 1.0f);
		ggx = roughness_squared / (ggx * ggx);
		// Evaluate the Smith masking-shadowing function
		float masking = lambert_incoming * sqrt(fma(fma(-data.lambert_outgoing, roughness_squared, data.lambert_outgoing), data.lambert_outgoing, roughness_squared));
		float shadowing = data.lambert_outgoing * sqrt(fma(fma(-lambert_incoming, roughness_squared, lambert_incoming), lambert_incoming, roughness_squared));
		float smith = 0.5f / (masking + shadowing);
		// Evaluate the Fresnel term and put it all together
		vec3 fresnel = fresnel_schlick(data.fresnel_0, vec3(1.0f), clamp(outgoing_dot_half, 0.0f, 1.0f));
		brdf += ggx * smith * fresnel;
	}
	return brdf * M_INV_PI;
}


//! Overload that evaluates both diffuse and specular
vec3 evaluate_brdf(shading_data_t data, vec3 incoming_light_direction) {
	return evaluate_brdf(data, incoming_light_direction, true, true);
}


/*! Samples a unit direction in the upper hemisphere. For uniform inputs, its
	density matches the distribution of visible normals in the GGX normal
	distribution function for the given outgoing direction. Inputs and outputs
	are in a local shading space where the z-axis is aligned with the surface
	normal.
	\param outgoing_shading_space The outgoing light direction in shading
		space. Need not be normalized.
	\param roughness The roughness parameters (also known as alpha) along the
		x- and y-axis of shading space for the considered GGX normal
		distribution.
	\param random_numbers In naive Monte Carlo, this should be two independent
		random numbers, distributed uniformly in [0,1).
	\return The sampled direction in shading space. It is normalized and
		z>=0.0f.
	\note This technique is due to the supplementary of a work by Walter et al.
		and the implementation is a rewrite of code by Heitz:
		- Bruce Walter, Zhao Dong, Steve Marschner, Donald P. Greenberg, 2015,
		  ACM Transactions on Graphics 35:1, The Ellipsoid Normal Distribution
		  Function
		  https://doi.org/10.1145/2815618
		  http://www.cs.cornell.edu/Projects/metalappearance/SupplementalEllipsoidNDF.pdf
		- Eric Heitz, Sampling the GGX Distribution of Visible Normals, 2018,
		  Journal of Computer Graphics Techniques (JCGT) 7:4
		  http://jcgt.org/published/0007/04/01/
*/
vec3 sample_ggx_visible_normal_distribution(vec3 outgoing_shading_space, vec2 roughness, vec2 random_numbers) {
	// We work with three coordinate frames:
	// - Shading space arises from world space through a rotation but the
	//   z-axis is aligned with the surface normal and the x- and y-axes
	//   correspond to the roughness values (can be arbitrary if
	//   roughness.x == roughness.y),
	// - Hemisphere space is obtained from shading space by multiplying x and y
	//   by the corresponding roughness values. In hemisphere space, the halved
	//   GGX ellipsoid that defines the normal distribution is a hemisphere.
	// - Ellipse space arises from hemisphere space through a rotation. The
	//   z-axis is the outgoing light direction, the x-axis is situated in the
	//   xy-plane of hemisphere space.
	
	// Construct the hemisphere to shading space transform
	mat3 ellipse_to_hemi;
	ellipse_to_hemi[2] = normalize(vec3(roughness, 1.0f) * outgoing_shading_space);
	float length_sq = dot(ellipse_to_hemi[2].xy, ellipse_to_hemi[2].xy);
	ellipse_to_hemi[0] = vec3(-ellipse_to_hemi[2].y, ellipse_to_hemi[2].x, 0.0f) * inversesqrt(length_sq);
	ellipse_to_hemi[0] = (length_sq <= 0.0f) ? vec3(1.0f, 0.0f, 0.0f) : ellipse_to_hemi[0];
	ellipse_to_hemi[1] = cross(ellipse_to_hemi[2], ellipse_to_hemi[0]);
	// Use the random numbers to sample a unit disk uniformly
	float radius = sqrt(random_numbers[0]);
	float azimuth = (2.0f * M_PI) * random_numbers[1];
	vec2 disk_sample = radius * vec2(cos(azimuth), sin(azimuth));
	// Scale and offset along the y-axis to turn this into a uniform sample in
	// the union of two halved ellipsoids
	vec3 sample_ellipse_space;
	sample_ellipse_space.x = disk_sample.x;
	float lerp_factor = fma(0.5f, ellipse_to_hemi[2].z, 0.5f);
	sample_ellipse_space.y = mix(sqrt(fma(-disk_sample.x, disk_sample.x, 1.0f)), disk_sample.y, lerp_factor);
	// Fill in the z-coordinate for unit length to obtain the sample in sample
	// space
	sample_ellipse_space.z = sqrt(max(0.0f, 1.0f - dot(sample_ellipse_space.xy, sample_ellipse_space.xy)));
	// Transform from ellipse space to hemisphere space
	vec3 sample_hemi_space = ellipse_to_hemi * sample_ellipse_space;
	// Transform back to shading space and renormalize. It is a normal vector,
	// so we use the inverse transpose of the inverse shading to hemisphere
	// space transform. Conveniently, the double inverse cancels out and the
	// transpose is irrelevant for a diagonal matrix.
	return normalize(vec3(roughness, 1.0f) * sample_hemi_space);
}


/*! This function evaluates the density that is sampled by
	sample_ggx_visible_normal_distribution(). It is normalized such that it
	integrates to one as the microfacet normal varies over the solid angle of
	the upper hemisphere.
	\param outgoing_dot_normal The dot product between the outgoing light
		direction and the geometric normal.
	\param microfacet_dot_normal The dot product between the microfacet normal
		as produced by sample_ggx_visible_normal_distribution() and the
		geometric normal.
	\param microfacet_dot_outgoing The dot product of the microfacet normal
		with the outgoing light direction.
	\param roughness The roughness for an isotropic material (anisotropy is not
		supported at this point.
	\return The density of the normal (not the reflected vector) with respect
		to the solid angle measure.*/
float get_ggx_visible_normal_density(float outgoing_dot_normal, float microfacet_dot_normal, float microfacet_dot_outgoing, float roughness) {
	// Evaluation of the GGX microfacet normal density is borrowed from the
	// Frostbite BRDF above
	float roughness_squared = roughness * roughness;
	float ggx = fma(fma(microfacet_dot_normal, roughness_squared, -microfacet_dot_normal), microfacet_dot_normal, 1.0f);
	ggx = roughness_squared / (ggx * ggx);
	ggx *= M_INV_PI;
	// Evaluate the masking term (except for a factor that will cancel later)
	float masking_over_out_z = sqrt(fma(fma(-outgoing_dot_normal, roughness_squared, outgoing_dot_normal), outgoing_dot_normal, roughness_squared));
	masking_over_out_z = 2.0f / (outgoing_dot_normal + masking_over_out_z);
	return masking_over_out_z * microfacet_dot_outgoing * ggx;
}


/*! Samples a visible normal from the GGX distribution for the given outgoing
	direction and reflects the outgoing direction with respect to this normal.
	The resulting direction is distributed approximately proportional to the
	GGX BRDF. Parameters forward to sample_ggx_visible_normal_distribution().
	It also outputs the density (which may be computed independently by
	get_ggx_reflected_direction_density()).*/
vec3 sample_ggx_reflected_direction(out float out_density, vec3 outgoing_shading_space, float roughness, vec2 random_numbers) {
	// Sample the microfacet normal
	vec3 micro_normal = sample_ggx_visible_normal_distribution(outgoing_shading_space, vec2(roughness), random_numbers);
	float micro_dot_out = dot(micro_normal, outgoing_shading_space);
	out_density = get_ggx_visible_normal_density(outgoing_shading_space.z, micro_normal.z, micro_dot_out, roughness);
	// Mirror the outgoing light direction
	vec3 incoming = fma(vec3(2.0f * micro_dot_out), micro_normal, -outgoing_shading_space);
	// Adapt the density to the mirroring
	out_density /= 4.0f * micro_dot_out;
	return incoming;
}

/*! Returns the density with respect to solid angle measure for the direction
	returned by sample_ggx_reflected_direction(). */
float get_ggx_reflected_direction_density(float outgoing_dot_normal, vec3 outgoing_dir, vec3 incoming_dir, vec3 surface_normal, float roughness) {
	// Construct the half-vector and related quantities
	vec3 micro_normal = normalize(outgoing_dir + incoming_dir);
	float micro_dot_out = dot(micro_normal, outgoing_dir);
	float micro_dot_normal = dot(micro_normal, surface_normal);
	// Compute the density for the microfacet normal
	float density = get_ggx_visible_normal_density(outgoing_dot_normal, micro_dot_normal, micro_dot_out, roughness);
	// Adapt the density to the mirroring
	density /= 4.0f * micro_dot_out;
	return density;
}
