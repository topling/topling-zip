#pragma once

#include <string>
#include <string.h> // for strlen

#include "config.hpp"
#include "fstring.hpp"

namespace terark {

TERARK_DLL_EXPORT int num_to_str(char* buf, bool x);
TERARK_DLL_EXPORT int num_to_str(char* buf, short x);
TERARK_DLL_EXPORT int num_to_str(char* buf, int x);
TERARK_DLL_EXPORT int num_to_str(char* buf, long x);
TERARK_DLL_EXPORT int num_to_str(char* buf, long long x);
TERARK_DLL_EXPORT int num_to_str(char* buf, unsigned short x);
TERARK_DLL_EXPORT int num_to_str(char* buf, unsigned int x);
TERARK_DLL_EXPORT int num_to_str(char* buf, unsigned long x);
TERARK_DLL_EXPORT int num_to_str(char* buf, unsigned long long x);

TERARK_DLL_EXPORT int num_to_str(char* buf, float x);
TERARK_DLL_EXPORT int num_to_str(char* buf, double x);
TERARK_DLL_EXPORT int num_to_str(char* buf, long double x);

template<class OStream>
class string_op_fmt {
	OStream* m_os;
	const char* m_fmt;
	static bool has_fmt(const char* fmt) {
		size_t fnum = 0;
		while (*fmt) {
			if ('%' == fmt[0]) {
				if ('%' == fmt[1])
					fmt += 2;
				else
					fmt += 1, fnum++;
			} else {
				fmt++;
			}
		}
		TERARK_VERIFY_LE(fnum, 1);
		return 1 == fnum;
	}
	string_op_fmt(const string_op_fmt&) = delete;
	string_op_fmt& operator=(const string_op_fmt&) = delete;
	//string_op_fmt(string_op_fmt&& y) = delete;
	//string_op_fmt& operator=(string_op_fmt&&) = delete;
public:
	explicit string_op_fmt(OStream& os) : m_os(&os), m_fmt(nullptr) {}
	string_op_fmt(OStream& os, const char* fmt_or_str)
	  : m_os(&os), m_fmt(nullptr) {
		std::move(*this) ^ fmt_or_str;
	}
	string_op_fmt(OStream& os, fstring str) : m_os(&os), m_fmt(nullptr) {
		std::move(*this) ^ str;
	}
	~string_op_fmt() {
		if (m_fmt) *m_os << m_fmt;
	}
	operator OStream&() && {
		TERARK_VERIFY_F(nullptr == m_fmt, "%s", m_fmt);
		return *m_os;
	}
	// does not support any "%s"
	string_op_fmt&& operator^(const char* fmt_or_str) && {
		if (m_fmt) { // this should be error, but we tolerate it
			*m_os << m_fmt;
			m_fmt = nullptr;
		}
		if (fmt_or_str) {
			if (has_fmt(fmt_or_str)) {
				m_fmt = fmt_or_str;
			} else {
				*m_os << fmt_or_str;
			}
		} else {
			*m_os << "(null)";
		}
		return std::move(*this);
	}
	template<class T>
	typename std::enable_if<std::is_fundamental<T>::value ||
							std::is_pointer<T>::value, string_op_fmt&&>::type
	operator^(const T x) && {
		if (m_fmt) {
			char buf[2048];
			auto len = snprintf(buf, sizeof(buf), m_fmt, x);
			*m_os << fstring(buf, len);
			m_fmt = nullptr;
		} else {
			*m_os << x;
		}
		return std::move(*this);
	}
	// T is a string type
	template<class T>
	auto operator^(const T& x) && ->
	typename
	std::enable_if< ( std::is_same<std::decay_t<decltype(*x.data())>, char>::value ||
					  std::is_same<std::decay_t<decltype(*x.data())>, signed char>::value ||
					  std::is_same<std::decay_t<decltype(*x.data())>, unsigned char>::value )
				 && std::is_integral<decltype(x.size())>::value,
	string_op_fmt&&>::type {
		if (m_fmt) {
			*m_os << m_fmt;
			m_fmt = nullptr;
		}
		*m_os << x;
		return std::move(*this);
	}
};

template<class String = std::string>
struct string_appender : public String {
	using String::String;

	const String& str() const { return *this; }
	String& str() { return *this; }

	template<class T>
	string_appender& operator&(const T& x) { return (*this) << x; }

	template<class T>
	string_appender& operator|(const T& x) { return (*this) << x; }

	string_op_fmt<string_appender> operator^(const char* fmt_or_str) {
		 return string_op_fmt<string_appender>(*this, fmt_or_str);
	}
	string_op_fmt<string_appender> operator^(fstring str) {
		 return string_op_fmt<string_appender>(*this, str);
	}

