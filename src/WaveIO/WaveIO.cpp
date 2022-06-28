#include "WaveIO.h"

#include "../DataStreams.h"

// Backwards string magic IDs as uint32s
constexpr uint32 RIFF_ID = 'FFIR', WAVE_ID = 'EVAW', FMT_ID = ' tmf', DATA_ID = 'atad';

bool WaveIO::ReadWave(DataReader& r, WaveIO::AudioInfo& infoOut) {
	// http://soundfile.sapp.org/doc/WaveFormat/
	uint32 riffID = r.Read<uint32>();
	uint32 riffSize = r.Read<uint32>(); // Includes subchunk
	uint32 waveFormatID = r.Read<uint32>();

	uint32 fmtID = r.Read<uint32>();

	char* s1 = (char*)&riffID, * s2 = (char*)&waveFormatID, *s3 = (char*)&fmtID;

	if (riffID != RIFF_ID || waveFormatID != WAVE_ID || fmtID != FMT_ID)
		ERROR_EXIT("Wave file has invalid initial header IDs");
	

	uint32 fmtChunkSize = r.Read<uint32>();
	WaveFormatType baseFormatType = r.Read<WaveFormatType>();
	auto actualFormatType = baseFormatType; // May be overwritten later

	bool hasSubFormat = baseFormatType != WaveFormatType::PCM;
	bool isExtensible = baseFormatType == WaveFormatType::EXTENSIBLE;

	if (fmtChunkSize < 16 || !hasSubFormat && fmtChunkSize > 16)
		ERROR_EXIT("Wave file has invalid format chunk size");

	uint16 channelCount = r.Read<uint16>();

	if (!channelCount || channelCount > 0xFF)
		ERROR_EXIT("Wave file has invalid channel count (" << channelCount << " channels)");

	infoOut.freq = r.Read<uint32>();
	uint32 byteRate = r.Read<uint32>();
	uint16 blockAlign = r.Read<uint16>();
	auto bytesPerSample = r.Read<uint16>() / 8;

	if (infoOut.freq == 0 || bytesPerSample > 8 || blockAlign != (bytesPerSample * channelCount))
		ERROR_EXIT("Wave file has invalid audio rate data");

	if (hasSubFormat) {
		// Extensible ref: https://docs.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible

		uint16 extraParamsSize = r.Read<uint16>();
		size_t backupByteIndex = r.curByteIndex;

		// Extensible must have at least 0x16 (22) bytes of extra params
		if (isExtensible && extraParamsSize < 0x16)
			ERROR_EXIT("Wave file with extensible format has invalid extra parameters size (" << extraParamsSize << ")");
		
		if (extraParamsSize > 0x1000)
			ERROR_EXIT("Wave file with extensible format has too much parameters data (" << 0x1000 << " bytes)");

		if (isExtensible) {
			uint16 miscInt16 = r.Read<uint16>();
			uint32 channelMask = r.Read<uint32>();

			uint32 guidPrefix = r.Read<uint32>();

			actualFormatType = (WaveFormatType)guidPrefix;
		}
		
		// Go to end
		r.curByteIndex = backupByteIndex + extraParamsSize;
	}

	// Actual audio data
	uint32 dataID = r.Read<uint32>();

	if (dataID != DATA_ID)
		ERROR_EXIT("Wave file has invalid data header ID");

	uint32 dataAmount = r.Read<uint32>();

	if (dataAmount % bytesPerSample)
		ERROR_EXIT("Wave file data amount is not aligned with the sample size");

	infoOut.channelData.resize(channelCount);

	// Show debug data
	DLOG("Base format type: " << (int)baseFormatType);
	DLOG("Actual format type: " << (int)actualFormatType);
	DLOG("Bytes per sample: " << bytesPerSample);
	DLOG("Data amount: 0x" << std::hex << dataAmount << " bytes");
	DLOG("Channel count: " << channelCount);

	for (
		size_t audioDataAmount = 0; 
		!r.IsDone() && audioDataAmount < dataAmount; 
		audioDataAmount += bytesPerSample) {
		size_t sampleIndex = audioDataAmount / bytesPerSample;

		byte dataVal[8];
		r.ReadBytes(&dataVal, bytesPerSample); // bytesPerSample was already verified to be <= 8, so this is safe

		const void* data = r.data + r.curByteIndex;

		float resultVal;
		if (actualFormatType == WaveFormatType::PCM) {
			// Simple linear value, scale down to our range
			// TODO: Don't use switch case for this, this isn't needed
			switch (bytesPerSample) {
			case 1:
				resultVal = *(int8*)dataVal / (float)INT8_MAX;
				break;
			case 2:
				resultVal = *(int16*)dataVal / (float)INT16_MAX;
				break;
			case 4:
				resultVal = *(int32*)dataVal / (float)INT32_MAX;
				break;
			case 8:
				resultVal = *(int64*)dataVal / (float)INT64_MAX;
				break;
			default:
				ERROR_EXIT("Wave file uses unsupported " << (bytesPerSample * 8) << " bits per sample");
			}
		} else if (actualFormatType == WaveFormatType::FLOAT) {
			// Floating point, just clamp
			resultVal = CLAMP(*(float*)&dataVal, -1, 1);
		} else {
			ERROR_EXIT("Wave file audio format type #" << (int)actualFormatType << " is currently not supported.");
		}

		infoOut.channelData[sampleIndex % channelCount].push_back(resultVal);

		if (r.overflowed)
			ERROR_EXIT("Wave file overflowed while trying to read audio data ")
	}

	infoOut.sampleCount = infoOut.channelData.front().size();

	return true;
}

vector<byte> WaveIO::WriteWave(const WaveIO::AudioInfo& audioInfo) {

	size_t totalAudioDataSize = audioInfo.sampleCount * audioInfo.channelData.size() * 4;
	size_t totalFileSize = 44 + totalAudioDataSize;

	uint16 channelCount = audioInfo.channelData.size();

	DataWriter w = DataWriter();

	{ // RIFF chunk
		w.Write<uint32>(RIFF_ID);
		w.Write<uint32>(totalFileSize - w.GetByteSize() - 4); // Write remaining size
		w.Write<uint32>(WAVE_ID);
	}

	{ // Format sub-chunk
		w.Write<uint32>(FMT_ID);
		w.Write<uint32>(16); // Always 16 for PCM
		w.Write(WaveFormatType::PCM);
		w.Write<uint16>(channelCount); // Channel count
		w.Write<uint32>(audioInfo.freq);
		w.Write<uint32>(audioInfo.freq * channelCount * 4); // Byte rate
		w.Write<uint16>(channelCount * 4); // Block align
		w.Write<uint16>(32); // Bits per sample
	}

	{ // Data sub-chunk
		w.Write(DATA_ID);
		w.Write<uint32>(totalAudioDataSize); // Write audio data size

		// Write actual sound data
		for (size_t i = 0; i < audioInfo.sampleCount; i++) {
			for (auto& channel : audioInfo.channelData) {
				float baseVal = channel[i];

				//float randDeviation = sinf((rand() % 1000) / 98.f) 
					//* 0.1;
				//baseVal *= 1 + randDeviation;

				baseVal = CLAMP(baseVal, -1, 1);

				w.Write<int32>(baseVal * INT32_MAX);
			}
		}
	}

	ASSERT(w.GetByteSize() == totalFileSize);
	return w.resultBytes;
}