/* vim: set tabstop=4 : */
#include "MemStream.hpp"
#include <stdlib.h>
#include <algorithm>
#include <stdexcept>
#include <typeinfo>
#include <errno.h>

#if defined(_MSC_VER)
# include <intrin.h>
#pragma intrinsic(_BitScanReverse)
//#pragma intrinsic(_BitScanReverse64)
#endif

#include <boost/predef/other/endian.h>
#include <terark/num_to_str.hpp>
#include <stdarg.h>
#include "var_int.hpp"

namespace terark {

//void MemIO_Base::skip(ptrdiff_t diff)

void throw_EndOfFile(const char* func, size_t want, size_t available)
{
	string_appender<> oss;
	oss << "in " << func << ", want=" << want
		<< ", available=" << available
//		<< ", tell=" << tell() << ", size=" << size()
		;
	throw EndOfFileException(oss.str().c_str());
}

void throw_OutOfSpace(const char* func, size_t want, size_t available)
{
	string_appender<> oss;
	oss << "in " << func << ", want=" << want
		<< ", available=" << available
//		<< ", tell=" << tell() << ", size=" << size()
		;
	throw OutOfSpaceException(oss.str().c_str());
}

void MemIO::throw_EndOfFile(const char* func, size_t want)
{
	terark::throw_EndOfFile(func, want, remain());
}

void MemIO::throw_OutOfSpace(const char* func, size_t want)
{
	terark::throw_OutOfSpace(func, want, remain());
}

//////////////////////////////////////////////////////////////////////////

void SeekableMemIO::seek(ptrdiff_t newPos)
{
	assert(newPos >= 0);
	if (newPos < 0 || newPos > m_end - m_beg) {
		string_appender<> oss;
		size_t curr_size = m_end - m_beg;
		oss << "in " << BOOST_CURRENT_FUNCTION
			<< "[newPos=" << newPos << ", size=" << curr_size << "]";
//		errno = EINVAL;
		throw std::invalid_argument(oss.str());
	}
	m_pos = m_beg + newPos;
}

void SeekableMemIO::seek(ptrdiff_t offset, int origin)
{
	size_t pos;
	switch (origin)
	{
		default:
		{
			string_appender<> oss;
			oss << "in " << BOOST_CURRENT_FUNCTION
				<< "[offset=" << offset << ", origin=" << origin << "(invalid)]";
		//	errno = EINVAL;
			throw std::invalid_argument(oss.str().c_str());
		}
		case 0: pos = (size_t)(0 + offset); break;
		case 1: pos = (size_t)(tell() + offset); break;
		case 2: pos = (size_t)(size() + offset); break;
	}
	seek(pos);
}

// rarely used methods....
//

std::pair<byte*, byte*> SeekableMemIO::range(size_t ibeg, size_t iend) const
{
	assert(ibeg <= iend);
	assert(ibeg <= size());
	assert(iend <= size());
	if (ibeg <= iend && ibeg <= size() && iend <= size())
	{
		return std::pair<byte*, byte*>(m_beg + ibeg, m_beg + iend);
	}
	string_appender<> oss;
	oss << BOOST_CURRENT_FUNCTION
		<< ": size=" << size()
		<< ", tell=" << tell()
		<< ", ibeg=" << ibeg
		<< ", iend=" << iend
		;
	throw std::invalid_argument(oss.str());
}

//////////////////////////////////////////////////////////////////////////

AutoGrownMemIO::AutoGrownMemIO(size_t size) noexcept
{
	if (size) {
		m_beg = (byte*)malloc(size);
		if (NULL == m_beg) {
			TERARK_DIE("size = %zd", size);
		}
		m_end = m_beg + size;
		m_pos = m_beg;
	}
	else
		m_pos = m_end = m_beg = NULL;
}

AutoGrownMemIO::~AutoGrownMemIO()
{
	if (m_beg)
		free(m_beg);
}

void AutoGrownMemIO::clone(const AutoGrownMemIO& src) noexcept
{
	AutoGrownMemIO t(src.size());
	memcpy(t.begin(), src.begin(), src.size());
	this->swap(t);
}

/**
 @brief 改变 buffer 尺寸

  不改变 buffer 中的已存内容，不改变 pos

 @note must m_pos <= request_cap
 */
void AutoGrownMemIO::reserve(size_t request_cap) noexcept
{
	TERARK_VERIFY_LE(tell(), request_cap);
	size_t oldcap = capacity();
	if (request_cap <= oldcap) {
		return;
	}
	size_t newcap = std::max(oldcap * 103/64, request_cap);
	byte* newbeg = (byte*)realloc(m_beg, newcap);
	if (newbeg) {
		m_pos = newbeg + (m_pos - m_beg);
		m_end = newbeg + newcap;
		m_beg = newbeg;
	}
	else {
		TERARK_DIE("AutoGrownMemIO::reserve(request_cap = %zd), oldcap = %zd, realloc(newcap = %zd) failed",
					request_cap, oldcap, newcap);
	}
}

void AutoGrownMemIO::grow_capacity(size_t nGrow) noexcept {
	size_t oldcap = m_end - m_beg;
	size_t request_cap = oldcap + nGrow;
	size_t newcap = std::max<size_t>(32, oldcap);
	while (newcap < request_cap) newcap *= 2;
	reserve(newcap);
}

/**
 @brief 释放原先的空间并重新分配

  相当于按新尺寸重新构造一个新 AutoGrownMemIO
  不需要把旧内容拷贝到新地址
 */
void AutoGrownMemIO::init(size_t newcap) noexcept
{
	size_t oldcap = (size_t)(m_beg - m_beg);
	if (m_beg)
		::free(m_beg);
	if (newcap) {
		m_beg = (byte*)::malloc(newcap);
		if (NULL == m_beg) {
			m_pos = m_end = NULL;
			TERARK_DIE("malloc failed in AutoGrownMemIO::init(newcap=%lu), oldcap=%lu"
				, (unsigned long)newcap
				, (unsigned long)oldcap
				);
		}
		m_pos = m_beg;
		m_end = m_beg + newcap;
	}
	else
		m_pos = m_end = m_beg = NULL;
}

void AutoGrownMemIO::growCapAndWrite(const void* data, size_t length) noexcept
{
	using namespace std;
	size_t nSize = size();
	size_t nGrow = max(length, nSize);
	reserve(max(nSize + nGrow, (size_t)64u));
	memcpy(m_pos, data, length);
	m_pos += length;
}

void AutoGrownMemIO::growCapAndWriteByte(byte b) noexcept
{
	using namespace std;
	reserve(max(2u * size(), (size_t)64u));
	*m_pos++ = b;
}

void AutoGrownMemIO::clear() noexcept {
	if (this->m_beg) {
		::free(this->m_beg);
		this->m_beg = NULL;
		this->m_end = NULL;
		this->m_pos = NULL;
	}
	else {
		assert(NULL == this->m_end);
		assert(NULL == this->m_pos);
	}
}

/**
 * shrink allocated memory to fit this->tell()
 */
void AutoGrownMemIO::shrink_to_fit() noexcept {
	if (NULL == m_beg) {
		assert(NULL == m_pos);
		assert(NULL == m_end);
	}
	else {
		assert(m_beg <= m_pos);
		assert(m_pos <= m_end);
		size_t realsize = m_pos - m_beg;
		if (0 == realsize) {
			::free(m_beg);
			m_beg = m_end = m_pos = NULL;
		}
		else {
			byte* newbeg = (byte*)realloc(m_beg, realsize);
			assert(NULL != newbeg);
			if (NULL == newbeg) {
				// realloc should always success on shrink
				abort();
			}
			m_end = m_pos = newbeg + realsize;
			m_beg = newbeg;
		}
	}
}

size_t AutoGrownMemIO::printf(const char* format, ...) noexcept
{
	va_list ap;
	size_t n;
	va_start(ap, format);
	n = this->vprintf(format, ap);
	va_end(ap);
	return n;
}

size_t AutoGrownMemIO::vprintf(const char* format, va_list ap) noexcept
{
	if (m_end - m_pos < 64) {
		this->reserve(std::max<size_t>(64, (m_end-m_beg)*2));
	}
	while (1) {
		ptrdiff_t n, size = m_end - m_pos;

	#if defined(va_copy)
		va_list ap_copy;
		va_copy(ap_copy, ap);
		n = ::vsnprintf((char*)m_pos, size, format, ap_copy);
		va_end(ap_copy);
	#else
		n = ::vsnprintf((char*)m_pos, size, format, ap);
	#endif

		/* If that worked, return the written bytes. */
		if (n > -1 && n < size) {
			m_pos += n;
			return n;
		}
		/* Else try again with more space. */
		if (n > -1)    /* glibc 2.1 */
			size = n+1; /* precisely what is needed */
		else           /* glibc 2.0 */
			size *= 2;  /* twice the old size */

		this->reserve((m_pos - m_beg + size) * 2);
	}
}

///////////////////////////////////////////////////////
//
#if defined(__GLIBC__) || defined(__CYGWIN__)

ssize_t
MemIO_FILE_read(void *cookie, char *buf, size_t size)
{
	MemIO* input = (MemIO*)cookie;
	return input->read(buf, size);
}

ssize_t
AutoGrownMemIO_FILE_write(void *cookie, const char *buf, size_t size)
{
	AutoGrownMemIO* output = (AutoGrownMemIO*)cookie;
	return output->write(buf, size);
}

#if defined(__CYGWIN__)
int AutoGrownMemIO_FILE_seek(void* cookie, _off64_t* offset, int whence)
#else
int AutoGrownMemIO_FILE_seek(void* cookie, off64_t* offset, int whence)
#endif
{
	AutoGrownMemIO* output = (AutoGrownMemIO*)cookie;
	try {
		output->seek(*offset, whence);
		*offset = output->tell();
		return 0;
	}
	catch (const std::exception& e) {
		errno = EINVAL;
		return -1;
	}
}

/**
 * @note must call fclose after use of returned FILE
 */
FILE* MemIO::forInputFILE()
{
	cookie_io_functions_t func = {
		MemIO_FILE_read,
		NULL,
		NULL,
		NULL
	};
	void* cookie = this;
	assert(cookie);
	FILE* fp = fopencookie(cookie,"r", func);
	if (fp == NULL) {
		perror("fopencookie@MemIO::getInputFILE");
		return NULL;
	}
	return fp;
}

/**
 * @note must call fclose after use of returned FILE
 */
FILE* AutoGrownMemIO::forFILE(const char* mode)
{
	cookie_io_functions_t func = {
		MemIO_FILE_read,
		AutoGrownMemIO_FILE_write,
		AutoGrownMemIO_FILE_seek,
		NULL
	};
	void* cookie = this;
	assert(cookie);
	FILE* fp = fopencookie(cookie, mode, func);
	if (fp == NULL) {
		perror("fopencookie@AutoGrownMemIO::forOutputFILE");
		return NULL;
	}
	return fp;
}

#endif

#define STREAM_READER MinMemIO
#define STREAM_WRITER MinMemIO
#include "var_int_io.hpp"

#define STREAM_READER MemIO
#define STREAM_WRITER MemIO
#include "var_int_io.hpp"

#define STREAM_WRITER AutoGrownMemIO
#include "var_int_io.hpp"

} // namespace terark


