#include "ZCAC.h"
#include "../Compression/Huffman/Huffman.h"
#include "../Compression/BitRepeater/BitRepeater.h"

ZCAC::FFTBlock ZCAC::FFTBlock::FromAudioData(const float* audioData) {

	FFTBlock result;

	Math::Complex fftBuffer[ZCAC_FFT_SIZE];
	for (int i = 0; i < ZCAC_FFT_SIZE; i++)
		fftBuffer[i] = { audioData[i], 0 };

	Math::FastFourierTransform(fftBuffer, ZCAC_FFT_SIZE);

	// Update ranges
	for (Math::Complex& complex : fftBuffer) {
		result.rangeMin = MIN(result.rangeMin, MIN(complex.real(), complex.imag()));
		result.rangeMax = MAX(result.rangeMax, MAX(complex.real(), complex.imag()));
	}

	// Store
	for (int i = 0; i < ZCAC_FFT_SIZE_STORAGE; i++) {
		Math::Complex& c = fftBuffer[i];
		float rangeScale = (result.rangeMax - result.rangeMin);
		float real = (c.real() - result.rangeMin) / rangeScale;
		float imag = (c.imag() - result.rangeMin) / rangeScale;

		result.data[i] = ComplexInts(Math::Complex(real, imag));
	}

	return result;
}

void ZCAC::FFTBlock::ToAudioData(float* audioDataOut) {
	Math::Complex fftBuffer[ZCAC_FFT_SIZE];

	for (int i = 0; i < ZCAC_FFT_SIZE_STORAGE; i++) {

		Math::Complex c = data[i].ToComplex();
		float rangeScale = (rangeMax - rangeMin);
		float real = (c.real() * rangeScale) + rangeMin;
		float imag = (c.imag() * rangeScale) + rangeMin;

		fftBuffer[i] = Math::Complex(real, imag);

		// Set mirrored
		if (i > 0)
			fftBuffer[ZCAC_FFT_SIZE - i] = std::conj(fftBuffer[i]);
	}

	// Flip imaginary values before feeding back in
	for (Math::Complex& complex : fftBuffer)
		complex = std::conj(complex);

	Math::FastFourierTransform(fftBuffer, ZCAC_FFT_SIZE);

	for (int i = 0; i < ZCAC_FFT_SIZE; i++) {
		float magnitude = abs(fftBuffer[i]) / ZCAC_FFT_SIZE;
		audioDataOut[i] = (fftBuffer[i].real() > 0) ? magnitude : -magnitude;
	}
}

float ZCAC::FFTBlock::GetAverageF() {
	float total = 0;
	for (ComplexInts& complexInts : data) {
		auto complex = complexInts.ToComplex();
		for (int i = 0; i < 2; i++)
			total += complex._Val[i];
	}
	return total / (ZCAC_FFT_SIZE_STORAGE * 2);
}

float ZCAC::FFTBlock::GetStandardDeviationF() {
	float avg = GetAverageF();
	float sqDeltaSum = 0;
	for (ComplexInts& complexInts : data) {
		auto complex = complexInts.ToComplex();
		for (int i = 0; i < 2; i++) {
			float delta = complex._Val[i] - avg;
			sqDeltaSum += delta * delta;
		}
	}

	return sqrtf(sqDeltaSum / (ZCAC_FFT_SIZE_STORAGE * 2));
}

////////////////////////////////

#pragma pack(push, 1) // No alignment padding for this struct
struct ZCAC_Header {
	const uint32 magic = ZCAC_MAGIC;

	uint32 versionNum = ZCAC_VERSION_NUM;
	byte numChannels;
	uint32 freq;
	uint64 samplesPerChannel;

	ZCAC::Flags flags;
};
#pragma pack(pop)

