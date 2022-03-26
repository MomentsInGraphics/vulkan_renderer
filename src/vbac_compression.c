/*
Copyright 2021 Bastian Kuth bastian.kuth@stud.hs-coburg.de, Quirin Meyer

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <vbac/vbac_compression.h>

/* struct used to store a weight 4-tuple */
typedef struct { float x, y, z, w; } vbac_vec4;

static uint64_t const oss_LUT_N[65] = {
	        /*0*/    /*1*/    /*2*/    /*3*/    /*4*/    /*5*/    /*6*/    /*7*/
	/*0*/	       0,       1,       2,       3,       5,       6,      9,      11,
	/*8*/       15,      19,      24,      31,      40,      51,     65,      82,
	/*16*/     104,     131,     166,     209,     264,     333,    421,     531,
	/*24*/     669,     843,    1063,    1340,    1689,    2128,   2682,    3379,
	/*32*/    4258,    5365,    6760,    8518,   10733,   13523,  17038,   21467,
	/*40*/   27047,   34078,   42936,   54097,   68158,   85874, 108196,  136318,
	/*48*/  171751,  216393,  272639,  343504,  432788,  545279, 687010,  865578,
	/*56*/ 1090561, 1374021, 1731159, 2181124, 2748045, 3462320, 432788, 5496091,
	/*64*/ 6924641
};

/* --- DECLARATION OF INTERNAL FUNCTIONS --- */
static uint64_t oss_baseIdx3(uint64_t ic, uint64_t n);
static uint64_t oss_solveForI3(uint64_t I, uint64_t n);
static uint64_t oss_baseIdx4(uint64_t id, uint64_t n);
static uint64_t oss_solveForI4(uint64_t I, uint64_t n, uint64_t MI4);
static vbac_vec4 oss_decompressTuple(uint64_t I, vbac_oss_info const info);
static uint64_t minU64(uint64_t a, uint64_t b);


/* --- IMPLEMENTATION OF INTERFACE FUNCTIONS --- */
void vbac_sortTuplesByWeight(float* const weightTuples, bone_index* const indexTuples, size_t const weightsPerTuple, size_t const nTuples) {
	for (size_t i = 0; i < nTuples; i++) {
		for (size_t j = 0; j < weightsPerTuple - 1; j++) {
			size_t maxIdx = j;
			float maxValue = weightTuples[i * weightsPerTuple + j];
			for (size_t k = j + 1; k < weightsPerTuple; k++) {
				if (maxValue < weightTuples[i * weightsPerTuple + k]) {
					maxValue = weightTuples[i * weightsPerTuple + k];
					maxIdx = k;
				}
			}
			/* swaps */
			weightTuples[i * weightsPerTuple + maxIdx] = weightTuples[i * weightsPerTuple + j];
			weightTuples[i * weightsPerTuple + j] = maxValue;

			bone_index t = indexTuples[i * weightsPerTuple + maxIdx];
			indexTuples[i * weightsPerTuple + maxIdx] = indexTuples[i * weightsPerTuple + j];
			indexTuples[i * weightsPerTuple + j] = t;
		}
	}
}

