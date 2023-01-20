#include "str_lex_iter.hpp"

namespace terark {

template<class CharT>
StringLexIteratorT<CharT>::~StringLexIteratorT() {}

template<class CharT>
void StringLexIteratorT<CharT>::dispose() {
	// default is to direct delete
	delete this;
}

template<class CharT>
bool StringLexIteratorT<CharT>::seek_begin() {
	return seek_lower_bound(fstr());
}

template<class CharT>
bool StringLexIteratorT<CharT>::seek_rev_lower_bound(fstr str) {
    if (seek_lower_bound(str)) {
        if (word() == str)
            return true;
        return decr();
    }
    return seek_end();
}

template class StringLexIteratorT<char>;
template class StringLexIteratorT<uint16_t>;

} // namespace terark
