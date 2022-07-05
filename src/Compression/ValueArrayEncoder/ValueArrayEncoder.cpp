#include "ValueArrayEncoder.h"

bool ValueArrayEncoder::Encode(DataReader& in, int bitsPerVal, size_t valAmount, DataWriter& out) {
	ASSERT(valAmount * bitsPerVal <= in.GetNumBitsLeft());
	ASSERT(bitsPerVal > 0 && bitsPerVal <= MAX_BITS_PER_VAL);

	int fullBytesPerVal = bitsPerVal / 8;
	int extraBitsPerVal = bitsPerVal % 8; // Bits that aren't filling a full byte

	// Full bytes of each value
	byte* fullBytesOut;
	if (fullBytesPerVal > 0) {
		fullBytesOut = (byte*)malloc(valAmount * fullBytesPerVal);
	} else {
		fullBytesOut = NULL;
	}

	DataWriter extraBitsOut;

	for (size_t i = 0; i < valAmount; i++) {

		if (fullBytesPerVal) {
			byte bytesBuf[MAX_BITS_PER_VAL / 8];
			in.ReadBytes(bytesBuf, fullBytesPerVal);
			
			// Write each byte of the every full value in its own concurrent memory
			// This should lead to better compression!
			for (size_t j = 0; j < fullBytesPerVal; j++)
				fullBytesOut[i + (j*valAmount)] = bytesBuf[j];
		}

		if (extraBitsPerVal) { // Write extra bits of each value onto the end
			byte extraBits = in.ReadBits<byte>(extraBitsPerVal);
			extraBitsOut.WriteBits(extraBits, extraBitsPerVal);
		}
	}

	if (in.overflowed) {
		if (fullBytesOut)
			free(fullBytesOut);
		return false;
	} else {
		DataWriter encodedDataOut;
		if (fullBytesPerVal > 0)
			encodedDataOut.WriteBytes(fullBytesOut, valAmount * fullBytesPerVal);

		encodedDataOut.Append(extraBitsOut); // AAA
		if (!encodedDataOut.Compress()) // AAA
			return false; // Failed to compress // AAA

		if (fullBytesOut)
			free(fullBytesOut);

		out.AlignToByte();
		out.Append(encodedDataOut);

		return true;
	}
}

bool ValueArrayEncoder::Decode(DataReader& in, int bitsPerVal, size_t valAmount, void* dataOut) {
	ASSERT(bitsPerVal > 0 && bitsPerVal <= MAX_BITS_PER_VAL);

	int fullBytesPerVal = bitsPerVal / 8;
	int extraBitsPerVal = bitsPerVal % 8; // Bits that aren't filling a full byte

	in.AlignToByte();

	vector<byte> decompressedData = in.Decompress();

	if (decompressedData.empty())
		return false; // Failed to decompress

	if ((decompressedData.size() * 8) < (valAmount * bitsPerVal))
		return false; // Not enough data decompressed

	DataReader extraBitsIn = DataReader(decompressedData);
	extraBitsIn.curByteIndex = fullBytesPerVal * valAmount;

	DataWriter decodedDataOut;

	for (size_t i = 0; i < valAmount; i++) {
		byte bytesBuf[MAX_BITS_PER_VAL / 8];

		if (fullBytesPerVal) {
			// Read concurrent full bytes
			for (size_t j = 0; j < fullBytesPerVal; j++)
				bytesBuf[j] = decompressedData[i + (j * valAmount)];

		}

		// Read extra bits
		if (extraBitsPerVal)
			bytesBuf[fullBytesPerVal] = extraBitsIn.ReadBits<byte>(extraBitsPerVal);

		// Write the value we just combined
		decodedDataOut.WriteBits(bytesBuf, bitsPerVal);
	}

	decodedDataOut.WriteToMemory(dataOut);
}
