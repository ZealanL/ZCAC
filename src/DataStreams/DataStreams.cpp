#include "DataStreams.h"

#include <zlib.h>

bool DataReader::ReadBit() {
	if (IsDone()) {
		// No bits left
		overflowed = true;
		return false;
	}

	bool result = (data[curByteIndex] >> curBitOffset) & 1;

	curBitOffset++;
	if (curBitOffset == 8) {
		curBitOffset = 0;
		curByteIndex++;
	}

	return result;
}

bool DataReader::ReadBytes(void* output, size_t amount) {
	ASSERT(amount > 0);

	if (amount > AmountBytesLeft()) {
		// Not enough data left
		overflowed = true;
		curByteIndex = dataSize;
		return false;
	} else {
		if (curBitOffset == 0) {
			// No bit offset, just copy directly
			memcpy(output, (data + curByteIndex), amount);
		} else {
			// Read with bit offset
			for (int i = 0; i < amount; i++) {
				byte b1 = data[curByteIndex + i], b2 = data[curByteIndex + i + 1];

				byte* outputBytes = (byte*)output;
				byte p1 = (b1 >> curBitOffset); // Last bits of b1
				byte p2 = (b2 << (8 - curBitOffset)); // First bits of b2
				outputBytes[i] = p1 | p2;
			}
		}

		curByteIndex += amount;
		return true;
	}
}

vector<byte> DataReader::Decompress() {
	ASSERT(curBitOffset == 0);
	curBitOffset = 0;

	size_t backupCurByteIndex = curByteIndex;

	uLong decompressedSize = Read<uint32>();
	byte* decompressedBuffer = (byte*)malloc(decompressedSize);
	if (!decompressedBuffer) {
		curByteIndex = backupCurByteIndex;
		return vector<byte>();
	}

	int result = uncompress(decompressedBuffer, &decompressedSize, data + curByteIndex, AmountBytesLeft());

	curByteIndex = backupCurByteIndex;

	if (result != Z_OK) {
		ASSERT(false);
		free(decompressedBuffer);
		return vector<byte>();
	} else {
		auto result = vector<byte>(decompressedBuffer, decompressedBuffer + decompressedSize);
		free(decompressedBuffer);
		return result;
	}
}

void DataWriter::WriteBytes(const void* data, size_t amount) {
	if (!curBitOffset) {
		// No current bit offset, just append bytes
		resultBytes.insert(resultBytes.end(), (const byte*)data, (const byte*)data + amount);
	} else {
		// Write each byte with our bit offset
		for (int i = 0; i < amount; i++) {
			// Get current byte from data
			byte b = ((byte*)data)[i];

			// Fill remainder of current byte with left bits of data
			curByteBuf |= (b << curBitOffset);

			// Finish curByte 
			resultBytes.push_back(curByteBuf);
			curByteBuf = 0;

			// Fill begining of current byte with right bits of data
			curByteBuf = (b >> (8 - curBitOffset));
		}
	}
}

bool DataWriter::Compress() {
	size_t compressedMaxSize = compressBound(resultBytes.size());
	byte* compressedBytes = (byte*)malloc(compressedMaxSize);

	uLong compressedSize;
	int result = compress2(compressedBytes, &compressedSize, &resultBytes.front(), resultBytes.size(), Z_BEST_COMPRESSION);

	if (result != Z_OK) {
		ASSERT(false);
		free(compressedBytes);
		return false;
	}

	uint32 originalSize = resultBytes.size();
	resultBytes.clear();
	curBitOffset = 0;
	Write<uint32>(originalSize);
	WriteBytes(compressedBytes, compressedSize);

	free(compressedBytes);

	curBitOffset = 0;
	return true;
}

bool DataWriter::WriteToFile(string path) {
	std::ofstream outFile = std::ofstream(path, std::ios::binary);
	if (!outFile.good())
		return false;

	if (GetBitSize() >= 8) { // We have full bytes to write
		outFile.write((char*)&resultBytes.front(), GetByteSize());
	}

	if (curBitOffset) {
		// Write extra bits
		outFile.write((char*)&curByteBuf, 1);
	}

	return true;
}