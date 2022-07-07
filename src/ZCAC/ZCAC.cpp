#include "ZCAC.h"
#include "../Compression/Huffman/Huffman.h"
#include "../Compression/BitRepeater/BitRepeater.h"
#include "Config/Config.h"
#include "../Compression/ValueArrayEncoder/ValueArrayEncoder.h"

ZCAC::FFTBlock ZCAC::FFTBlock::FromAudioData(const float* audioData) {

	FFTBlock result;

	// Update max amplitude
	Math::Complex fftBuffer[ZCAC_FFT_SIZE];
	for (int i = 0; i < ZCAC_FFT_SIZE; i++) {
		fftBuffer[i] = { audioData[i], 0 };

		result.maxAmplitude = MAX(result.maxAmplitude, abs(audioData[i]));
	}	

	// Make sure max amplitude isn't too low
	result.maxAmplitude = MAX(result.maxAmplitude, 0.01);

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

float ZCAC::FFTBlock::GetZeroVolF() {
	return -rangeMin / (rangeMax - rangeMin);
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

bool ZCAC::Encode(const WaveIO::AudioInfo& waveAudioInfo, DataWriter& out, Config config) {
	// Make header
	ZCAC_Header header;
	header.freq = waveAudioInfo.freq;
	header.numChannels = waveAudioInfo.channelData.size();
	header.samplesPerChannel = waveAudioInfo.sampleCount;

	Flags flags = config.GetFlags();
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

		size_t blockAmount = blocks.size();

		// Write block amount
		out.Write<uint32>(blockAmount);

		// Write block ranges
		for (auto& block : blocks) {
			out.Write<float>(block.rangeMin);
			out.Write<float>(block.rangeMax);
		}

		size_t TOTAL_VAL_AMOUNT = ZCAC_FFT_SIZE_STORAGE * blockAmount * 2;

		// If we are omitting FFT vals, this will store whether or not each FFT value is omitted
		// Order is part/block/slot because this produces long sets of repeated bits, which makes them easier to compress
		ScopeMem<bool> omitValLookup;

		// Make FFT val omission lookup table 
		if (flags & FLAG_OMIT_FFT_VALS) {
			// Create lookup table
			size_t totalValsOmitted = 0;
			omitValLookup.Alloc(TOTAL_VAL_AMOUNT);

			// Division of STDV a value must be within to be skipped
			float stdvDiv = 4 + (config.quality * 1.7f);

			// part/block/slot
			for (int iPart = 0, totalLookupIndex = 0; iPart < 2; iPart++) {
				for (int iBlock = 0; iBlock < blockAmount; iBlock++) {
					FFTBlock& block = blocks[iBlock];
					float
						base = block.GetZeroVolF(),
						stdv = block.GetStandardDeviationF();

					for (int iSlot = 0; iSlot < ZCAC_FFT_SIZE_STORAGE; iSlot++, totalLookupIndex++) {
						ComplexInts& curComplexInts = blocks[iBlock].data[iSlot];
						float valF = blocks[iBlock].data[iSlot][iPart] / (float)ZCAC_INT_VAL_MAX;
						float dev = abs(valF - base);

						bool shouldSkip = dev < (stdv / stdvDiv);

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

		// Write FFT block values
		// part/block/slot
		DataWriter fftData;
		size_t totalValsWritten = 0;
		for (int iPart = 0, totalLookupIndex = 0; iPart < 2; iPart++) {
			for (int iBlock = 0; iBlock < blockAmount; iBlock++) {
				for (int iSlot = 0; iSlot < ZCAC_FFT_SIZE_STORAGE; iSlot++, totalLookupIndex++) {
					if (flags & FLAG_OMIT_FFT_VALS)
						if (omitValLookup[totalLookupIndex])
							continue;

					uint16 val = blocks[iBlock].data[iSlot][iPart];
					ASSERT(val <= ZCAC_INT_VAL_MAX);
					fftData.WriteBits(val, ZCAC_INT_VAL_BITS);
					totalValsWritten++;
				}
			}
		}

		fftData.AlignToByte();

		{ // Compress via ValueArrayEncoder
			DataReader tempReader = DataReader(fftData.resultBytes);
			size_t sizeBefore = out.GetByteSize();

			if (!ValueArrayEncoder::Encode(tempReader, ZCAC_INT_VAL_BITS, totalValsWritten, out)) {
				return false; // Failed to compress-encode FFT vals
			} else {
				size_t sizeAfter = out.GetByteSize();

				size_t encodedFFTValsSize = sizeAfter - sizeBefore;
				DLOG("Encode-compressed FFT vals to " << (100.f * encodedFFTValsSize / fftData.GetByteSize()) << "% original size");
			}
		}
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
			DLOG("Decompressed from " << in.GetNumBytesLeft() << " bytes to " << decompressedBytes.size()
				<< " bytes (->" << (int)(100.f * decompressedBytes.size() / in.GetNumBytesLeft()) << "%)");
			in = DataReader(decompressedBytes);
		} else {
			DLOG("Failed to decompress, proceeding anyway.");
		}
	}

	for (int channelIndex = 0; channelIndex < header.numChannels; channelIndex++) {
		uint32 blockAmount = in.Read<uint32>();
		vector<FFTBlock> blocks;
		for (int j = 0; j < blockAmount; j++) {
			FFTBlock curBlock;
			// Read range
			curBlock.rangeMin = in.Read<float>();
			curBlock.rangeMax = in.Read<float>();

			blocks.push_back(curBlock);
		}

		size_t TOTAL_VAL_AMOUNT = ZCAC_FFT_SIZE_STORAGE * blocks.size() * 2;

		size_t totalValsToRead = TOTAL_VAL_AMOUNT;

		ScopeMem<bool> omitValLookup = NULL;
		if (header.flags & FLAG_OMIT_FFT_VALS) {
			// Deserialize omitted vals list
			omitValLookup.Alloc(TOTAL_VAL_AMOUNT);

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

			for (int i = 0; i < TOTAL_VAL_AMOUNT; i++)
				if (omitValLookup[i])
					totalValsToRead--;
		}

		size_t deltaValsAllocSize = (totalValsToRead * ZCAC_INT_VAL_BITS) / 8 + 1;
		ScopeMem deltaVals = ScopeMem(deltaValsAllocSize);
		if (!ValueArrayEncoder::Decode(in, ZCAC_INT_VAL_BITS, totalValsToRead, deltaVals)) {
			return false; // Failed to decode-decompress FFT vals
		}

		DataReader deltaValsReader = DataReader(deltaVals, deltaValsAllocSize);

		// Read vals
		// part/block/slot
		for (int iPart = 0, totalIndex = 0; iPart < 2; iPart++) {
			for (int iBlock = 0; iBlock < blockAmount; iBlock++) {
				for (int iSlot = 0; iSlot < ZCAC_FFT_SIZE_STORAGE; iSlot++, totalIndex++) {
					if (header.flags & FLAG_OMIT_FFT_VALS) {
						if (omitValLookup[totalIndex]) {
							// Make value empty
							blocks[iBlock].data[iSlot][iPart] = blocks[iBlock].GetZeroVolF() * ZCAC_INT_VAL_MAX;
							continue;
						}

					}

					uint16 val = deltaValsReader.ReadBits<uint16>(ZCAC_INT_VAL_BITS);
					blocks[iBlock].data[iSlot][iPart] = val;
				}
			}
		}

		if (!header.samplesPerChannel || header.samplesPerChannel > (blockAmount * ZCAC_FFT_SIZE))
			return false; // Invalid samples per channel

		// Write to channel
		ScopeMem<float> audioDataOutBuffer = ScopeMem<float>(header.samplesPerChannel + ZCAC_FFT_SIZE);
		audioDataOutBuffer.MakeZero();

		for (int i = 0; i < blockAmount; i++) {
			float blockAudioOut[ZCAC_FFT_SIZE];
			blocks[i].ToAudioData(blockAudioOut); // AAA

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

		audioInfoOut.channelData.push_back(vector<float>(audioDataOutBuffer.data, audioDataOutBuffer + header.samplesPerChannel));
	}

	return true;
}