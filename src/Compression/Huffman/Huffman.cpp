#include "Huffman.h"

#ifdef _DEBUG
void PrintNodeRecursive(Huffman::Tree::Node* curNode, string curBuildStr = "", int level = 0) {

	std::stringstream lineStream;

	bool isRoot = level == 0;

	// Add indent
	if (curNode->HasChildren()) {
		for (int i = 0; i < level; i++)
			lineStream << " | ";
	} else {
		for (int i = 0; i < level - 1; i++)
			lineStream << " | ";

		lineStream << " +-";
	}

	if (curNode->HasChildren()) {
		lineStream << '[';

		DLOG(lineStream.str() << (isRoot ? "ROOT": "B ") << curBuildStr << ']');
		level++;
		PrintNodeRecursive(curNode->left, curBuildStr + '0',	level);
		PrintNodeRecursive(curNode->right, curBuildStr + '1',	level);
	} else {
		lineStream << '<';
		DLOG(lineStream.str() << curBuildStr << "> = " << curNode->data);
	}
}

void Huffman::Tree::DebugPrint() {
	DLOG("Huffman::Tree (size = " << _freqMap.size() << "):");
	if (_freqMap.empty()) {
		DLOG("  ~ Empty ~");
	} else {
		PrintNodeRecursive(root);
	}
	DLOG("====================");
}

size_t Huffman::Tree::GetEncodedBitSize() {
	if (!root)
		return 0;

	size_t result = 0;
	for (auto& pair : _freqMap)
		result += encodingMap[pair.first].bitLength * pair.second;

	return result;
}
#endif

void Huffman::Tree::BuildMapRecursive(Node* curNode, EncodedValBits curBits) {
	if (curNode->HasChildren()) {
		// This is a tree node
		Huffman::EncodedValBits leftVal = curBits, rightVal = curBits;
		leftVal.AddBit(0);
		rightVal.AddBit(1);

		BuildMapRecursive(curNode->left, leftVal);
		BuildMapRecursive(curNode->right, rightVal);
	} else {
		ASSERT(curBits.bitLength > 0);

		// Value node, add to map
		encodingMap[curNode->data] = curBits;
	}
}

Huffman::Tree::Tree(const FrequencyMap& freqMap) {
	FreeNodeRecursive(root);
	SetFreqMap(freqMap);

	// Just a single value? still write a 0
	if (root && !root->HasChildren())
		encodingMap[root->data].AddBit(0);
}

bool Huffman::Tree::SetFreqMap(const FrequencyMap& freqMap) {
	if (freqMap.empty())
		return false;

	this->_freqMap = freqMap;

	// For efficient sorting and retrieval during tree construction
	typedef std::priority_queue<Node*, vector<Node*>, std::function<bool(Node* a, Node* b)>> NodeHeapQueue;
	NodeHeapQueue heap = NodeHeapQueue(Node::CompareFreqByPtr);

	// Create all nodes
	for (auto& pair : freqMap)
		heap.push(new Node{ pair.first, pair.second });

	while (heap.size() > 1) {
		// Get next 2 best nodes
		Node* left = heap.top();
		heap.pop();

		Node* right = heap.top();
		heap.pop();

		// Make internal parent node (value doesn't matter)
		heap.push(new Node{ 0, left->freq + right->freq, left, right });
	}

	// Set root
	root = heap.top();

	// Build map
	BuildMapRecursive(root, EncodedValBits());
	return true;
}

void Huffman::EncodedValBits::AddBit(bool val) {
	if (bitLength == HUFFMAN_VAL_MAX_BITS) {
		ASSERT(false);
	} else {
		byte& b = data[bitLength / 8];
		byte bitNum = bitLength % 8;

		if (bitNum == 0)
			b = 0; // Set byte to 0 when first used

		b |= (val << bitNum);

		bitLength++;
	}
}

Huffman::Val Huffman::Tree::ReadEncodedVal(DataReader& reader) {
	ASSERT(root);
	Node* curNode = root;

	while (curNode->HasChildren())
		curNode = reader.ReadBit() ? curNode->right : curNode->left;

	return curNode->data;
}

void Huffman::Tree::SerializeFreqMap(const FrequencyMap& freqMap, DataWriter& writer) {
	bool use32BitNums = freqMap.size() > UINT16_MAX;

	if (!use32BitNums) {
		// Make sure no frequency counts are higher than UINT16_MAX, otherwise we will need to use 32 bit numbers
		for (auto& pair : freqMap) {
			if (pair.second > UINT16_MAX) {
				use32BitNums = true;
				break;
			}
		}
	}

	writer.WriteBit(use32BitNums);

	int numBitAmount = use32BitNums ? 32 : 16;

	// Write amount of data entries
	writer.WriteBits(freqMap.size(), numBitAmount);

	Val highestVal = 0;
	for (auto& pair : freqMap)
		highestVal = MAX(highestVal, pair.first);

	// Maximum number of bits for any value in the freq map
	int maxValBits = FW::MinBitsNeeded(highestVal);
	writer.Write<byte>(maxValBits);

	for (auto& pair : freqMap) {
		writer.WriteBits(pair.first, maxValBits); // Val
		writer.WriteBits(pair.second, numBitAmount); // Number of occurences
	}
}

bool Huffman::Tree::DeserializeFreqMap(FrequencyMap& freqMapOut, DataReader& reader) {
	freqMapOut.clear();

	bool use32BitIndexing = reader.ReadBit();
	int numBitAmount = use32BitIndexing ? 32 : 16;

	// Read number of data entries
	size_t entryAmount = reader.ReadBits<size_t>(numBitAmount);

	// Maximum number of bits for any value in the freq map
	int maxValBits = reader.Read<byte>();

	for (int i = 0; i < entryAmount; i++) {
		Val val = reader.ReadBits<Val>(maxValBits);
		uint32 occurrenceCount = reader.ReadBits<uint32>(numBitAmount);

		ASSERT(occurrenceCount > 0);

		if (freqMapOut.count(val))
			return false; // Duplicate entry

		freqMapOut[val] = occurrenceCount;

		if (reader.overflowed)
			return false; // Ran out of room
	}
	return true;
}
