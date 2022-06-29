#pragma once
#include "../Framework.h"
#include "../Math/Math.h"
#include "../WaveIO/WaveIO.h"

// Version number
#define ZCAC_VERSION_MAJOR 0
#define ZCAC_VERSION_MINOR 0
#define ZCAC_VERSION_NUM ((ZCAC_VERSION_MAJOR << 16) | ZCAC_VERSION_MINOR)

// Size of fourier transform input
#define ZCAC_FFT_SIZE 1024

// Overlap between FFT blocks to prevent edge effects
#define ZCAC_FFT_PAD (ZCAC_FFT_SIZE / 64)

// Bits per FFT integer
#define ZCAC_INT_VAL_BITS 9
#define ZCAC_INT_VAL_MAX ((1<<ZCAC_INT_VAL_BITS) - 1)

// FFT size for storing our FFT result (due to hermitian symmetry)
#define ZCAC_FFT_SIZE_STORAGE (ZCAC_FFT_SIZE / 2 + 1)

// Must be a power of two
SASSERT(!(ZCAC_FFT_SIZE& (ZCAC_FFT_SIZE - 1)));

#define ZCAC_MAGIC 'CACZ' // "ZCAC"

namespace ZCAC {

	// Special version of std::complex that uses integers of a dynamic length for real/imag
	// Actual stored integers are 16 bit
	struct ComplexInts {
		uint16 real, imag;

		ComplexInts(uint16 real = 0, uint16 imag = 0) {
			ASSERT(real <= ZCAC_INT_VAL_MAX);
			ASSERT(imag <= ZCAC_INT_VAL_MAX);

			this->real = real;
			this->imag = imag;
		}

		ComplexInts(Math::Complex complex) {
			real = roundf(CLAMP(complex.real(), 0.f, 1.f) * ZCAC_INT_VAL_MAX);
			imag = roundf(CLAMP(complex.imag(), 0.f, 1.f) * ZCAC_INT_VAL_MAX);
		}


		Math::Complex ToComplex() {
			return Math::Complex{
				real / (Math::Complex::value_type)ZCAC_INT_VAL_MAX,
				imag / (Math::Complex::value_type)ZCAC_INT_VAL_MAX,
			};
		}
	};

	struct FFTBlock {
		ComplexInts data[ZCAC_FFT_SIZE_STORAGE];

		float rangeMin = FLT_MAX, rangeMax = -FLT_MAX;

		static FFTBlock FromAudioData(const float* audioData);
		void ToAudioData(float* audioDataOut);
	};

	bool Encode(const WaveIO::AudioInfo& waveAudioInfo, DataWriter& out);
	bool Decode(DataReader in, WaveIO::AudioInfo& audioInfoOut);
}