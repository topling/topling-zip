/* vim: set tabstop=4 : */
#pragma once

#include <exception>
#include <terark/fstring.hpp>

namespace terark {

#if defined(_MSC_VER)
// non dll-interface class 'std::exception' used as base for dll-interface
#pragma warning(push)
#pragma warning(disable:4275)
#endif
class TERARK_DLL_EXPORT IOException : public std::exception
{
protected:
	std::string m_message;
	int m_errCode;
public:
	explicit IOException(fstring msg);
	explicit IOException(int errCode, fstring szMsg);
	virtual ~IOException() override;

	const char* what() const noexcept override;
	int errCode() const throw() { return m_errCode; }

	static int lastError();
	static std::string errorText(int errCode);
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

class TERARK_DLL_EXPORT OpenFileException : public IOException
{
	std::string m_path;
public:
	using IOException::IOException;
	explicit OpenFileException(fstring path, fstring szMsg);
	~OpenFileException() override;
};

// blocked streams read 0 bytes will cause this exception
// other streams read not enough maybe cause this exception
// all streams read 0 bytes will cause this exception
class TERARK_DLL_EXPORT EndOfFileException : public IOException
{
public:
	using IOException::IOException;
};

class TERARK_DLL_EXPORT OutOfSpaceException : public IOException
{
public:
	using IOException::IOException;
};

class TERARK_DLL_EXPORT DelayWriteException : public IOException
{
public:
	using IOException::IOException;
//	size_t streamPosition;
};

class TERARK_DLL_EXPORT BrokenPipeException : public IOException
{
public:
	using IOException::IOException;
};


} // namespace terark
