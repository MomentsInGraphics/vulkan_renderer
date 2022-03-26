/*
Copyright 2021 Bastian Kuth bastian.kuth@stud.hs-coburg.de, Quirin Meyer

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/* struct that gets generated by the ssq compression function. Needs to be passed to the decompression function */
struct vbac_ssq_fix4_info {
	uint N;      /* number of sample points at the v2 axis									 */
	uint MI4;    /* maximum Index into v4 axis (used for flipping the function)				 */
	float scale; /* distance between two saple points (twice the maximum quantization error) */
};

vbac_ssq_fix4_info vbac_ssq_info = vbac_ssq_fix4_info (209 , 518175, 0.0024038461538461540);


uint ssq_baseIdx3(uint ic, uint n) {
	uint a = 2u * n-3u * ic + 1u;
	uint a2 = a * a;
	uint r = (a2) % 12u;
	return a2 / 12u + uint(r >= 6);
}
uint ssq_solveForI3(uint I, uint n) {
	uint X = ssq_baseIdx3(0u, n) - I;
	float a = 2.f * float(n)+1.f - sqrt(12u * X);
	uint ic = uint(a) / 3u;

	/* fix off-by-one */
	uint lower = ssq_baseIdx3(ic, n);
	uint upper = ssq_baseIdx3(ic + 1u, n);

	return ic - uint(X > lower) + uint(X <= upper);
}
uint ssq_baseIdx4(uint id, uint n) {
	uint a = 2u * id - n - 1u;
	uint a2 = (a * a) / 36u;
	uint a2r = (a * a) % 36u;
	uint b = 3u - 2u * a;
	uint I = a2 * b + (a2r * b + 18u) / 36u;
	return I;
}
uint ssq_solveForI4(uint I, uint n, uint MI4) {
	/* uint X = ssq_baseIdx4(0u, n) - I; */
	/* As ssq_baseIdx4(0u, n) is static, we can precompute it on the CPU and store it in MI4 instead */
	uint X = MI4 - I;
	float b = float(X) * 144.;
	float cr = pow(float(b), 1. / 3.);
	float f = (cr + float(1.0) / cr);
	uint id = uint((int(n) * 2 + 3 - int(f)) / 4);
	uint lower = ssq_baseIdx4(id, n);
	return id - uint(X > lower);
}

//vbac_vec4 ssq_fix4_decompressTuple(uint I, vbac_ssq_fix4_info const info) {
vec4 decompress_optimal_simplex_sampling (uint I){
	uint i, j, k;

	uint N = vbac_ssq_info.N;
	k = ssq_solveForI4(I, N, vbac_ssq_info.MI4);

	/* ssq_baseIdx4(0u, N) computed on CPU and saved in info.MI4 */
	I -= vbac_ssq_info.MI4 - ssq_baseIdx4(k, N);
	N -= 2u * k;
	j = ssq_solveForI3(I, N);

	I -= (N*N+N+1)/3u - ssq_baseIdx3(j, N);
	i = I;

	/* shear alias delta code */
	j += k;
	i += j;

	vec4 v = { /* .x = */ 1.f, /* .y = */ 0.f, /* .z = */ 0.f, /* .w = */ 0.f };

	v.y = float(i)*vbac_ssq_info.scale;
	v.x -= v.y;

	v.z = float(j)*vbac_ssq_info.scale;
	v.x -= v.z;

	v.w = float(k)*vbac_ssq_info.scale;
	v.x -= v.w;

	return v;
}
