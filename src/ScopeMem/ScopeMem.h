#pragma once

#include "../Framework.h"

// Basic class for allocating bytes in a scope
template<typename T = byte>
struct ScopeMem {
	T* data;
	size_t size;

	ScopeMem() {
		data = NULL;
		size = 0;
	}

	ScopeMem(size_t size) {
		this->size = size;
		data = (T*)malloc(size * sizeof(T));
		ASSERT(data);
	}

	void Alloc(size_t size) {
		if (data)
			free(data);
		this->size = size;
		data = (T*)malloc(size * sizeof(T));
		ASSERT(data);
	}

	void MakeZero() {
		if (data)
			memset(data, 0, size * sizeof(T));
	}

	~ScopeMem() {
		free(data);
	}

	// No copy/move constructor
	ScopeMem(const ScopeMem& other) = delete;
	ScopeMem(ScopeMem&& other) = delete;

	operator T*() {
		return data;
	}
};