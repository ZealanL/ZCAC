#pragma once
#include "../../DataStreams/DataStreams.h"

// Compresses a large array of integer values of any constant bit length
// TODO: Currently just uses ZLIB, we should make our own actual encoder specifically for FFT values and such

namespace ValueArrayEncoder {

	constexpr int MAX_BITS_PER_VAL = 32;
	SASSERT(MAX_BITS_PER_VAL % 8 == 0); // Should be rounded to byte count

	bool Encode(DataReader& in, int bitsPerVal, size_t valAmount, DataWriter& out);

	bool Decode(DataReader& in, int bitsPerVal, size_t valAmount, void* dataOut);
}