
// Created by Lei Peng 2017-07-25

#pragma once

#include <terark/config.hpp> // for terark_likely
#include <terark/heap_ext.hpp>
#include <stddef.h>
#include <algorithm>
#include <functional>
#include <memory> // for unique_ptr

namespace terark {

template<class T>
std::unique_ptr<T> unique_ptr_from_move_cons(T&& t) {
	return std::unique_ptr<T>(new T(std::move(t)));
}
template<class T>
std::unique_ptr<T> unique_ptr_from_move_cons(T& t) = delete;
template<class T>
std::unique_ptr<T> unique_ptr_from_move_cons(const T& t) = delete;

template<class T>
std::unique_ptr<T>
replace_select_sort_unique_ptr_of(T* p) {
	return std::unique_ptr<T>(p);
}
template<class T>
std::unique_ptr<T>
replace_select_sort_unique_ptr_of(std::unique_ptr<T>&& p) {
	return std::unique_ptr<T>(std::move(p));
}
template<class T>
std::unique_ptr<T>
replace_select_sort_unique_ptr_of(std::unique_ptr<T>& p) = delete;
template<class T>
std::unique_ptr<T>
replace_select_sort_unique_ptr_of(const std::unique_ptr<T>& p) = delete;

///@param createWriter(runs) should return a pointer
template<class Vec, class Reader, class WriterFactory, class Less, class Greater>
void
replace_select_sort(Vec& vec, Reader read, WriterFactory createWriter, Less less, Greater greater) {
	size_t size = vec.size();
	assert(size > 0);
	if (0 == size) {
		throw std::invalid_argument("vec.size() must > 0");
	}
	size_t hlen = 0;
	size_t runs = 0;
	auto pWriter = replace_select_sort_unique_ptr_of(createWriter(runs++));
	auto beg = vec.begin();
	while (hlen < size && read(&beg[hlen])) {
		hlen++;
	}
	if (hlen < size) {
		std::sort(beg, beg + hlen, less);
		for (size_t i = 0; i < hlen; ++i) {
			(*pWriter)(beg[i]);
		}
		return; // done
	}
	else {
		std::make_heap(beg, beg + size, greater);
	}
	typedef typename Vec::value_type Value;
	Value curr;
	while (read(&curr)) {
		(*pWriter)(beg[0]);
		size_t hole = 0;
		if (terark_likely(!less(curr, beg[0]))) { // curr >= beg[0]
			terark_adjust_heap(beg, hole, hlen, std::move(curr), greater);
		}
		else { // curr < beg[0]
			if (terark_likely(--hlen)) {
				terark_adjust_heap(beg, hole, hlen, std::move(beg[hlen]), greater);
				beg[hlen] = std::move(curr);
			}
			else {
				pWriter = replace_select_sort_unique_ptr_of(createWriter(runs++));
				beg[0] = std::move(curr);
				std::make_heap(beg, beg + size, greater);
				hlen = size;
			}
		}
	}
	std::sort(beg, beg + hlen, less);
	for (size_t i = 0; i < hlen; ++i) (*pWriter)(beg[i]);
	if (hlen < size) {
		pWriter = replace_select_sort_unique_ptr_of(createWriter(runs++));
		std::sort(beg + hlen, beg + size, less);
		for (size_t i = hlen; i < size; ++i) (*pWriter)(beg[i]);
	}
}

template<class Vec, class Reader, class Writer, class Less>
void replace_select_sort(Vec& vec, Reader read, Writer write, Less less) {
	typedef typename Vec::value_type Value;
	auto greater = [less](const Value& x, const Value& y) {
		return less(y, x);
	};
	replace_select_sort<Vec, Reader, Writer>(vec, read, write, less, greater);
}

template<class Vec, class Reader, class Writer>
void replace_select_sort(Vec& vec, Reader read, Writer write) {
	typedef typename Vec::value_type Value;
	replace_select_sort<Vec, Reader, Writer>(
		vec, read, write, std::less<Value>(), std::greater<Value>());
}

} // namespace terark
