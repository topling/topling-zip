/* vim: set tabstop=4 : */
/********************************************************************
	@file circular_queue.hpp
	@brief 循环队列的实现

	@date	2006-9-28 12:07
	@author	Lei Peng
	@{
*********************************************************************/
#pragma once

#include <terark/stdtypes.hpp>
#include <terark/util/throw.hpp>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

namespace terark {

/**
 @brief 使用一个用户指定的 vector 类容器实现循环队列

  - 循环队列的大小是固定的，其结构是一个环状数组，所有槽位均可存储有效元素

  - 循环队列可以提供 serial/real_index 序列号，可用于元素的标识（如网络协议中的帧序号）

  - 所有输入参数均需要满足其前条件，如果不满足，
    Debug 版会引发断言失败，Release 版会导致未定义行为
 */
template<class ElemT, bool CapIsPower2 = false>
class circular_queue
{
public:
    using counter_type = uintptr_t;

private:
    ElemT* m_vec;
    counter_type m_head; // next pop_front sequence
    counter_type m_tail; // next push_back sequence
    counter_type m_nlen;

    counter_type index(counter_type serial) const noexcept {
        if (CapIsPower2) {
            return serial & (m_nlen - 1);
        }
        return serial < m_nlen ? serial : serial - m_nlen;
    }

    counter_type distance(counter_type first, counter_type last) const noexcept {
        counter_type count = last - first;
        if (!CapIsPower2 && last < first) {
            count += 2 * m_nlen;
        }
        return count;
    }

    counter_type next_n(counter_type current, counter_type n) const noexcept {
        assert(n <= m_nlen);
        if (CapIsPower2) {
            return current + n;
        }
        // Wrap at 2N so non-power-of-two slots remain continuous at rollover.
        counter_type until_wrap = 2 * m_nlen - current;
        return n < until_wrap ? current + n : n - until_wrap;
    }

    counter_type prev_n(counter_type current, counter_type n) const noexcept {
        assert(n <= m_nlen);
        if (CapIsPower2) {
            return current - n;
        }
        return current >= n ? current - n : 2 * m_nlen - (n - current);
    }

    counter_type next(counter_type current) const noexcept { return next_n(current, 1); }
    counter_type prev(counter_type current) const noexcept { return prev_n(current, 1); }

    static ElemT* allocate(counter_type capacity) {
        TERARK_VERIFY_LE(capacity, size_t(-1) / sizeof(ElemT));
        if (capacity == 0) {
            return nullptr;
        }
        void* memory;
        if constexpr (alignof(ElemT) > alignof(max_align_t)) {
            memory = ::operator new(sizeof(ElemT) * capacity,
                std::align_val_t(alignof(ElemT)), std::nothrow);
        } else {
            memory = ::malloc(sizeof(ElemT) * capacity);
        }
        TERARK_VERIFY(memory != nullptr);
        return static_cast<ElemT*>(memory);
    }

    static void deallocate(ElemT* memory) noexcept {
        if constexpr (alignof(ElemT) > alignof(max_align_t)) {
            ::operator delete(memory, std::align_val_t(alignof(ElemT)));
        } else {
            ::free(memory);
        }
    }

public:
	typedef ElemT			value_type;

	typedef       ElemT&          reference;
	typedef const ElemT&    const_reference;
	typedef       ElemT*	      pointer;
	typedef const ElemT*    const_pointer;

	typedef uintptr_t	size_type;

    template<bool IsConst>
    class basic_iterator {
        friend class circular_queue;
        template<bool> friend class basic_iterator;
        using queue_type = typename std::conditional<
            IsConst, const circular_queue, circular_queue>::type;
        queue_type* queue = nullptr;
        counter_type serial = 0;

        basic_iterator(queue_type* q, counter_type s) noexcept : queue(q), serial(s) {}

    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = ElemT;
        using difference_type = ptrdiff_t;
        using pointer = typename std::conditional<IsConst, const ElemT*, ElemT*>::type;
        using reference = typename std::conditional<IsConst, const ElemT&, ElemT&>::type;

        basic_iterator() noexcept = default;

