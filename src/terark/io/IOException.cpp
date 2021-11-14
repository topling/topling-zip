/* vim: set tabstop=4 : */
#include "IOException.hpp"
#include <terark/num_to_str.hpp>

#include <string.h>

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#else
#	include <errno.h>
#endif

namespace terark {

IOException::IOException(fstring msg)
  : m_errCode(lastError())
{
	m_message.assign(msg.data(), msg.size());
	m_message += ": ";
	m_message += errorText(m_errCode);
}

IOException::IOException(int errCode, fstring szMsg)
  : m_errCode(errCode)
{
	m_message.assign(szMsg.data(), szMsg.size());
	m_message += ": ";
	m_message += errorText(m_errCode);
}

IOException::~IOException() = default;

const char* IOException::what() const noexcept {
	return m_message.c_str();
}

int IOException::lastError()
{
#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)
	return ::GetLastError();
#else
	return errno;
#endif
}

std::string IOException::errorText(int errCode)
{
#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)
	HLOCAL hLocal = NULL;
	DWORD dwTextLength = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER,
		NULL,
		errCode,
		0, //MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)
		(LPSTR)&hLocal,
		0,
		NULL
		);
	string_appender<> oss;
	LPCSTR pszMsg = (LPCSTR)LocalLock(hLocal);
	oss << "error[code=" << errCode << ", message=" << pszMsg << "]";
	LocalFree(hLocal);
#else
	string_appender<> oss;
	//char msg[256] = "strerror_r failed";
	//::strerror_r(errCode, msg, sizeof(msg));
        // strerror_r has two different version, thus is not portable
        #define msg strerror(errCode)
	oss << "error[code=" << errCode << ", message=" << msg << "]";
#endif
	return oss.str();
}

//////////////////////////////////////////////////////////////////////////

OpenFileException::OpenFileException(fstring path, fstring szMsg)
	: IOException(szMsg)
{
	m_path.assign(path.data(), path.size());
	m_message += ": ";
	m_message += m_path;
}

OpenFileException::~OpenFileException() = default;

} // namespace terark
