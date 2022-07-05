#pragma once
#include "../../Framework.h"

namespace ZCAC {

	// Redeclaration
	typedef uint32 Flags;

	struct Config {
		// Overall quality from 1-10
		enum Quality {
			MIN = 1,
			WORST = MIN,

			BAD = 3,

			MEDIUM = 5,

			HIGH = 7,

			MAX = 10,
			BEST = MAX,

			DEFAULT = MEDIUM
		} quality = Quality::DEFAULT;
		
		// Removes frequencies that are too quiet (relative to simultanious frequencies) to be heard
		bool omitUnimportantFreqs = true;

		bool zlibCompress = true;

		uint32 GetFlags();
	};
}