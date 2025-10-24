/* vim: set tabstop=4 : */
#pragma once

#include <stdio.h>
#include <assert.h>

#if defined(__GLIBC__)
//	#include <libio.h>
#endif

#include <terark/stdtypes.hpp>
#include <terark/util/refcount.hpp>
#include <terark/fstring.hpp>
#include "IOException.hpp"
#include "IStream.hpp"

namespace terark {

TERARK_DLL_EXPORT void set_close_on_exec(int fd);
TERARK_DLL_EXPORT void set_close_on_exec(FILE*);

/**
 @brief FileStream encapsulate FILE* as RefCounter, IInputStream, IOutputStream, ISeekable
 @note
  -# FileStream::eof maybe not thread-safe
  -# FileStream::getByte maybe not thread-safe
  -# FileStream::readByte maybe not thread-safe
  -# FileStream::writeByte maybe not thread-safe
 */
class TERARK_DLL_EXPORT FileStream
	: public RefCounter
	, public ISeekable
	, public IInputStream
	, public IOutputStream
{
	DECLARE_NONE_COPYABLE_CLASS(FileStream)
protected:
	FILE* m_fp;

public:
	typedef boost::mpl::true_ is_seekable;

	static bool copyFile(fstring srcPath, fstring dstPath);
	static void ThrowOpenFileException(fstring fpath, fstring mode);

public:
	FileStream(fstring fpath, fstring mode);
	FileStream(int fd, fstring mode);
//	explicit FileStream(FILE* fp = 0) noexcept : m_fp(fp) {}
	FileStream() noexcept : m_fp(0) {} // 不是我打开的文件，请显式 attach/detach
	~FileStream();

	bool isOpen() const noexcept { return 0 != m_fp; }
	operator FILE*() const noexcept { return m_fp; }

	void open(fstring fpath, fstring mode);

	//! no throw
	bool xopen(fstring fpath, fstring mode);

	void dopen(int fd, fstring mode);

	void close() noexcept;

	void attach(::FILE* fp) noexcept;
	FILE* detach() noexcept;

	FILE* fp() const noexcept { return m_fp; }
#ifdef __USE_MISC
	bool eof() const noexcept override { return !!feof_unlocked(m_fp); }
	int  getByte() noexcept { return fgetc_unlocked(m_fp); }
#else
	bool eof() const noexcept override { return !!feof(m_fp); }
	int  getByte() noexcept { return fgetc(m_fp); }
#endif
	byte readByte();
	void writeByte(byte b);

	void ensureRead(void* vbuf, size_t length);
	void ensureWrite(const void* vbuf, size_t length);

	size_t read(void* buf, size_t size) noexcept override;
	size_t write(const void* buf, size_t size) noexcept override;
	void flush() override;
	void puts(fstring text);

	void rewind() override;
	void seek(stream_offset_t offset, int origin) override;
	void seek(stream_position_t pos) override;
	stream_position_t tell() const override;
	stream_position_t size() const override;

	size_t pread(stream_position_t pos, void* vbuf, size_t length);
	size_t pwrite(stream_position_t pos, const void* vbuf, size_t length);

	void disbuf() noexcept;

public:
	#include "var_int_declare_read.hpp"
	#include "var_int_declare_write.hpp"
private:
#if defined(__GLIBC__)
	size_t reader_remain_bytes() const { FILE* fp = m_fp; return fp->_IO_read_end - fp->_IO_read_ptr; }
	size_t writer_remain_bytes() const { FILE* fp = m_fp; return fp->_IO_write_end - fp->_IO_write_ptr; }
#else
	size_t reader_remain_bytes() const { return 0; }
	size_t writer_remain_bytes() const { return 0; }
#endif
	byte readByte_slow();
	void writeByte_slow(byte b);
	void ensureRead_slow(void* vbuf, size_t length);
	void ensureWrite_slow(const void* vbuf, size_t length);

public:
	uint64_t cat(FILE* fp);
	uint64_t cat(fstring fpath);
	uint64_t fsize() const;
	static uint64_t fpsize(FILE* fp);
	static uint64_t fdsize(int fd);

	void chsize(uint64_t newfsize) const;
	static void fpchsize(FILE* fp, uint64_t newfsize);
	static void fdchsize(int fd, uint64_t newfsize);
};

class TERARK_DLL_EXPORT NonOwnerFileStream : public FileStream {
public:
	explicit NonOwnerFileStream(FILE* fp) noexcept { m_fp = fp; }
	NonOwnerFileStream(const char* fpath, const char* mode) : FileStream(fpath, mode) {}
	NonOwnerFileStream(int fd, const char* mode) : FileStream(fd, mode) {}
	~NonOwnerFileStream();
};

#if defined(__GLIBC__)

inline byte FileStream::readByte() {
	assert(m_fp);
	FILE* fp = m_fp;
	if (terark_likely(fp->_IO_read_ptr < fp->_IO_read_end)) {
		return *(byte*)(fp->_IO_read_ptr++);
	} else {
		return readByte_slow();
	}
}

inline void FileStream::writeByte(byte b) {
	assert(m_fp);
	FILE* fp = m_fp;
	if (terark_likely(fp->_IO_write_ptr < fp->_IO_write_end)) {
		*fp->_IO_write_ptr++ = b;
	} else {
		writeByte_slow(b);
	}
}

inline void FileStream::ensureRead(void* vbuf, size_t length) {
	assert(m_fp);
	FILE* fp = m_fp;
	char* next_ptr = fp->_IO_read_ptr + length;
	if (terark_likely(next_ptr <= fp->_IO_read_end)) {
		memcpy(vbuf, fp->_IO_read_ptr, length);
		fp->_IO_read_ptr = next_ptr;
	} else {
		ensureRead_slow(vbuf, length);
	}
}

inline void FileStream::ensureWrite(const void* vbuf, size_t length) {
	assert(m_fp);
	FILE* fp = m_fp;
	char* next_ptr = fp->_IO_write_ptr + length;
	if (terark_likely(next_ptr <= fp->_IO_write_end)) {
		memcpy(fp->_IO_write_ptr, vbuf, length);
		fp->_IO_write_ptr = next_ptr;
	} else {
		ensureWrite_slow(vbuf, length);
	}
}

#elif defined(_MSC_VER) && _MSC_VER <= 1800

inline byte FileStream::readByte() {
	assert(m_fp);
	FILE* fp = m_fp;
	if (fp->_cnt > 0) {
		fp->_cnt--;
		return *(byte*)(fp->_ptr++);
	} else {
		return readByte_slow();
	}
}

inline void FileStream::writeByte(byte b) {
	assert(m_fp);
	FILE* fp = m_fp;
	if (fp->_cnt > 0) {
		fp->_cnt--;
		*fp->_ptr++ = b;
	} else {
		writeByte_slow(b);
	}
}

inline void FileStream::ensureRead(void* vbuf, size_t length) {
	assert(m_fp);
	FILE* fp = m_fp;
	if (fp->_cnt >= intptr_t(length)) {
		memcpy(vbuf, fp->_ptr, length);
		fp->_cnt -= length;
		fp->_ptr += length;
	} else {
		ensureRead_slow(vbuf, length);
	}
}

inline void FileStream::ensureWrite(const void* vbuf, size_t length) {
	assert(m_fp);
	FILE* fp = m_fp;
	if (fp->_cnt >= intptr_t(length)) {
		memcpy(fp->_ptr, vbuf, length);
		fp->_cnt -= length;
		fp->_ptr += length;
	} else {
		ensureWrite_slow(vbuf, length);
	}
}

#else

inline byte FileStream::readByte()
{
	assert(m_fp);
#ifdef __USE_MISC
	int ch = fgetc_unlocked(m_fp);
#else
	int ch = fgetc(m_fp);
#endif
	if (-1 == ch)
		throw EndOfFileException(BOOST_CURRENT_FUNCTION);
	return ch;
}

inline void FileStream::writeByte(byte b)
{
	assert(m_fp);
#ifdef __USE_MISC
	if (EOF == fputc_unlocked(b, m_fp))
#else
	if (EOF == fputc(b, m_fp))
#endif
		throw OutOfSpaceException(BOOST_CURRENT_FUNCTION);
}

#endif // __GLIBC__

inline void FileStream::puts(fstring text) {
	assert(m_fp);
	ensureWrite(text.data(), text.size());
}

class TERARK_DLL_EXPORT OsFileStream
	: public RefCounter
	, public ISeekable
	, public IInputStream
	, public IOutputStream
{
	DECLARE_NONE_COPYABLE_CLASS(OsFileStream)
protected:
	int m_fd;
	bool m_eof = false;
public:
	typedef boost::mpl::true_ is_seekable;
	static void ThrowOpenFileException(fstring fpath, int flags, int mode);
	OsFileStream(fstring fpath, int flags, int mode);
	OsFileStream() noexcept : m_fd(-1) {}
	~OsFileStream() override;
	bool isOpen() const noexcept { return m_fd >= 0; }
	operator int() const noexcept { return m_fd; } // NOLINT
	void open(fstring fpath, int flags, int mode);
	bool xopen(fstring fpath, int flags, int mode) noexcept;
	void close();
	void attach(int fd) noexcept;
	int detach() noexcept;
	int fd() const noexcept { return m_fd; }
	bool eof() const noexcept override { return m_eof; }
	size_t read(void* buf, size_t len) override;
	size_t write(const void* buf, size_t len) override;
	void flush() override;
	void rewind() override;
	void seek(stream_offset_t offset, int origin) override;
	void seek(stream_position_t pos) override;
	stream_position_t tell() const override;
	stream_position_t size() const override;
	size_t pread(stream_position_t pos, void* buf, size_t len);
	size_t pwrite(stream_position_t pos, const void* buf, size_t len);
	void chsize(llong newfsize) const;
};

} // namespace terark
