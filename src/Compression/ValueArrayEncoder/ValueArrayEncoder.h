#pragma once
#include "../../DataStreams/DataStreams.h"

// Compresses a large array of integer values of any constant bit length

namespace ValueArrayEncoder {

	typedef uint32 ArrayVal;
	constexpr int MAX_BITS_PER_VAL = sizeof(ArrayVal) * 8;

	bool Encode(DataReader& in, int bitsPerVal, size_t valAmount, DataWriter& out);

	bool Decode(DataReader& in, int bitsPerVal, size_t valAmount, void* dataOut);
}