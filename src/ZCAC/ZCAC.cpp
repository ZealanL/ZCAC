#include "ZCAC.h"

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

		result.data[i] = ZCAC::ComplexInts(Math::Complex(real, imag));
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

#pragma pack(push, 1) // No alignment padding for this struct
struct ZCAC_Header {
	uint32 versionNum = ZCAC_VERSION_NUM;
	byte numChannels;
	uint32 freq;
	uint64 samplesPerChannel;
};
#pragma pack(pop)

bool ZCAC::Encode(const WaveIO::AudioInfo& waveAudioInfo, DataWriter& out) {
	// Write header
	ZCAC_Header header;
	header.freq = waveAudioInfo.freq;
	header.numChannels = waveAudioInfo.channelData.size();
	header.samplesPerChannel = waveAudioInfo.sampleCount;
	out.Write(header);

	for (auto& channel : waveAudioInfo.channelData) {
		// Make blocks
		vector<FFTBlock> fftBlocks;
		for (int i = 0; i < channel.size(); i += ZCAC_FFT_SIZE - ZCAC_FFT_PAD) {
			if (i + ZCAC_FFT_SIZE <= channel.size()) {
				// Within range, simply copy over
				fftBlocks.push_back(ZCAC::FFTBlock::FromAudioData(&channel[i]));
			} else {
				// Padding needed
				float paddedData[ZCAC_FFT_SIZE] = {}; // Will pad to zero
				memcpy(paddedData, &channel[i], (channel.size() - i - 1) * sizeof(float));
				fftBlocks.push_back(ZCAC::FFTBlock::FromAudioData(paddedData));
			}
		}

		// Write blocks
		out.Write<uint32>(fftBlocks.size());
		for (auto& block : fftBlocks) {
			// Write range
			out.Write<float>(block.rangeMin);
			out.Write<float>(block.rangeMax);

			// Write FFT data
			for (ComplexInts& complexInts : block.data) {
				out.WriteBits(complexInts.real, ZCAC_INT_VAL_BITS);
				out.WriteBits(complexInts.imag, ZCAC_INT_VAL_BITS);
			}
		}
	}

	// Compress
	if (!out.Compress())
		return false; // Failed to compress

	// Place magic at beginning
	uint32 magic = ZCAC_MAGIC;
	out.resultBytes.insert(out.resultBytes.begin(), (byte*)&magic, (byte*)&magic + sizeof(uint32));

	return true;
}

bool ZCAC::Decode(DataReader in, WaveIO::AudioInfo& audioInfoOut) {
	// Get magic
	if (in.Read<uint32>() != ZCAC_MAGIC)
		return false; // Missing magic

	// Decompress
	vector<byte> decompressedBytes = in.Decompress();
	if (decompressedBytes.empty())
		return false; // Failed to decompress

	in = DataReader(decompressedBytes);

	// Read header
	ZCAC_Header header = in.Read<ZCAC_Header>();
	if (header.versionNum != ZCAC_VERSION_NUM)
		return false; // Wrong version

	audioInfoOut.freq = header.freq;
	audioInfoOut.sampleCount = header.samplesPerChannel;

	for (int i = 0; i < header.numChannels; i++) {
		uint32 blockAmount = in.Read<uint32>();
		vector<FFTBlock> blocks; // Not reserving space incase blockAmount is invalid/corrupt
		for (int j = 0; j < blockAmount; j++) {
			FFTBlock curBlock;

			// Read range
			curBlock.rangeMin = in.Read<float>();
			curBlock.rangeMax = in.Read<float>();

			// Read data
			for (ComplexInts& complexInts : curBlock.data) {
				complexInts.real = in.ReadBits<uint16>(ZCAC_INT_VAL_BITS);
				complexInts.imag = in.ReadBits<uint16>(ZCAC_INT_VAL_BITS);
				if (in.overflowed)
					break; //return false; // Overflowed while trying to read
			}

			blocks.push_back(curBlock);
		}

		if (!header.samplesPerChannel || header.samplesPerChannel > blocks.size() * ZCAC_FFT_SIZE)
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
