#pragma once
#include "../Framework.h"

#include "../DataStreams/DataStreams.h"

// For reading/writing .wav (WAVE) files
// References:
//	http://soundfile.sapp.org/doc/WaveFormat/
//	http://www-mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html
namespace WaveIO {
	// Wave baseformat enums
	// Ref: http://soundfile.sapp.org/doc/WaveFormat/
	enum class WaveFormatType : uint16 {
		PCM = 1,
		FLOAT = 3, // 32 bit float
		ALAW = 6, // 8 bit ITU-T G.711 A-law
		MULAW = 7, // 8 bit ITU-T G.711 µ-law

		EXTENSIBLE = 0xFFFE
	};

	struct AudioInfo {
		uint32 freq;
		uint32 sampleCount;

		// Data vector for each channel, in the format of simple scalar floats (from -1 to 1)
		vector<vector<float>> channelData;
	};

	// Returns false if the wave file failed to be read
	bool ReadWave(DataReader& reader, WaveIO::AudioInfo& audioInfoOut);

	// Will be written as a PCM-32
	vector<byte> WriteWave(const WaveIO::AudioInfo& audioInfo);
}