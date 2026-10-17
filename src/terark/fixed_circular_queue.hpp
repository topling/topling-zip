/* vim: set tabstop=4 : */
/********************************************************************
	@file fixed_circular_queue.hpp
	@brief 循环队列的实现

	@date	2023-10-02 23:17
	@author	Lei Peng
	@{
*********************************************************************/
#pragma once

#include <terark/stdtypes.hpp>
#include <iterator>
#include <type_traits>
#include <utility>

namespace terark {

/**
 @brief 固定容量的双端循环队列，所有 Capacity 个槽位均可使用。

  - head/tail 是序号而不是数组下标，序号相等表示空，距离 Capacity 表示满。
  - 2 的幂容量使用无符号整数自然回绕；其他容量使用 [0, 2 * Capacity) 序号。
  - 所有输入参数均需要满足其前条件；本容器本身不提供并发同步。
 */
template<class ElemT, int Capacity>
class fixed_circular_queue
{
	static_assert(Capacity > 0, "Capacity must be positive");

public:
	typedef ElemT value_type;
	typedef ElemT& reference;
	typedef const ElemT& const_reference;
	typedef ElemT* pointer;
	typedef const ElemT* const_pointer;
	typedef uintptr_t size_type;
	typedef unsigned counter_type;

private:
	counter_type m_head; // next pop_front sequence
	counter_type m_tail; // next push_back sequence
	union {
		ElemT m_vec[Capacity];
	};
	static constexpr counter_type m_nlen = Capacity;
	static constexpr bool CapIsPower2 = (Capacity & (Capacity - 1)) == 0;
	static constexpr counter_type m_serial_limit = m_nlen * 2;
	static_assert(counter_type(Capacity) < counter_type(-1) / 2 + 1,
		"Capacity must fit in half the counter range");

	static counter_type slot(counter_type serial) noexcept
	{
		if (CapIsPower2) {
			return serial & (m_nlen - 1);
		}
		return serial >= m_nlen ? serial - m_nlen : serial;
	}

	static counter_type next(counter_type serial) noexcept
	{
		if (CapIsPower2) {
			return serial + 1;
		}
		return serial == m_serial_limit - 1 ? 0 : serial + 1;
	}

	static counter_type prev(counter_type serial) noexcept
	{
		if (CapIsPower2) {
			return serial - 1;
		}
		return serial == 0 ? m_serial_limit - 1 : serial - 1;
	}

	static counter_type distance(counter_type from, counter_type to) noexcept
	{
		if (CapIsPower2) {
			return counter_type(to - from);
		}
		return to >= from ? to - from : m_serial_limit - from + to;
	}

	static counter_type advance(counter_type serial, ptrdiff_t n) noexcept
	{
		assert(n >= -ptrdiff_t(Capacity));
		assert(n <= ptrdiff_t(Capacity));
		if (CapIsPower2) {
			return serial + counter_type(n);
		}
		if (n >= 0) {
			counter_type count = counter_type(n);
			counter_type remain = m_serial_limit - serial;
			return count >= remain ? count - remain : serial + count;
		}
		counter_type count = counter_type(-n);
		return count > serial ? m_serial_limit - (count - serial) : serial - count;
	}

	template<bool Move, class Source>
	void construct_from(Source& other)
	{
		m_head = m_tail = other.m_head;
		try {
			while (m_tail != other.m_tail) {
				if constexpr (Move) {
					emplace_back(std::move(other.m_vec[slot(m_tail)]));
				} else {
					emplace_back(other.m_vec[slot(m_tail)]);
				}
			}
		} catch (...) {
			clear();
			throw;
		}
	}

public:
	template<bool IsConst>
	class basic_iterator
	{
		friend class fixed_circular_queue;
		template<bool> friend class basic_iterator;
		typedef typename std::conditional<IsConst,
			const fixed_circular_queue, fixed_circular_queue>::type queue_type;
		queue_type* m_queue = nullptr;
		counter_type m_serial = 0;

		basic_iterator(queue_type* queue, counter_type serial) noexcept
			: m_queue(queue), m_serial(serial) {}

		ptrdiff_t offset() const noexcept { return m_queue->virtual_index(m_serial); }

