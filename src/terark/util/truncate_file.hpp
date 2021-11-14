#pragma once

#include <terark/config.hpp>

namespace terark {
	TERARK_DLL_EXPORT
	void truncate_file(const char* fpath, unsigned long long size);

	template<class String>
	inline
	void truncate_file(const String& fpath, unsigned long long size) {
		assert(fpath.data()[fpath.size()] == '\0');
		truncate_file(fpath.data(), size);
	}
}
