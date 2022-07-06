#include "ValueArrayEncoder.h"

#include "../Huffman/Huffman.h"

bool ValueArrayEncoder::Encode(DataReader& in, int bitsPerVal, size_t valAmount, DataWriter& out) {
	ASSERT(valAmount * bitsPerVal <= in.GetNumBitsLeft());
	ASSERT(bitsPerVal > 0 && bitsPerVal <= MAX_BITS_PER_VAL);

	Huffman::Tree::FrequencyMap valFreqMap;
	ScopeMem<Huffman::Val> vals = ScopeMem<Huffman::Val>(valAmount);
	for (int i = 0; i < valAmount; i++) {
		vals[i] = in.ReadBits<Huffman::Val>(bitsPerVal);
		valFreqMap[vals[i]]++;
	}

	DataWriter encodedWriter;
	Huffman::Tree::SerializeFreqMap(valFreqMap, encodedWriter);
	Huffman::Tree tree = Huffman::Tree(valFreqMap);

	for (size_t i = 0; i < valAmount; i++) {
		Huffman::Val curVal = vals[i];

		auto& encodingBits = tree.encodingMap[curVal];

		encodedWriter.WriteBits(encodingBits, encodingBits.bitLength);
	}
	
#ifdef _DEBUG
	size_t totalOccurenceAccount = 0;
	for (auto& pair : valFreqMap)
		totalOccurenceAccount += pair.second;

	DLOG("Original size: " << (bitsPerVal * valAmount));
	DLOG("Huffman occurence size: " << encodedWriter.GetBitSize());
	DLOG("Compression ratio: " << (100.f * encodedWriter.GetBitSize() / (bitsPerVal * valAmount)) << "%");
	DLOG("Average occurences per pair: " << (totalOccurenceAccount / (float)valFreqMap.size()));
#endif

	if (in.overflowed) {
		return false;
	} else {
		out.AlignToByte();
		out.Append(encodedWriter);
		return true;
	}
}

bool ValueArrayEncoder::Decode(DataReader& in, int bitsPerVal, size_t valAmount, void* dataOut) {
	ASSERT(bitsPerVal > 0 && bitsPerVal <= MAX_BITS_PER_VAL);

	in.AlignToByte();

	DataWriter decodedDataOut;

	Huffman::Tree::FrequencyMap valFreqMap;
	DataWriter encodedWriter;
	if (!Huffman::Tree::DeserializeFreqMap(valFreqMap, in))
		return false; // Failed to deserialize frequency map

	Huffman::Tree tree = Huffman::Tree(valFreqMap);

	for (int i = 0; i < valAmount; i++) {
		Huffman::Val val = tree.ReadEncodedVal(in);
		decodedDataOut.WriteBits(val, bitsPerVal);
	}

	decodedDataOut.WriteToMemory(dataOut);
}