	public:
		typedef std::random_access_iterator_tag iterator_category;
		typedef ElemT value_type;
		typedef ptrdiff_t difference_type;
		typedef typename std::conditional<IsConst, const ElemT*, ElemT*>::type pointer;
		typedef typename std::conditional<IsConst, const ElemT&, ElemT&>::type reference;

		basic_iterator() noexcept = default;
		template<bool OtherConst,
			typename std::enable_if<IsConst || !OtherConst, int>::type = 0>
		basic_iterator(const basic_iterator<OtherConst>& other) noexcept
			: m_queue(other.m_queue), m_serial(other.m_serial) {}

		reference operator*() const noexcept { return m_queue->m_vec[slot(m_serial)]; }
		pointer operator->() const noexcept { return &**this; }
		reference operator[](difference_type n) const noexcept { return *(*this + n); }

		basic_iterator& operator++() noexcept
		{
			m_serial = next(m_serial);
			return *this;
		}
		basic_iterator operator++(int) noexcept
		{
			basic_iterator old = *this;
			++*this;
			return old;
		}
		basic_iterator& operator--() noexcept
		{
			m_serial = prev(m_serial);
			return *this;
		}
		basic_iterator operator--(int) noexcept
		{
			basic_iterator old = *this;
			--*this;
			return old;
		}
		basic_iterator& operator+=(difference_type n) noexcept
		{
			assert(n >= -offset());
			assert(n <= difference_type(m_queue->size()) - offset());
			if constexpr (CapIsPower2) {
				m_serial += counter_type(n);
			} else {
				m_serial = advance(m_serial, n);
			}
			return *this;
		}
		basic_iterator& operator-=(difference_type n) noexcept
		{
			assert(n <= offset());
			assert(n >= offset() - difference_type(m_queue->size()));
			if constexpr (CapIsPower2) {
				m_serial -= counter_type(n);
			} else {
				m_serial = advance(m_serial, -n);
			}
			return *this;
		}
		basic_iterator operator+(difference_type n) const noexcept
		{
			basic_iterator iter = *this;
			return iter += n;
		}
		basic_iterator operator-(difference_type n) const noexcept
		{
			basic_iterator iter = *this;
			return iter -= n;
		}
		template<bool OtherConst>
		difference_type operator-(const basic_iterator<OtherConst>& other) const noexcept
		{
			assert(m_queue == other.m_queue);
			return offset() - other.offset();
		}
		template<bool OtherConst>
		bool operator==(const basic_iterator<OtherConst>& other) const noexcept
		{
			return m_queue == other.m_queue && m_serial == other.m_serial;
		}
		template<bool OtherConst>
		bool operator!=(const basic_iterator<OtherConst>& other) const noexcept
		{
			return !(*this == other);
		}
		template<bool OtherConst>
		bool operator<(const basic_iterator<OtherConst>& other) const noexcept
		{
			assert(m_queue == other.m_queue);
			return offset() < other.offset();
		}
		template<bool OtherConst>
		bool operator>(const basic_iterator<OtherConst>& other) const noexcept
		{
			return other < *this;
		}
		template<bool OtherConst>
		bool operator<=(const basic_iterator<OtherConst>& other) const noexcept
		{
			return !(other < *this);
		}
		template<bool OtherConst>
		bool operator>=(const basic_iterator<OtherConst>& other) const noexcept
		{
			return !(*this < other);
		}
		friend basic_iterator operator+(difference_type n, basic_iterator iter) noexcept
		{
			return iter += n;
		}
	};

	typedef basic_iterator<false> iterator;
	typedef basic_iterator<true> const_iterator;

	explicit fixed_circular_queue(ptrdiff_t cap) : m_head(0), m_tail(0)
	{
		TERARK_VERIFY_EQ(Capacity, cap);
	}
	fixed_circular_queue() noexcept : m_head(0), m_tail(0) {}

	fixed_circular_queue(const fixed_circular_queue& other) : m_head(0), m_tail(0)
	{
		construct_from<false>(other);
	}
	fixed_circular_queue(fixed_circular_queue&& other)
		noexcept(std::is_nothrow_move_constructible<ElemT>::value) : m_head(0), m_tail(0)
	{
		construct_from<true>(other);
		other.clear();
	}
	fixed_circular_queue& operator=(const fixed_circular_queue& other)
	{
		if (this != &other) {
			clear();
			construct_from<false>(other);
		}
		return *this;
	}
	fixed_circular_queue& operator=(fixed_circular_queue&& other)
		noexcept(std::is_nothrow_move_constructible<ElemT>::value)
	{
		if (this != &other) {
			clear();
			construct_from<true>(other);
			other.clear();
		}
		return *this;
	}

