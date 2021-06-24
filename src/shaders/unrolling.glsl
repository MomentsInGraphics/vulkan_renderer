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


/*! \file
	Invoking ray tracing instructions inside of loops deteriorates performance
	massively. The [[unroll]] directive helps to some extent but blatantly
	copying the code for each run through the loop is far more effective (speed
	ups around 0.6 ms per frame at 1920x1080 on a 2070 Super). This header
	provides preprocessor directives to implement this unrolling in the least
	ugly way possible.*/

//! Versions of UNROLLED_FOR_LOOP() with fixed count
#define UNROLLED_FOR_LOOP_0(I, N, C) 
#define UNROLLED_FOR_LOOP_1(I, N, C) {uint I=0; C}
#define UNROLLED_FOR_LOOP_2(I, N, C) {uint I=0; C} {uint I=1; C}
#define UNROLLED_FOR_LOOP_3(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C}
#define UNROLLED_FOR_LOOP_4(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C}
#define UNROLLED_FOR_LOOP_5(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C}
#define UNROLLED_FOR_LOOP_6(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C}
#define UNROLLED_FOR_LOOP_7(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C}
#define UNROLLED_FOR_LOOP_8(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C}
#define UNROLLED_FOR_LOOP_9(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C}
#define UNROLLED_FOR_LOOP_10(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C}
#define UNROLLED_FOR_LOOP_11(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C}
#define UNROLLED_FOR_LOOP_12(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C}
#define UNROLLED_FOR_LOOP_13(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C}
#define UNROLLED_FOR_LOOP_14(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C}
#define UNROLLED_FOR_LOOP_15(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C}
#define UNROLLED_FOR_LOOP_16(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C}
#define UNROLLED_FOR_LOOP_17(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C}
#define UNROLLED_FOR_LOOP_18(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C}
#define UNROLLED_FOR_LOOP_19(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C}
#define UNROLLED_FOR_LOOP_20(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C}
#define UNROLLED_FOR_LOOP_21(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C}
#define UNROLLED_FOR_LOOP_22(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C}
#define UNROLLED_FOR_LOOP_23(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C} {uint I=22; C}
#define UNROLLED_FOR_LOOP_24(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C} {uint I=22; C} {uint I=23; C}
#define UNROLLED_FOR_LOOP_25(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C} {uint I=22; C} {uint I=23; C} {uint I=24; C}
#define UNROLLED_FOR_LOOP_26(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C} {uint I=22; C} {uint I=23; C} {uint I=24; C} {uint I=25; C}
#define UNROLLED_FOR_LOOP_27(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C} {uint I=22; C} {uint I=23; C} {uint I=24; C} {uint I=25; C} {uint I=26; C}
#define UNROLLED_FOR_LOOP_28(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C} {uint I=22; C} {uint I=23; C} {uint I=24; C} {uint I=25; C} {uint I=26; C} {uint I=27; C}
#define UNROLLED_FOR_LOOP_29(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C} {uint I=22; C} {uint I=23; C} {uint I=24; C} {uint I=25; C} {uint I=26; C} {uint I=27; C} {uint I=28; C}
#define UNROLLED_FOR_LOOP_30(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C} {uint I=22; C} {uint I=23; C} {uint I=24; C} {uint I=25; C} {uint I=26; C} {uint I=27; C} {uint I=28; C} {uint I=29; C}
#define UNROLLED_FOR_LOOP_31(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C} {uint I=22; C} {uint I=23; C} {uint I=24; C} {uint I=25; C} {uint I=26; C} {uint I=27; C} {uint I=28; C} {uint I=29; C} {uint I=30; C}
#define UNROLLED_FOR_LOOP_32(I, N, C) {uint I=0; C} {uint I=1; C} {uint I=2; C} {uint I=3; C} {uint I=4; C} {uint I=5; C} {uint I=6; C} {uint I=7; C} {uint I=8; C} {uint I=9; C} {uint I=10; C} {uint I=11; C} {uint I=12; C} {uint I=13; C} {uint I=14; C} {uint I=15; C} {uint I=16; C} {uint I=17; C} {uint I=18; C} {uint I=19; C} {uint I=20; C} {uint I=21; C} {uint I=22; C} {uint I=23; C} {uint I=24; C} {uint I=25; C} {uint I=26; C} {uint I=27; C} {uint I=28; C} {uint I=29; C} {uint I=30; C} {uint I=31; C}

//! Just use a loop
#define UNROLLED_FOR_LOOP_33(I, N, C) for (uint I = 0; I != (N); ++I) { C }

//! Used to force macro expansion on CLAMPED_COUNT
#define UNROLLED_FOR_LOOP_WRAPPER(INDEX, COUNT, CLAMPED_COUNT, CODE) UNROLLED_FOR_LOOP_##CLAMPED_COUNT(INDEX, COUNT, CODE)

//#define UNROLLED_FOR_LOOP_WRAPPER(INDEX, COUNT, CLAMPED_COUNT, CODE) for (uint INDEX = 0; INDEX != (COUNT); ++INDEX) { CODE }

//! Implements a loop with unrolling up to 32 iterations. CLAMPED_COUNT has to
//! be the minimum of COUNT and 33. If COUNT < 33, CODE is duplicated COUNT
//! times, otherwise a loop is used. Each instance of CODE is in its own scope
//! where the variable uint INDEX is set to a zero-based loop index. Loop
//! indices go in increasing order up to COUNT - 1.
#define UNROLLED_FOR_LOOP(INDEX, COUNT, CLAMPED_COUNT, CODE) UNROLLED_FOR_LOOP_WRAPPER(INDEX, COUNT, CLAMPED_COUNT, CODE)