bool ZCAC::Encode(const WaveIO::AudioInfo& waveAudioInfo, DataWriter& out, ZCAC::Flags flags) {
	// Make header
	ZCAC_Header header;
	header.freq = waveAudioInfo.freq;
	header.numChannels = waveAudioInfo.channelData.size();
	header.samplesPerChannel = waveAudioInfo.sampleCount;
	header.flags = flags;

	for (auto& channel : waveAudioInfo.channelData) {
		// Make blocks
		vector<FFTBlock> blocks;
		for (int i = 0; i < channel.size(); i += ZCAC_FFT_SIZE - ZCAC_FFT_PAD) {
			if (i + ZCAC_FFT_SIZE <= channel.size()) {
				// Within range, simply copy over
				blocks.push_back(ZCAC::FFTBlock::FromAudioData(&channel[i]));
			} else {
				// Padding needed
				float paddedData[ZCAC_FFT_SIZE] = {}; // Will pad to zero
				memcpy(paddedData, &channel[i], (channel.size() - i - 1) * sizeof(float));
				blocks.push_back(ZCAC::FFTBlock::FromAudioData(paddedData));
			}
		}
		// Write block amount
		out.Write<uint32>(blocks.size());

		// Write block ranges
		for (auto& block : blocks) {
			out.Write<float>(block.rangeMin);
			out.Write<float>(block.rangeMax);
		}

		size_t TOTAL_VAL_AMOUNT = ZCAC_FFT_SIZE_STORAGE * blocks.size() * 2;

		// Create block deltas - these will store the deltas of each FFT slot from one block to the next
		// Order is part/slot/block, because FFT values of a specific slot tend to change semi-consistently from one block to another
		auto blockDeltas = new uint16[TOTAL_VAL_AMOUNT];

		// If we are omitting FFT vals, this will store whether or not each FFT value is omitted
		// Order is part/block/slot because this produces long sets of repeated bits, which makes them easier to compress
		bool* omitValLookup = NULL;

		// Make FFT val omission lookup table 
		if (flags & FLAG_OMIT_FFT_VALS) {
			// Create lookup table
			size_t totalValsOmitted = 0;
			omitValLookup = new bool[TOTAL_VAL_AMOUNT];

			// part/block/slot
			for (int i = 0, totalLookupIndex = 0; i < 2; i++) {
				for (int iBlock = 0; iBlock < blocks.size(); iBlock++) {
					FFTBlock& block = blocks[iBlock];
					float
						avg = block.GetAverageF(),
						stdv = block.GetStandardDeviationF();

					for (int iSlot = 0; iSlot < ZCAC_FFT_SIZE_STORAGE; iSlot++, totalLookupIndex++) {
						ComplexInts& curComplexInts = blocks[iBlock].data[iSlot];
						uint16 val = i ? curComplexInts.imag : curComplexInts.real;
						float valF = val / (float)ZCAC_INT_VAL_MAX;
						float dev = abs(valF - avg);

						bool shouldSkip = (dev < (stdv / 5)); // TODO: Don't use hardcoded std portion

						omitValLookup[totalLookupIndex] = shouldSkip;

						if (shouldSkip)
							totalValsOmitted++;
					}
				}
			}

			DLOG("FFT values omitted: " << totalValsOmitted << " (" << (100.f * totalValsOmitted / TOTAL_VAL_AMOUNT) << "%)");

			// Write lookup table
			DataWriter lookupTableData;

			// TODO: Inefficient
			for (int i = 0; i < TOTAL_VAL_AMOUNT; i++)
				lookupTableData.WriteBit(omitValLookup[i]);

			if (BitRepeater::Encode(lookupTableData)) {
				DLOG("Compressed FFT value omission lookup table down to " << (100.f * lookupTableData.GetBitSize() / TOTAL_VAL_AMOUNT) << "%");
				out.WriteBit(1); // Mark compressed
			} else {
				DLOG("Not compressing FFT value omission table (inefficient)");
				out.WriteBit(0); // Mark uncompressed
			}
			out.Append(lookupTableData);
		}

		// Make huffman freq map of all fft complex delta vals
		Huffman::Tree::FrequencyMap fftDeltaFreqMap;

		// Build block deltas (and also delta freq map)
		// part/block/slot
		uint16 lastVals[ZCAC_FFT_SIZE_STORAGE] = {};
		for (int iPart = 0, totalLookupIndex = 0; iPart < 2; iPart++) {
			for (int iBlock = 0; iBlock < blocks.size(); iBlock++) {
				for (int iSlot = 0; iSlot < ZCAC_FFT_SIZE_STORAGE; iSlot++, totalLookupIndex++) {

					// part/slot/block
					size_t blockDeltaIndex =
						(iPart * (TOTAL_VAL_AMOUNT / 2)) + (iSlot * blocks.size()) + iBlock;

					ComplexInts& curComplexInts = blocks[iBlock].data[iSlot];

					uint16 val = iPart ? curComplexInts.imag : curComplexInts.real;

					ASSERT(val <= ZCAC_INT_VAL_MAX);

					if (flags & FLAG_OMIT_FFT_VALS)
						if (omitValLookup[totalLookupIndex])
							continue;

					uint16 delta = (val - lastVals[iSlot]) & ZCAC_INT_VAL_MAX;

					blockDeltas[blockDeltaIndex] = delta;

					fftDeltaFreqMap[delta]++;

					lastVals[iSlot] = val;
				}
			}
		}

		Huffman::Tree huffTree = Huffman::Tree(fftDeltaFreqMap);

		size_t totalHuffBits = 0;
		for (auto& pair : huffTree.encodingMap)
			totalHuffBits += fftDeltaFreqMap[pair.first] * pair.second.bitLength;

		DataWriter freqMapWriter;
		Huffman::Tree::SerializeFreqMap(fftDeltaFreqMap, freqMapWriter);

		float huffCompressionRatio = (totalHuffBits + freqMapWriter.GetBitSize()) / (float)(TOTAL_VAL_AMOUNT * ZCAC_INT_VAL_BITS);

		DLOG("Huffman compression ratio: " << huffCompressionRatio);

		bool encodeHuffman = huffCompressionRatio < 1;

		if (encodeHuffman) {
			DLOG(" > Encoding via Huffman...");
			out.WriteBit(1);

			out.Append(freqMapWriter);
		} else {
			DLOG(" > NOT using Huffman, encoding raw...");
			out.WriteBit(0);
		}

		size_t valsWritten = 0;

		// part/slot/block
		for (int iPart = 0, totalIndex = 0; iPart < 2; iPart++) {
			for (int iSlot = 0; iSlot < ZCAC_FFT_SIZE_STORAGE; iSlot++) {
				for (int iBlock = 0; iBlock < blocks.size(); iBlock++, totalIndex++) {

					if (flags & FLAG_OMIT_FFT_VALS) {
						// part/block/slot
						size_t omitLookupIndex =
							(iPart * (TOTAL_VAL_AMOUNT / 2)) + (iBlock * ZCAC_FFT_SIZE_STORAGE) + iSlot;
						if (omitValLookup[omitLookupIndex])
							continue;
					}

					if (encodeHuffman) {
						Huffman::EncodedValBits& huffBits = huffTree.encodingMap[blockDeltas[totalIndex]];
						out.WriteBits(huffBits.data, huffBits.bitLength);
					} else {
						out.WriteBits(blockDeltas[totalIndex], ZCAC_INT_VAL_BITS);
					}

					valsWritten++;
				}
			}
		}

		DLOG("Wrote a total of " << valsWritten << " FFT vals.");

		delete[] blockDeltas;

		if (flags & FLAG_OMIT_FFT_VALS)
			delete[] omitValLookup;
	}

	if (flags & FLAG_ZLIB_COMPRESSION) {
		DLOG("Compressing with ZLIB...");
		// Compress
		if (!out.Compress())
			return false; // Failed to compress
	}

	// Place header at beginning
	out.resultBytes.insert(out.resultBytes.begin(), (byte*)&header, (byte*)&header + sizeof(header));

	return true;
}