	void init(ptrdiff_t /*cap*/)
	{
		TERARK_VERIFY_EQ(m_head, m_tail);
	}

	~fixed_circular_queue() { clear(); }

	void clear()
	{
		while (!empty()) {
			pop_front();
		}
		m_head = m_tail = 0;
	}

	bool empty() const noexcept { return m_head == m_tail; }
	bool full() const noexcept { return distance(m_head, m_tail) == m_nlen; }
	size_type size() const noexcept { return distance(m_head, m_tail); }
	size_type capacity() const noexcept { return m_nlen; }

	void push_back(const ElemT& val) { emplace_back(val); }
	void push_back(ElemT&& val) { emplace_back(std::move(val)); }
	template<class... Args>
	void emplace_back(Args&&... args)
	{
		assert(!full());
		new(static_cast<void*>(m_vec + slot(m_tail))) ElemT(std::forward<Args>(args)...);
		m_tail = next(m_tail);
	}

	const ElemT& front() const noexcept
	{
		assert(!empty());
		return m_vec[slot(m_head)];
	}
	ElemT& front() noexcept
	{
		assert(!empty());
		return m_vec[slot(m_head)];
	}
	void pop_front(ElemT& val)
	{
		assert(!empty());
		val = std::move(m_vec[slot(m_head)]);
		pop_front();
	}
	void pop_front()
	{
		assert(!empty());
		m_vec[slot(m_head)].~ElemT();
		m_head = next(m_head);
	}
	void pop_lower(counter_type serial)
	{
		assert(!empty());
		size_type count = size_type(virtual_index(serial)) + 1;
		assert(count <= size());
		while (count-- > 0) {
			pop_front();
		}
	}

	void push_front(const ElemT& val) { emplace_front(val); }
	void push_front(ElemT&& val) { emplace_front(std::move(val)); }
	template<class... Args>
	void emplace_front(Args&&... args)
	{
		assert(!full());
		counter_type head = prev(m_head);
		new(static_cast<void*>(m_vec + slot(head))) ElemT(std::forward<Args>(args)...);
		m_head = head;
	}
	ElemT& back() noexcept
	{
		assert(!empty());
		return m_vec[slot(prev(m_tail))];
	}
	const ElemT& back() const noexcept
	{
		assert(!empty());
		return m_vec[slot(prev(m_tail))];
	}
	void pop_back(ElemT& val)
	{
		assert(!empty());
		val = std::move(back());
		pop_back();
	}
	void pop_back()
	{
		assert(!empty());
		m_tail = prev(m_tail);
		m_vec[slot(m_tail)].~ElemT();
	}
	void pop_upper(counter_type serial)
	{
		assert(!empty());
		size_type count = size() - size_type(virtual_index(serial));
		assert(count <= size());
		while (count-- > 0) {
			pop_back();
		}
	}

	iterator begin() noexcept { return iterator(this, m_head); }
	const_iterator begin() const noexcept { return const_iterator(this, m_head); }
	const_iterator cbegin() const noexcept { return begin(); }
	iterator end() noexcept { return iterator(this, m_tail); }
	const_iterator end() const noexcept { return const_iterator(this, m_tail); }
	const_iterator cend() const noexcept { return end(); }

	ptrdiff_t virtual_index(counter_type serial) const noexcept
	{
		counter_type index = distance(m_head, serial);
		assert(index <= size());
		return index;
	}
	counter_type real_index(ptrdiff_t index) const noexcept
	{
		assert(index >= 0);
		assert(size_type(index) <= size());
		return advance(m_head, index);
	}
	counter_type head_real_index() const noexcept { return m_head; }
	counter_type tail_real_index() const noexcept { return m_tail; }

	ElemT& operator[](ptrdiff_t index)
	{
		assert(index >= 0);
		assert(size_type(index) < size());
		return m_vec[slot(advance(m_head, index))];
	}
	const ElemT& operator[](ptrdiff_t index) const
	{
		assert(index >= 0);
		assert(size_type(index) < size());
		return m_vec[slot(advance(m_head, index))];
	}
};

} // namespace terark