vbac_oss_info vbac_oss_compress(
	float const* weightTuples,
	size_t const nTuples,
	size_t const totalBits,
	uint64_t* const compressedData
) {
	uint64_t nPoints = 1ull << totalBits;

	uint64_t N = oss_LUT_N[totalBits];
	vbac_oss_info const info = {
		.N = N,
		.MI4 = oss_baseIdx4(0u, info.N),
		.scale = .5 / (info.N - 1ull)
	};

	double v2, v3, v4;
	uint64_t i, j, k;

	for (int tplIdx = 0; tplIdx < nTuples; tplIdx++) {
		v2 = weightTuples[tplIdx * 4 + 1];
		v3 = weightTuples[tplIdx * 4 + 2];
		v4 = weightTuples[tplIdx * 4 + 3];

		uint64_t toj = 0;
		uint64_t tok = 0;

		uint64_t N = info.N;

		k = v4 / info.scale + .5;
		k = minU64(k, N / 2. - .5); /* clamp */
		v4 = k * info.scale;
		tok = info.MI4 - oss_baseIdx4(k, N);
		N -= 2ull * k;

		j = (v3 - v4) / info.scale + .5;
		j = minU64(j, (2ull*N + 1ull) / 3. - 1.);
		v3 = j * info.scale;
		toj = (N * j - j * j * 3. / 4. + j / 2. + 1. / 4.);
		N -= ((3ull * j) / 2.);

		i = (v2 - v3 - v4) / info.scale + .5;
		i = minU64(i, N - 1);

		compressedData[tplIdx] = i + toj + tok;
	}
	return info;
}
void vbac_oss_decompress(
	uint64_t const* compressedData,
	size_t const nTuples,
	vbac_oss_info const info,
	float* weightTuples
) {
	for (size_t i = 0; i < nTuples; i++) {
		vbac_vec4 v = oss_decompressTuple(compressedData[i], info);
		weightTuples[i * 4 + 0] = v.x;
		weightTuples[i * 4 + 1] = v.y;
		weightTuples[i * 4 + 2] = v.z;
		weightTuples[i * 4 + 3] = v.w;
	}
}


/* --- IMPLEMENTATION OF INTERNAL FUNCTIONS --- */

static uint64_t oss_baseIdx3(uint64_t ic, uint64_t n) {
	uint64_t a = 2ull * n - 3ull * ic + 1ull;
	uint64_t a2 = a * a;
	uint64_t r = a2 % 12ull;
	return a2 / 12ull + (r >= 6ull);
}
static uint64_t oss_solveForI3(uint64_t I, uint64_t n) {
	uint64_t X = oss_baseIdx3(0, n) - I;
	uint64_t a = (uint64_t)(2. * n + 1. - sqrt(12ull * X));
	uint64_t ic = a / 3ull;

	/* fix off-by-one */
	uint64_t lower = oss_baseIdx3(ic, n);
	uint64_t upper = oss_baseIdx3(ic + 1ull, n);

	return ic - (X > lower) + (X <= upper);
}
static uint64_t oss_baseIdx4(uint64_t id, uint64_t n) {
	uint64_t a = 2ull * id - n - 1ull;
	uint64_t a2 = (a * a) / 36ull;
	uint64_t a2r = (a * a) % 36ull;
	uint64_t b = 3ull - 2ull * a;
	uint64_t I = a2 * b + (a2r * b + 18ull) / 36ull;
	return I;
}
static uint64_t oss_solveForI4(uint64_t I, uint64_t n, uint64_t MI4) {
	/* uint64_t X = oss_baseIdx4(0u, n) - I; */
	/* As oss_baseIdx4(0u, n) is static, we can precompute it on the CPU and store it in MI4 instead */
	uint64_t X = MI4 - I;
	double b = (double)(X) * 144.;
	double cr = pow((double)(b), 1./3.);
	double f = (cr + 1. / cr);
	uint64_t id = (n * 2ull + 3ull - (int64_t)(f)) / 4ull;
	uint64_t lower = oss_baseIdx4(id, n);
	return id - (X > lower);
}
static vbac_vec4 oss_decompressTuple(uint64_t I, vbac_oss_info const info) {
	uint64_t i, j, k;

	uint64_t N = info.N;
	k = oss_solveForI4(I, N, info.MI4);

	/* oss_baseIdx4(0u, N) computed on CPU and saved in info.MI4 */
	I -= info.MI4 - oss_baseIdx4(k, N);
	N -= 2ull * k;
	j = oss_solveForI3(I, N);

	I -= (N*N+N+1)/3ull - oss_baseIdx3(j, N);
	i = I;

	/* shear alias delta code */
	j += k;
	i += j;

	vbac_vec4 v = { .x = 1.f, .y = 0.f, .z = 0.f, .w = 0.f };

	v.y = (float)(i)*info.scale;
	v.x -= v.y;

	v.z = (float)(j)*info.scale;
	v.x -= v.z;

	v.w = (float)(k)*info.scale;
	v.x -= v.w;

	return v;
}
static uint64_t minU64(uint64_t a, uint64_t b) {
	return (a < b) ? a : b;
}