        template<bool OtherConst, typename std::enable_if<IsConst && !OtherConst, int>::type = 0>
        basic_iterator(const basic_iterator<OtherConst>& other) noexcept
          : queue(other.queue), serial(other.serial) {}

        reference operator*() const noexcept {
            assert(queue->distance(queue->m_head, serial) < queue->size());
            return queue->m_vec[queue->index(serial)];
        }
        pointer operator->() const noexcept { return &operator*(); }
        reference operator[](difference_type n) const noexcept { return *(*this + n); }

        basic_iterator& operator+=(difference_type n) noexcept {
            assert(n >= -queue->virtual_index(serial));
            assert(n <= difference_type(queue->size()) - queue->virtual_index(serial));
            if constexpr (CapIsPower2) {
                serial += counter_type(n);
            } else {
                serial = n >= 0 ? queue->next_n(serial, counter_type(n))
                                : queue->prev_n(serial, counter_type(-n));
            }
            return *this;
        }
        basic_iterator& operator-=(difference_type n) noexcept {
            assert(n <= queue->virtual_index(serial));
            assert(n >= queue->virtual_index(serial) - difference_type(queue->size()));
            if constexpr (CapIsPower2) {
                serial -= counter_type(n);
            } else {
                serial = n >= 0 ? queue->prev_n(serial, counter_type(n))
                                : queue->next_n(serial, counter_type(-n));
            }
            return *this;
        }
        basic_iterator& operator++() noexcept { return *this += 1; }
        basic_iterator& operator--() noexcept { return *this -= 1; }
        basic_iterator operator++(int) noexcept {
            basic_iterator old = *this;
            ++*this;
            return old;
        }
        basic_iterator operator--(int) noexcept {
            basic_iterator old = *this;
            --*this;
            return old;
        }
        basic_iterator operator+(difference_type n) const noexcept {
            basic_iterator result = *this;
            result += n;
            return result;
        }
        basic_iterator operator-(difference_type n) const noexcept {
            basic_iterator result = *this;
            result -= n;
            return result;
        }
        template<bool OtherConst>
        difference_type operator-(const basic_iterator<OtherConst>& other) const noexcept {
            assert(queue == other.queue);
            return queue->virtual_index(serial) - queue->virtual_index(other.serial);
        }
        template<bool OtherConst>
        bool operator==(const basic_iterator<OtherConst>& other) const noexcept {
            return queue == other.queue && serial == other.serial;
        }
        template<bool OtherConst>
        bool operator!=(const basic_iterator<OtherConst>& other) const noexcept {
            return !(*this == other);
        }
        template<bool OtherConst>
        bool operator<(const basic_iterator<OtherConst>& other) const noexcept {
            return *this - other < 0;
        }
        template<bool OtherConst>
        bool operator>(const basic_iterator<OtherConst>& other) const noexcept {
            return other < *this;
        }
        template<bool OtherConst>
        bool operator<=(const basic_iterator<OtherConst>& other) const noexcept {
            return !(other < *this);
        }
        template<bool OtherConst>
        bool operator>=(const basic_iterator<OtherConst>& other) const noexcept {
            return !(*this < other);
        }
        friend basic_iterator operator+(difference_type n, const basic_iterator& iter) noexcept {
            return iter + n;
        }
    };

    using iterator = basic_iterator<false>;
    using const_iterator = basic_iterator<true>;

	/**
	 @brief 构造最多能容纳 cap 个有效元素的循环队列
	 */
    explicit circular_queue(ptrdiff_t cap) {
        if (cap < 0 || (CapIsPower2 && (cap == 0 || (cap & (cap - 1)) != 0))) {
            THROW_STD(logic_error, "invalid cap = %zd", cap);
        }
        m_nlen = counter_type(cap);
        m_vec = allocate(m_nlen);
        m_head = m_tail = 0;
    }
    circular_queue() noexcept : m_vec(nullptr), m_head(0), m_tail(0), m_nlen(0) {}

    circular_queue(const circular_queue& other)
      : m_vec(allocate(other.m_nlen)), m_head(other.m_head),
        m_tail(other.m_head), m_nlen(other.m_nlen) {
        try {
            while (m_tail != other.m_tail) {
                emplace_back(other.m_vec[index(m_tail)]);
            }
        } catch (...) {
            clear();
            deallocate(m_vec);
            throw;
        }
    }