bool ZCAC::Decode(DataReader in, WaveIO::AudioInfo& audioInfoOut) {
	// Read header
	ZCAC_Header header = in.Read<ZCAC_Header>();

	if (header.magic != ZCAC_MAGIC)
		return false; // Missing magic

	if (header.versionNum != ZCAC_VERSION_NUM)
		return false; // Wrong version

	audioInfoOut.freq = header.freq;
	audioInfoOut.sampleCount = header.samplesPerChannel;

	vector<byte> decompressedBytes;
	if (header.flags & FLAG_ZLIB_COMPRESSION) {
		// Attempt to decompress
		decompressedBytes = in.Decompress();
		if (!decompressedBytes.empty()) {
			DLOG("Decompressed from " << in.AmountBytesLeft() << " bytes to " << decompressedBytes.size()
				<< " bytes (->" << (int)(100.f * decompressedBytes.size() / in.AmountBytesLeft()) << "%)");
			in = DataReader(decompressedBytes);
		} else {
			DLOG("Failed to decompress, proceeding anyway.");
		}
	}

	for (int channelIndex = 0; channelIndex < header.numChannels; channelIndex++) {
		uint32 blockAmount = in.Read<uint32>();
		vector<FFTBlock> blocks; // Not reserving space incase blockAmount is invalid/corrupt
		for (int j = 0; j < blockAmount; j++) {
			FFTBlock curBlock;

			// Read range
			curBlock.rangeMin = in.Read<float>();
			curBlock.rangeMax = in.Read<float>();

			blocks.push_back(curBlock);
		}

		size_t TOTAL_VAL_AMOUNT = ZCAC_FFT_SIZE_STORAGE * blocks.size() * 2;

		bool* omitValLookup = NULL;
		if (header.flags & FLAG_OMIT_FFT_VALS) {
			// Deserialize omitted vals list
			omitValLookup = new bool[TOTAL_VAL_AMOUNT];

			bool bitRepeatCompressed = in.ReadBit();
			if (bitRepeatCompressed) {
				DataWriter decompressed;
				if (!BitRepeater::Decode(in, decompressed))
					return false; // Failed to decompress FFT omissions

				if (decompressed.GetBitSize() != TOTAL_VAL_AMOUNT)
					return false; // FFT omissions are of the wrong size 

				// TODO: Inefficient
				for (int i = 0; i < TOTAL_VAL_AMOUNT; i++)
					omitValLookup[i] = decompressed.GetBitAt(i);

			} else {
				// TODO: Inefficient
				for (int i = 0; i < TOTAL_VAL_AMOUNT; i++)
					omitValLookup[i] = in.ReadBit();
			}
		}

		bool huffmanEncodedVals = in.ReadBit();

		Huffman::Tree::FrequencyMap huffFreqMap;
		Huffman::Tree huffTree;
		if (huffmanEncodedVals) {
			if (!Huffman::Tree::DeserializeFreqMap(huffFreqMap, in))
				return false; // Failed to read huffman frequency map

			huffTree.SetFreqMap(huffFreqMap);
		}

		size_t valsRead = 0;
		uint16 lastVals[ZCAC_FFT_SIZE_STORAGE] = {};
		// part/slot/block
		for (int iPart = 0; iPart < 2; iPart++) {
			for (int iSlot = 0; iSlot < ZCAC_FFT_SIZE_STORAGE; iSlot++) {
				for (int iBlock = 0; iBlock < blocks.size(); iBlock++) {

					ComplexInts& curComplexInts = blocks[iBlock].data[iSlot];

					if (header.flags & FLAG_OMIT_FFT_VALS) {
						// part/block/slot
						size_t omitValIndex =
							(iPart * (TOTAL_VAL_AMOUNT / 2)) + (iBlock * ZCAC_FFT_SIZE_STORAGE) + iSlot;

						if (omitValLookup[omitValIndex]) {
							// This value is skipped
							float emptyValF = -blocks[iBlock].rangeMin / (blocks[iBlock].rangeMax - blocks[iBlock].rangeMin);
							(iPart ? curComplexInts.imag : curComplexInts.real) = emptyValF * ZCAC_INT_VAL_MAX;
							continue;
						}
					}

					uint16 deltaVal;
					if (huffmanEncodedVals) {
						deltaVal = huffTree.ReadEncodedVal(in);
					} else {
						deltaVal = in.ReadBits<uint16>(ZCAC_INT_VAL_BITS);
					}

					lastVals[iSlot] = (lastVals[iSlot] + deltaVal) & ZCAC_INT_VAL_MAX;

					(iPart ? curComplexInts.imag : curComplexInts.real) = lastVals[iSlot];
					valsRead++;
				}

				if (in.overflowed)
					return false; // Overflowed while trying to read
			}
		}

		// We should be done reading now
		ASSERT(in.curByteIndex == (in.dataSize - 1));

		if (!header.samplesPerChannel || header.samplesPerChannel > (blocks.size() * ZCAC_FFT_SIZE))
			return false; // Invalid samples per channel

		// Write to channel
		float* audioDataOutBuffer = new float[header.samplesPerChannel + ZCAC_FFT_SIZE];
		for (int i = 0; i < blocks.size(); i++) {
			float blockAudioOut[ZCAC_FFT_SIZE];
			blocks[i].ToAudioData(blockAudioOut);

			int realOutputIndex = i * (ZCAC_FFT_SIZE - ZCAC_FFT_PAD);

			if (i > 0) {
				// Blend with last
				for (int j = 0; j < ZCAC_FFT_PAD; j++) {
					float ratio = j / (float)ZCAC_FFT_PAD;

					float ours = blockAudioOut[j];
					float theirs = audioDataOutBuffer[realOutputIndex + j];

					float interp = (ours * ratio) + (theirs * (1.f - ratio));
					blockAudioOut[j] = interp;
				}
			}
			memcpy(&audioDataOutBuffer[realOutputIndex], blockAudioOut, ZCAC_FFT_SIZE * sizeof(float));
		}

		audioInfoOut.channelData.push_back(vector<float>(audioDataOutBuffer, audioDataOutBuffer + header.samplesPerChannel));
		delete[] audioDataOutBuffer;
	}

	return true;
}