	string_appender& operator<<(const fstring x) { this->append(x.data(), x.size()); return *this; }
	string_appender& operator<<(const char*   x) { this->append(x, strlen(x)); return *this; }
	string_appender& operator<<(const char    x) { this->push_back(x); return *this; }
	string_appender& operator<<(const bool    x) { this->push_back(x ? '1' : '0'); return *this; }

	string_appender& operator<<(short x) { char buf[16]; this->append(buf, num_to_str(buf, x)); return *this; };
	string_appender& operator<<(int x) { char buf[32]; this->append(buf, num_to_str(buf, x)); return *this; };
	string_appender& operator<<(long x) { char buf[48]; this->append(buf, num_to_str(buf, x)); return *this; };
	string_appender& operator<<(long long x) { char buf[64]; this->append(buf, num_to_str(buf, x)); return *this; };
	string_appender& operator<<(unsigned short x) { char buf[16]; this->append(buf, num_to_str(buf, x)); return *this; };
	string_appender& operator<<(unsigned int x) { char buf[32]; this->append(buf, num_to_str(buf, x)); return *this; };
	string_appender& operator<<(unsigned long x) { char buf[48]; this->append(buf, num_to_str(buf, x)); return *this; };
	string_appender& operator<<(unsigned long long x) { char buf[64]; this->append(buf, num_to_str(buf, x)); return *this; };

	string_appender& operator<<(float x) { char buf[96]; this->append(buf, num_to_str(buf, x)); return *this; };
	string_appender& operator<<(double x) { char buf[96]; this->append(buf, num_to_str(buf, x)); return *this; };
	string_appender& operator<<(long double x) { char buf[96]; this->append(buf, num_to_str(buf, x)); return *this; };

	template<class StreamOperator>
	auto operator<<(const StreamOperator& op) -> decltype(op(*this))& {
		return op(*this);
	}
	string_appender& write(const typename String::value_type* ptr, size_t len) {
		this->append(ptr, len);
		return *this;
	}
};

template<class StringView = fstring>
struct ReplaceChar {
	template<class OutputStream>
	OutputStream& operator()(OutputStream& os) const {
		size_t oldsize = os.size(), textlen = text.size();
		os.resize(oldsize + textlen);
		auto src = text.data();
		auto dst = os.data() + oldsize;
		for (size_t i = 0; i < textlen; i++) {
			const auto ch = src[i];
			dst[i] = ch == match ? replace : ch;
		}
		return os;
	}
	const StringView text;
	const typename StringView::value_type match, replace;
};
template<class StringView = fstring>
struct ReplaceSubStr {
	template<class OutputStream>
	OutputStream& operator()(OutputStream& os) const {
		auto text_data = text.data();
		auto search_pos = size_t(0), hit_pos = text.find(search_pos, match);
		while (hit_pos != StringView::npos) {
			os.write(text_data + search_pos, hit_pos - search_pos);
			os.write(replace.data(), replace.size());
			search_pos = hit_pos + match.size();
			hit_pos = text.find(search_pos, match);
		}
		TERARK_ASSERT_LE(search_pos, text.size());
		os.write(text_data + search_pos, text.size() - search_pos);
		return os;
	}
	const StringView text;
	const StringView match;
	const StringView replace;
};

template<class StringView = fstring>
class ReplaceBySmallTR {
public:
	template<class OutputStream>
	OutputStream& operator()(OutputStream& os) const {
		size_t oldsize = os.size(), textlen = text.size(), tr_len = match.size();
		os.resize(oldsize + textlen);
		auto p_match = match.data(), p_replace = replace.data();
		auto src = text.data();
		auto dst = os.data() + oldsize;
		for (size_t i = 0; i < textlen; i++) {
			auto ch = src[i];
			for (size_t j = 0; j < tr_len; j++) {
				if (ch == p_match[j]) {
					ch = p_replace[j];
					break;
				}
			}
			dst[i] = ch;
		}
		return os;
	}
	ReplaceBySmallTR(StringView _text, StringView _match, StringView _replace)
		: text(_text), match(_match), replace(_replace)
	{
		TERARK_VERIFY_EQ(_match.size(), _replace.size());
	}
private:
	const StringView text;
	const StringView match;
	const StringView replace;
};

template<class String>
inline string_appender<String>& as_string_appender(String& str) {
	return static_cast<string_appender<String>&>(str);
}
template<class String>
inline string_appender<String> as_string_appender(const String& str) {
	return static_cast<const string_appender<String>&>(str); // copy
}
template<class String>
inline string_appender<String>&& as_string_appender(String&& str) {
	return static_cast<string_appender<String>&&>(str);
}

} // namespace terark