    circular_queue(circular_queue&& other) noexcept
      : m_vec(other.m_vec), m_head(other.m_head),
        m_tail(other.m_tail), m_nlen(other.m_nlen) {
        other.m_vec = nullptr;
        other.m_head = other.m_tail = other.m_nlen = 0;
    }

    circular_queue& operator=(const circular_queue& other) {
        if (this != &other) {
            circular_queue copy(other);
            swap(copy);
        }
        return *this;
    }

    circular_queue& operator=(circular_queue&& other) noexcept {
        if (this != &other) {
            circular_queue moved(std::move(other));
            swap(moved);
        }
        return *this;
    }

    void swap(circular_queue& other) noexcept {
        std::swap(m_vec, other.m_vec);
        std::swap(m_head, other.m_head);
        std::swap(m_tail, other.m_tail);
        std::swap(m_nlen, other.m_nlen);
    }

	void init(ptrdiff_t cap) {
		assert(0 == m_nlen);
		new(this)circular_queue(cap);
	}

	~circular_queue() {
		clear();
		deallocate(m_vec);
	}

	/**
	 @brief 清除队列中的有效元素

	 - 前条件：无
	 - 操作结果：队列为空
	 */
	void clear()
	{
		while (!empty())
			pop_front();
		m_head = m_tail = 0;
	}

	/**
	 @brief 测试队列是否为空

	 - 前条件：无
	 @return true 表示队列为空，false 表示非空
	 */
	bool empty() const noexcept { return m_head == m_tail; }

	/**
	 @brief 测试队列是否已满

	 - 前条件：无
	 @return true 表示队列已满，false 表示未满
	 */
	bool full() const noexcept { return size() == m_nlen; }

	/**
	 @brief 返回队列当前尺寸

	 - 前条件：无
	 @return 队列中有效元素的个数，总小于等于 capacity
	 */
    size_type size() const noexcept { return distance(m_head, m_tail); }

	/**
	 @brief 返回队列容量

	 - 前条件：无
	 @return 即构造该对象时传入的参数，或者 resize 后的新容量
	 */
	size_type capacity() const noexcept { return m_nlen; }

	/**
	 @brief 在队列尾部加入一个新元素

	 - 前条件：队列不满
	 - 操作结果：新元素被添加到队列尾部
	 */
	void push_back(const ElemT& val)
	{
		assert(!full());
		new(static_cast<void*>(m_vec + index(m_tail)))ElemT(val);
		m_tail = next(m_tail);
	}

	void push_back(ElemT&& val)
	{
		assert(!full());
		new(static_cast<void*>(m_vec + index(m_tail)))ElemT(std::move(val));
		m_tail = next(m_tail);
	}

	template<class... Args>
	void emplace_back(Args&&... args)
	{
		assert(!full());
		new(static_cast<void*>(m_vec + index(m_tail)))ElemT(std::forward<Args>(args)...);
		m_tail = next(m_tail);
	}

	//@{
	/**
	 @brief 返回队列头部的那个元素

	 - 前条件：队列不空
	 */
	const ElemT& front() const noexcept
	{
		assert(!empty());
		return m_vec[index(m_head)];
	}
	ElemT& front() noexcept
	{
		assert(!empty());
		return m_vec[index(m_head)];
	}
	//@}

	/**
	 @brief 弹出队列头部的那个元素并通过 out 参数 val 返回

	 - 前条件：队列不空
	 @param[out] val 队列头部元素将被复制进 val
	 */
	void pop_front(ElemT& val)
	{
		assert(!empty());
		val = std::move(m_vec[index(m_head)]);
		m_vec[index(m_head)].~ElemT();
		m_head = next(m_head);
	}

	/**
	 @brief 弹出队列头部的那个元素

	 - 前条件：队列不空
	 该函数与 stl vector/list/deque 兼容
	 */
	void pop_front()
	{
		assert(!empty());
		m_vec[index(m_head)].~ElemT();
		m_head = next(m_head);
	}

