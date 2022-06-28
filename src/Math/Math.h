#pragma once
#include "../Framework.h"

namespace Math {
	typedef std::complex<float> Complex;
	void FastFourierTransform(Complex* vals, uint32 amount);
}