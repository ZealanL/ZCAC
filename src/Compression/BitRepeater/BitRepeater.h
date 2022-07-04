#include "../../DataStreams/DataStreams.h"

// BitRepeater is a simple "algorithm" I made purely for the purpose of encoding repeating bits (e.x. 111111, 00000)
namespace BitRepeater {
	// Returns false if encoded version would take up more size (won't modify writer in this case)
	bool Encode(DataWriter& writer);

	// Returns false if decode failed
	bool Decode(DataReader& in, DataWriter& out);
}