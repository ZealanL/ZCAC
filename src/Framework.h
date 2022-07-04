#pragma once
#pragma region STD Includes
#define _HAS_STD_BYTE false
#define _USE_MATH_DEFINES
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cassert>
#include <string>
#include <complex>
#include <functional>
#include <vector>
#include <stack>
#include <queue>
#include <map>
#include <array>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <list>
#include <iomanip>
#include <chrono>
#include <math.h>

// Remove need for std namespace scope for very common datatypes
using std::vector;
using std::map;
using std::unordered_map;
using std::set;
using std::multiset;
using std::unordered_set;
using std::list;
using std::stack;
using std::deque;
using std::string;
using std::wstring;
using std::pair;
#pragma endregion

// Integer typedefs
typedef int8_t	int8;	typedef uint8_t	 uint8;
typedef int16_t int16;	typedef uint16_t uint16;
typedef int32_t int32;	typedef uint32_t uint32;
typedef int64_t int64;	typedef uint64_t uint64;
typedef uint8_t byte;

// Debug assertion
#define SASSERT static_assert
#define ASSERT assert
#define IASSERT(e, s) ASSERT(e >= 0 && e < s) // Index assert 

// Quick max/min/clamp logic macros
#define MAX(a, b) ((a > b) ? a : b)
#define MIN(a, b) ((a < b) ? a : b)
#define MAX3(a, b, c) MAX(MAX(a, b), c)
#define MIN3(a, b, c) MIN(MIN(a, b), c)
#define CLAMP(v, min, max) MAX(min, MIN(v, max))

// Printing to console
#define LOG(s) std::cout << std::dec << s << std::endl

#define ERROR_EXIT(s) { LOG("ERROR: " << s); ASSERT(false); exit(EXIT_FAILURE); }

// Debug-only printing to console
#ifdef _DEBUG
#define DLOG(s) LOG("[DBG]: " << s)
#else
#define DLOG(s)
#endif

// Framework functions
namespace FW {
	// Returns the minimum number of bits needed to store an integer
	template <typename T>
	byte MinBitsNeeded(T val) {
		byte bitCount = 1;
		for (; val > 1; bitCount++)
			val >>= 1;
		return bitCount;
	}
}