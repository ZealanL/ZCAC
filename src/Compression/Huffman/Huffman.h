#pragma once
#include "../../Framework.h"

#include "../../DataStreams/DataStreams.h"

// Ref:
//	https://www.geeksforgeeks.org/huffman-coding-greedy-algo-3/
//	https://iq.opengenus.org/huffman-encoding/
//	https://www.youtube.com/watch?v=dM6us854Jk0

namespace Huffman {

	// Encoded value type
	typedef uint32 Val;

	// The bits for a huffman value
#define HUFFMAN_VAL_MAX_BITS 256
	struct EncodedValBits {
		byte data[HUFFMAN_VAL_MAX_BITS / 8];
		size_t bitLength = 0;

		void AddBit(bool val);

		void Fart() const {}
	};

	class Tree {
	public:
		struct Node {
			Val data;
			uint32 freq;

			Node* left = NULL, * right = NULL;

			bool HasChildren() {
				// We should never have just one child node
				ASSERT((left == NULL) == (right == NULL));

				return left != NULL;
			}

			static bool CompareFreqByPtr(Node* a, Node* b) {
				if (a->freq != b->freq) {
					return a->freq > b->freq;
				} else {
					return a->data > b->data; // Resolve ambiguities
				}
			}
		};

		typedef map<Val, uint32> FrequencyMap;
		Tree(const FrequencyMap& freqMap); // Build the tree

		bool SetFreqMap(const Huffman::Tree::FrequencyMap& freqMap);

		Tree() = default;

		// No copy constructor
		Tree(const Tree& other) = delete;

		// No move constructor
		Tree(Tree&& other) = delete;

		Val ReadEncodedVal(DataReader& reader);

		static void SerializeFreqMap(const FrequencyMap& freqMap, DataWriter& writer);
		static bool DeserializeFreqMap(FrequencyMap& freqMapOut, DataReader& reader);

		//////

		typedef unordered_map<Val, EncodedValBits> HuffmanMap;
		HuffmanMap encodingMap;

		Node* root = NULL;
		
#ifdef _DEBUG
		void DebugPrint();
#endif

		// Size, in bits, if we were to encode this tree
		size_t GetEncodedBitSize();

		~Tree() {
			FreeNodeRecursive(root);
			root = NULL;
		}

	private:
		void BuildMapRecursive(Node* curNode, EncodedValBits curBits);

		FrequencyMap _freqMap;

		void FreeNodeRecursive(Node* node) {
			
			if (!node)
				return;

			FreeNodeRecursive(node->left);
			FreeNodeRecursive(node->right);

			delete node;
		}
	};
}