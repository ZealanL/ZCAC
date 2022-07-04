#include "BitRepeater.h"
#include "../Huffman/Huffman.h"

struct BitSequence {
	bool val;
	size_t length;
};

#define LENGTH_BITCOUNT_MIN 1
#define LENGTH_BITCOUNT_MAX 31
#define LENGTH_BITCOUNT_STEP 3
#define MAX_SEQ_LENGTH (1 << LENGTH_BITCOUNT_MAX)

// Returns false if length is too large
bool WriteLength(uint32 length, DataWriter& writerOut) {
	if (length > MAX_SEQ_LENGTH)
		return false;

	// We will write one less
	length -= 1;

	int minBitsNeeded = FW::MinBitsNeeded(length);

	int bitCount = LENGTH_BITCOUNT_MIN;
	while (bitCount < minBitsNeeded) {
		bitCount += LENGTH_BITCOUNT_STEP;
		writerOut.WriteBit(1);
	}

	writerOut.WriteBit(0);

	writerOut.WriteBits(length, bitCount);

	return true;
}

// Returns -1 if invalid
size_t ReadLength(DataReader& in) {
	int bitCount = LENGTH_BITCOUNT_MIN;
	while (in.ReadBit()) {
		bitCount += LENGTH_BITCOUNT_STEP;
		if (bitCount > LENGTH_BITCOUNT_MAX)
			return -1;
	}
	
	return in.ReadBits<size_t>(bitCount) + 1;
}

bool BitRepeater::Encode(DataWriter& writer) {
	vector<BitSequence> seqs;
	size_t bitCount = writer.GetBitSize();
	size_t bitCountForFullBytes = writer.resultBytes.size() * 8;

	for (int i = 0; i < bitCount; i++) {
		bool bitVal = writer.GetBitAt(i);

		if (seqs.empty() || seqs.back().val != bitVal) {
			seqs.push_back({ bitVal, 1 });
		} else {
			seqs.back().length++;
			if (seqs.back().length == MAX_SEQ_LENGTH) {
				ASSERT(false);
				return false;
			}
		}
	}

	if (seqs.empty())
		return false;

	DataWriter encodedWriter;

	// Write sequence count
	encodedWriter.Write<uint32>(seqs.size());

	// Number of bits we will need to raw-encode each sequence
	size_t seqsEncodeBitCount = 0;

	// Make freq map for potential tree
	Huffman::Tree::FrequencyMap huffMap;
	for (BitSequence& seq : seqs)
		huffMap[seq.length]++;
	
	// TODO: This is just a vague guess of if a huffman tree would be more efficient
	bool useHuffTree = huffMap.size() < (seqs.size() / 4);

	encodedWriter.WriteBit(useHuffTree);

	Huffman::Tree tree;
	if (useHuffTree) {
		tree.SetFreqMap(huffMap);
		Huffman::Tree::SerializeFreqMap(huffMap, encodedWriter);
	}
	
	// Write starting bit
	if (!seqs.empty())
		encodedWriter.WriteBit(seqs.front().val);
	
	if (useHuffTree) {
		for (BitSequence& seq : seqs) {
			Huffman::EncodedValBits valBits = tree.encodingMap[seq.length];
			encodedWriter.WriteBits(valBits.data, valBits.bitLength);
		}
	} else {
		for (BitSequence& seq : seqs)
			WriteLength(seq.length, encodedWriter);
	}

	if (encodedWriter.GetBitSize() > writer.GetBitSize()) {
		return false;
	} else {
		writer = encodedWriter;
		return true;
	}
}

bool BitRepeater::Decode(DataReader& in, DataWriter& out) {
	uint32 seqCount = in.Read<uint32>();

	if (in.overflowed)
		return false;

	if (seqCount == 0)
		return true;

	bool huffmanEncodedLengths = in.ReadBit();

	Huffman::Tree::FrequencyMap huffMap;
	Huffman::Tree tree;
	if (huffmanEncodedLengths) {
		if (!Huffman::Tree::DeserializeFreqMap(huffMap, in))
			return false;

		tree.SetFreqMap(huffMap);
	}

	// Read starting bit
	bool curBit = in.ReadBit();

	for (size_t i = 0; i < seqCount; i++) {
		size_t seqLength;
		if (huffmanEncodedLengths) {
			seqLength = tree.ReadEncodedVal(in);
		} else {
			seqLength = ReadLength(in);

			if (seqLength == -1)
				return false;
		}

		if (in.overflowed)
			return false;

		size_t j = 0;

		// Write in 16-bit blocks for efficiency
		for (; j + 16 <= seqLength; j += 16)
			out.Write<uint16>(curBit ? 0xFFFF : 0);

		for (; j < seqLength; j++)
			out.WriteBit(curBit);

		curBit = !curBit;
	}

	return true;
}