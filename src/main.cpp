#include "Framework.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include "WaveIO/WaveIO.h"
#include "Math/Math.h"
#include "ZCAC/ZCAC.h"
#include "DataStreams.h"

vector<byte> FileToBytes(string path) {
	vector<byte> out;

	std::ifstream inFile = std::ifstream(path, std::ios::binary);
	inFile >> std::noskipws; // Don't skip whitespaces!
	if (!inFile.good())
		return out;

	// Determine length
	inFile.seekg(0, std::ios::end);
	size_t fileLength = inFile.tellg();
	inFile.seekg(0, std::ios::beg);

	if (fileLength == 0)
		return out;

	out.resize(fileLength);
	inFile.read((char*)&out.front(), fileLength);
	return out;
}

int main(int argc, char* argv[]) {
	string filePath;
	if (argc > 1) {
		filePath = argv[1];
	} else {
#ifdef _WIN32
		// Prompt to open file
		char pathBuffer[MAX_PATH]{};
		OPENFILENAMEA ofn{};
		ofn.lStructSize = sizeof(ofn);

		ofn.lpstrFilter = "WAVE Audio Files (*.wav)\0*.wav\0";
		ofn.lpstrFile = pathBuffer;
		ofn.nMaxFile = MAX_PATH;

		ofn.lpstrTitle = "Choose a .wav file";
		ofn.Flags = OFN_FILEMUSTEXIST;

		if (!GetOpenFileNameA(&ofn))
			exit(EXIT_FAILURE); // User didn't select a file

		filePath = pathBuffer;
#else
		LOG("Usage: program <path to .wav>")
#endif
	}

	vector<byte> wavFileBytes = FileToBytes(filePath);
	DataReader inWavFile = DataReader(wavFileBytes);
	if (!inWavFile.IsValid())
		ERROR_EXIT("Cannot open file \"" << filePath << "\"");
	
	if (inWavFile.dataSize == 0)
		ERROR_EXIT("File is empty");

	WaveIO::AudioInfo audioInfo;
	if (!WaveIO::ReadWave(inWavFile, audioInfo))
		ERROR_EXIT("Failed to parse invalid wave file");

	LOG("Loaded WAVE file with " << audioInfo.channelData.size() << " channels at " << audioInfo.freq << "hz");

	{ // Show duration
		int seconds = audioInfo.channelData.front().size() / audioInfo.freq;
		LOG(" > Total duration: " << (seconds / 60) << ":" << std::setw(2) << std::setfill('0') << (seconds % 60));
	}

	string outEncodedPath = "test_encoded.zcac";
	string outDecodedPath = "test_decoded.wav";

	{ // Encode to file
		DataWriter testOutZCAC;
		if (ZCAC::Encode(audioInfo, testOutZCAC)) {
			testOutZCAC.WriteToFile(outEncodedPath);
			LOG("Encoded successfully! Writing to \"" << outEncodedPath << "\"...");
		} else {
			ERROR_EXIT("Failed to encode!");
		}
	}

	{ // Decode from file
		vector<byte> zcacFileBytes = FileToBytes(outEncodedPath);
		DataReader testInZCAC = DataReader(zcacFileBytes);

		WaveIO::AudioInfo audioInfoIn;
		if (ZCAC::Decode(testInZCAC, audioInfoIn)) {
			LOG("Decoded successfully! Writing to \"" << outDecodedPath << "\"...");
			DataWriter outWave = WaveIO::WriteWave(audioInfoIn);
			outWave.WriteToFile(outDecodedPath);
		} else {
			ERROR_EXIT("Failed to decode!");
		}
	}

	LOG("Done!");
}