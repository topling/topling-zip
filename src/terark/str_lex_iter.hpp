#pragma once
#include <terark/valvec.hpp>
#include <terark/fstring.hpp>
#include <terark/util/function.hpp>

namespace terark {

template<class CharT>
class TERARK_DLL_EXPORT StringLexIteratorT : public CacheAlignedNewDelete, boost::noncopyable {
protected:
    typedef typename terark_get_uchar_type<CharT>::type uch_t;
	valvec<uch_t>  m_word;
	virtual ~StringLexIteratorT();
public:
	typedef basic_fstring<CharT> fstr;
	virtual void dispose();
	virtual bool incr() = 0;
	virtual bool decr() = 0;
	virtual bool seek_begin();
	virtual bool seek_end() = 0;
	virtual bool seek_lower_bound(fstr) = 0;
	bool seek_rev_lower_bound(fstr); // convenient function

	fstr word() const { return fstr(m_word.data(), m_word.size()); }

	// for user add app data after m_word.size() and before m_word.capacity()
	// user should not add more than 16 bytes app data
	valvec<uch_t>& mutable_word() { return m_word; }
};

typedef StringLexIteratorT<char    >  StringLexIterator;
typedef StringLexIteratorT<uint16_t>  StringLexIterator16;

struct DisposeAsDelete {
	template<class T> void operator()(T* p) const { p->dispose(); }
};

} // namespace terark
