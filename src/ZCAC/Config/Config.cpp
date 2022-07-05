#include "Config.h"

#include "../ZCAC.h"

ZCAC::Flags ZCAC::Config::GetFlags() {
	Flags result = FLAG_NONE;

	if (omitUnimportantFreqs)
		result |= FLAG_OMIT_FFT_VALS;

	if (zlibCompress)
		result |= FLAG_ZLIB_COMPRESSION;

	return result;
}