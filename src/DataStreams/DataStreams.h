#pragma once
#include "../Framework.h"

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
		return GetNumBytesRead() * 8 + curBitOffset;
	}

	size_t GetNumBitsLeft() {
		return IsDone() ? 0 : (dataSize * 8 - GetNumBitsRead());
	}

	size_t GetNumBytesLeft() {
		return GetNumBitsLeft() / 8;
	}

	bool ReadBit();

	template <typename T>
	T ReadBits(size_t bitCount) {
		ASSERT(bitCount > 0);
		ASSERT(bitCount <= (sizeof(T) * 8));

		byte result[sizeof(T)] = {};
	
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

	bool ReadBytes(void* output, size_t amount);

	// Align cursor to next byte index with no bit offset if reading between bytes
	void AlignToByte();

	template<typename T>
	T Read(T defaultReturn = T{}) {
		T result = defaultReturn;
		ReadBytes(&result, sizeof(T));
		return result;
	}

	vector<byte> Decompress();
};

// For writing data to file bytes
struct DataWriter {
	// Bit progress in current byte
	uint8 curBitOffset = 0; 

	// In-progress byte
	byte curByteBuf = 0;

	vector<byte> resultBytes;

	DataWriter() = default;

	DataWriter(vector<byte> initialBytes) {
		resultBytes = initialBytes;
	}

	void Append(const DataWriter& other) {
		if (!other.resultBytes.empty())
			WriteBytes(&other.resultBytes.front(), other.resultBytes.size());

		if (other.curBitOffset)
			WriteBits(other.curByteBuf, other.curBitOffset);
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
		ASSERT(bitCount > 0);
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

	void WriteBytes(const void* data, size_t amount);

	template <typename T>
	void Write(const T& data) {
		WriteBytes(&data, sizeof(T));
	}

	// Includes possible partially-written byte
	size_t GetByteSize() const {
		return resultBytes.size() + (curBitOffset ? 1 : 0);
	}

	size_t GetBitSize() const {
		return (resultBytes.size() * 8) + curBitOffset;
	}

	// Will just return 0 if index is out-of-bounds
	bool GetBitAt(size_t bitIndex);

	// If current byte is partially complete, pads with 0 bits until the next byte
	void AlignToByte();

	bool Compress();

	bool WriteToFile(string path);
};