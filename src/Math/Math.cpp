#include "Math.h"

void Math::FastFourierTransform(Complex* vals, uint32 amount) {
	// Input must be a power of two
	ASSERT((amount & (amount - 1)) == 0);

	// CODE COPIED FROM: https://rosettacode.org/wiki/Fast_Fourier_transform#C.2B.2B

	uint32 k = amount;
	Complex::value_type thetaT = M_PI / amount;
	Complex phiT = Complex(cos(thetaT), -sin(thetaT)), T;
	while (k > 1) {
		uint32 n = k;
		k >>= 1;
		phiT = phiT * phiT;
		T = 1;
		for (uint32 l = 0; l < k; l++) {
			for (uint32 a = l; a < amount; a += n) {
				uint32 b = a + k;
				Complex t = vals[a] - vals[b];
				vals[a] += vals[b];
				vals[b] = t * T;
			}
			T *= phiT;
		}
	}

	// Decimate
	uint32 m = (uint32)log2f(amount);
	for (uint32 a = 0; a < amount; a++) {
		uint32 b = a;
		// Reverse bits
		b = (((b & 0xaaaaaaaa) >> 1) | ((b & 0x55555555) << 1));
		b = (((b & 0xcccccccc) >> 2) | ((b & 0x33333333) << 2));
		b = (((b & 0xf0f0f0f0) >> 4) | ((b & 0x0f0f0f0f) << 4));
		b = (((b & 0xff00ff00) >> 8) | ((b & 0x00ff00ff) << 8));
		b = ((b >> 16) | (b << 16)) >> (32 - m);
		if (b > a) {
			// Swap
			Complex t = vals[a];
			vals[a] = vals[b];
			vals[b] = t;
		}
	}
}