	/**
	 @brief 弹出序列号小于等于输入参数 real_index 的所有元素

	  pop all elements which real_index is earlier or equal than real_index
	 - 前条件：队列不空，且参数 real_index 代表的元素必须在队列中
	 */
	void pop_lower(counter_type real_index)
	{
		assert(!empty());
		// because poped elements can be equal with real_index
		// poped elements count is (virtual_index(real_index) + 1)
		size_type count = size_type(virtual_index(real_index)) + 1;
		assert(count <= size());
		while (count-- > 0)
		{
			pop_front();
		}
	}

	/**
	 @name 不是队列操作的成员
	  none queue operations.
	  只是为了功能扩展
	@{
	 */

	/**
	 @brief 在队列头部加入新元素

	 - 前条件：队列不满
	 - 操作结果：现存元素的序号不变，新元素 val 成为新的队头
	 */
	void push_front(const ElemT& val)
	{
		assert(!full());
		auto head = prev(m_head);
		new(static_cast<void*>(m_vec + index(head)))ElemT(val);
		m_head = head;
	}

	void push_front(ElemT&& val)
	{
		assert(!full());
		auto head = prev(m_head);
		new(static_cast<void*>(m_vec + index(head)))ElemT(std::move(val));
		m_head = head;
	}

	template<class... Args>
	void emplace_front(Args&&... args)
	{
		assert(!full());
		auto head = prev(m_head);
		new(static_cast<void*>(m_vec + index(head)))ElemT(std::forward<Args>(args)...);
		m_head = head;
	}

	//@{
	/** 返回队列尾部元素 前条件：队列不空 */
	ElemT& back() noexcept
	{
		assert(!empty());
		return m_vec[index(prev(m_tail))];
	}
	const ElemT& back() const noexcept
	{
		assert(!empty());
		return m_vec[index(prev(m_tail))];
	}
	//@}

	/**
	 @brief 弹出队列尾部元素

	 - 前条件：队列不空
	 */
	void pop_back(ElemT& val)
	{
		assert(!empty());
		val = std::move(back());
		pop_back();
	}

	/**
	 @brief 弹出队列尾部元素

	 - 前条件：队列不空
	 */
	void pop_back()
	{
		assert(!empty());
		m_tail = prev(m_tail);
		m_vec[index(m_tail)].~ElemT();
	}

	/**
	 @brief 弹出序列号比大于等于输入参数 real_index 的所有元素

	  pop elements which real_index is later or equal than real_index
	 - 前条件：队列不空，且参数 real_index 代表的元素必须在队列中
	 */
	void pop_upper(counter_type real_index)
	{
		assert(!empty());
		// because poped elements can be equal with real_index
		// if not include the equal one, count is (size() - virtual_index(real_index) - 1);
		size_type count = size() - size_type(virtual_index(real_index));
		assert(count <= size());
		while (count-- > 0)
		{
			pop_back();
		}
	}
	//@} // name 不是队列操作的成员

	/**
	 @name iterator 相关成员
	 @{
	 */
    iterator begin() noexcept { return iterator(this, m_head); }
    const_iterator begin() const noexcept { return const_iterator(this, m_head); }
    iterator end() noexcept { return iterator(this, m_tail); }
    const_iterator end() const noexcept { return const_iterator(this, m_tail); }

    // Real indices retain the wrap information; they are not physical slot indices.
    ptrdiff_t virtual_index(counter_type serial) const noexcept {
        counter_type offset = distance(m_head, serial);
        assert(offset <= size());
        return ptrdiff_t(offset);
    }

    counter_type real_index(ptrdiff_t offset) const noexcept {
        assert(offset >= 0 && counter_type(offset) <= size());
        return next_n(m_head, counter_type(offset));
    }

    counter_type head_real_index() const noexcept { return m_head; }
    counter_type tail_real_index() const noexcept { return m_tail; }

	/**
	 @brief 通过 virtual_index 取得元素
	 @{
	 */
    ElemT& operator[](ptrdiff_t offset) {
        assert(offset >= 0 && counter_type(offset) < size());
        return m_vec[index(real_index(offset))];
    }
    const ElemT& operator[](ptrdiff_t offset) const {
        assert(offset >= 0 && counter_type(offset) < size());
        return m_vec[index(real_index(offset))];
    }

	//@}
};

} // namespace terark
