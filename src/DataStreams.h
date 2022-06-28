#pragma once
#include "Framework.h"

#include <zlib.h>

// For reading data from file bytes
struct DataReader {
	const byte* data;
	size_t dataSize;
	size_t curByteIndex = 0;

	// Progress through current byte
	uint8_t curBitOffset = 0;

	DataReader(const vector<byte>& data) {
		this->data = data.empty() ? NULL : &data.front();
		this->dataSize = data.size();
	}

	DataReader(const void* data, size_t dataSize) {
		this->data = (const byte*)data;
		this->dataSize = dataSize;
	}

	bool IsValid() {
		return data != NULL;
	}

	// We reached the end by trying to read more than was available
	bool overflowed = false;

	bool IsDone() {
		return curByteIndex >= dataSize;
	}

	size_t GetNumBytesRead() {
		return curByteIndex;
	}

	size_t GetNumBitsRead() {
		return GetNumBytesRead() + curBitOffset;
	}

	size_t AmountBitsLeft() {
		return IsDone() ? 0 : (dataSize * 8 - GetNumBitsRead());
	}

	size_t AmountBytesLeft() {
		return AmountBitsLeft() / 8;
	}

	bool ReadBit() {
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

	template <typename T>
	T ReadBits(size_t bitCount) {
		byte result[sizeof(T)] = {};
		ASSERT(bitCount <= (sizeof(T) * 8));

		// Amount of full bytes to read
		size_t numFullBytes = bitCount / 8;

		if (numFullBytes)
			ReadBytes(&result, numFullBytes); // Write read bytes, if there are any

		size_t extraBits = bitCount - (numFullBytes * 8);
		if (extraBits) { // There are still bits left

			// TODO: This is inefficient
			for (int i = 0; i < extraBits; i++) {
				result[numFullBytes] |= (ReadBit() << i);
			}
		}

		return *(T*)&result;
	}

	bool ReadBytes(void* output, size_t amount) {
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
					byte p2 = (b2 << (8-curBitOffset)); // First bits of b2
					outputBytes[i] = p1 | p2;
				}
			}

			curByteIndex += amount;
			return true;
		}
	}

	template<typename T>
	T Read(T defaultReturn = T{}) {
		T result = defaultReturn;
		ReadBytes(&result, sizeof(T));
		return result;
	}

	vector<byte> Decompress() {
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
};

// For writing data to file bytes
struct DataWriter {
	// Bit progress in current byte
	uint8 curBitOffset = 0; 

	// In-progress byte
	byte curByteBuf = 0;

	vector<byte> resultBytes;

	DataWriter() {
	}

	DataWriter(vector<byte> initialBytes) {
		resultBytes = initialBytes;
	}

	void WriteBit(bool val) {
		curByteBuf |= (val << curBitOffset);
		curBitOffset++;
		
		if (curBitOffset == 8) {
			// We've reached the end, reset
			resultBytes.push_back(curByteBuf);
			curBitOffset = 0;
			curByteBuf = 0;
		}
	}

	template <typename T>
	void WriteBits(const T& data, size_t bitCount) {

		ASSERT(bitCount <= (sizeof(T) * 8));

		// Amount of full bytes to write
		size_t numFullBytes = bitCount / 8;

		if (numFullBytes)
			WriteBytes(&data, numFullBytes); // Write full bytes, if there are any

		size_t extraBits = bitCount - (numFullBytes * 8);
		if (extraBits) { // There are still bits left
			
			// Get last bits to write
			byte lastByteBits = *((byte*)&data + numFullBytes);

			// TODO: This is inefficient
			for (int i = 0; i < extraBits; i++) {
				bool val = (lastByteBits >> i) & 1;
				WriteBit(val);
			}
		}
	}

	void WriteBytes(const void* data, size_t amount) {
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

	template <typename T>
	void Write(const T& data) {
		WriteBytes(&data, sizeof(T));
	}

	size_t GetByteSize() const {
		return resultBytes.size() + (curBitOffset ? 1 : 0);
	}

	size_t GetBitSize() const {
		return (resultBytes.size() * 8) + curBitOffset;
	}

	bool Compress() {
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

	bool WriteToFile(string path) {
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
};