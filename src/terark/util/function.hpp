/* vim: set tabstop=4 : */
#pragma once

#ifdef TERARK_FUNCTION_USE_BOOST
	#include <boost/function.hpp>
#else
	#include <functional>
#endif
#include <type_traits>

#include <terark/preproc.hpp>
#include <terark/stdtypes.hpp>
#include <boost/mpl/bool.hpp>
#include <boost/mpl/if.hpp>
#include <boost/noncopyable.hpp>
#include <boost/static_assert.hpp>
#include <boost/type_traits.hpp>
#include <boost/utility.hpp>

#if defined(TERARK_HAS_WEAK_SYMBOL) && 0
  #define TERARK_VALVEC_HAS_WEAK_SYMBOL 1
#endif

#if TERARK_VALVEC_HAS_WEAK_SYMBOL
extern "C" {
    // jemalloc specific
    size_t TERARK_WEAK_SYMBOL xallocx(void*, size_t, size_t, int flags);
  #if defined(__GLIBC__) && defined(__THROW)
    size_t TERARK_WEAK_SYMBOL malloc_usable_size(void*) __THROW;
  #else
    size_t TERARK_WEAK_SYMBOL malloc_usable_size(void*);
  #endif
};
#endif

namespace terark {

#ifdef TERARK_FUNCTION_USE_BOOST
    using boost::bind;
    using boost::function;
	using boost::ref;
	using boost::cref;
	using boost::reference_wrapper
	using boost::remove_reference;
#else
    using std::bind;
	using std::function;
	using std::ref;
	using std::cref;
	using std::reference_wrapper;
	using std::remove_reference;
#endif

    class TERARK_DLL_EXPORT CacheAlignedNewDelete {
    public:
        void* operator new(size_t);
        void* operator new(size_t, void* p) { return p; } // placement new
        void operator delete(void* p, size_t);
        void operator delete(void*, void*) { abort(); } // suppress warn
    };

    enum class MemType : unsigned char {
        Malloc,
        Mmap,
        User,
    };
    // defined in util/mmap.cpp
    TERARK_DLL_EXPORT void mmap_close(void* base, size_t size);

    template<class T>
    struct ParamPassType {
        static const bool is_pass_by_value =
// some fundamental types are larger than sizeof(void*) such as long double
        boost::is_fundamental<T>::value || (
                sizeof(T) <= sizeof(void*)
            && (sizeof(T) & (sizeof(T)-1 )) == 0 // power of 2
            && boost::has_trivial_copy<T>::value
            && boost::has_trivial_copy_constructor<T>::value
            && boost::has_trivial_destructor<T>::value
        );
        typedef typename boost::mpl::if_c<is_pass_by_value,
            const T, const T&>::type type;
    };

#if (defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus >= 201103L || \
	 defined(_MSC_VER) && _MSC_VER >= 1700) && 0
	template <typename T>
	class is_iterator_impl {
	  static char test(...);
	  template <typename U,
		typename=typename std::iterator_traits<U>::difference_type,
		typename=typename std::iterator_traits<U>::pointer,
		typename=typename std::iterator_traits<U>::reference,
		typename=typename std::iterator_traits<U>::value_type,
		typename=typename std::iterator_traits<U>::iterator_category
	  > static long test(U*);
	public:
	  static const bool value = sizeof(test((T*)(NULL))) == sizeof(long);
	};
#else
	template <typename T>
	class is_iterator_impl {
		static T makeT();
		static char test(...); // Common case
		template<class R> static typename R::iterator_category* test(R);
		template<class R> static void* test(R*); // Pointer
	public:
		static const bool value = sizeof(test(makeT())) == sizeof(void*);
	};
#endif
	template <typename T>
	struct is_iterator :
		public boost::mpl::bool_<is_iterator_impl<T>::value> {};

#if defined(_MSC_VER) || defined(_LIBCPP_VERSION)
	template<class T>
	void STDEXT_destroy_range_aux(T*, T*, boost::mpl::true_) {}
	template<class T>
	void STDEXT_destroy_range_aux(T* p, T* q, boost::mpl::false_) {
		for (; p < q; ++p) p->~T();
	}
	template<class T>
	void STDEXT_destroy_range(T* p, T* q) {
		STDEXT_destroy_range_aux(p, q, boost::has_trivial_destructor<T>());
	}
#else
	#define STDEXT_destroy_range std::_Destroy
#endif

template<class ForwardIt, class Size>
ForwardIt
always_uninitialized_default_construct_n_aux(ForwardIt first, Size n,
                                             std::false_type /*nothrow*/) {
    using T = typename std::iterator_traits<ForwardIt>::value_type;
    ForwardIt current = first;
    try {
        for (; n > 0 ; (void) ++current, --n) {
            ::new (static_cast<void*>(std::addressof(*current))) T ();
			// ------------------------------- init primitives  ---^^
			// std::uninitialized_default_construct_n will not init primitives
        }
        return current;
    }  catch (...) {
        STDEXT_destroy_range(first, current);
        throw;
    }
}

template<class ForwardIt, class Size>
ForwardIt
always_uninitialized_default_construct_n_aux(ForwardIt first, Size n,
                                             std::true_type /*nothrow*/) {
    using T = typename std::iterator_traits<ForwardIt>::value_type;
    ForwardIt current = first;
    for (; n > 0 ; (void) ++current, --n) {
        ::new (static_cast<void*>(std::addressof(*current))) T ();
        // ------------------------------- init primitives  ---^^
        // std::uninitialized_default_construct_n will not init primitives
    }
    return current;
}

template<class ForwardIt, class Size>
ForwardIt
always_uninitialized_default_construct_n(ForwardIt first, Size n) {
    using T = typename std::iterator_traits<ForwardIt>::value_type;
    return always_uninitialized_default_construct_n_aux(
            first, n, std::is_nothrow_default_constructible<T>());
}

template<class T>
inline bool is_object_overlap(const T* x, const T* y) {
	assert(NULL != x);
	assert(NULL != y);
	if (x+1 <= y || y+1 <= x)
		return false;
	else
		return true;
}

inline size_t larger_capacity(size_t oldcap) {
    // use ull for 32 bit platform
    typedef unsigned long long ull;
    return size_t(ull(oldcap) * 103 / 64); // 103/64 = 1.609375 <~ 1.618
}

/// alloc on stack by alloca or on heap by malloc
#define TERARK_FAST_ALLOC_EX(Type, Ptr, Num, MaxStackBytes) \
  Type* Ptr = terark_likely(Num <= (MaxStackBytes) / sizeof(Type)) \
            ? (Type*)alloca(sizeof(Type) * Num)  \
            : (Type*)malloc(sizeof(Type) * Num)

/// TERARK_FAST_ALLOC_EX and call default cons
#define TERARK_FAST_ARRAY_EX(Type, Ptr, Num, MaxStackBytes) \
        TERARK_FAST_ALLOC_EX(Type, Ptr, Num, MaxStackBytes); \
        for (size_t i = 0; i < Num; i++) new (Ptr + i) Type ()

/// destroy Num objects and free(Ptr) if Ptr was allocated by alloca,
/// destroy needs to know Num, but free need not to know bytes to be freed,
/// if Ptr is trivially destructable, std::destroy_n() just do nothing.
#define TERARK_FAST_CLEAN_EX(Ptr, NumCons, NumAlloc, MaxStackBytes) \
  std::destroy_n(Ptr, NumCons); \
  if (NumAlloc > (MaxStackBytes) / sizeof(*(Ptr))) ::free(Ptr)

#define TERARK_FAST_ALLOC(Type, Ptr, Num) \
        TERARK_FAST_ALLOC_EX(Type, Ptr, Num, 4096)

#define TERARK_FAST_ARRAY(Type, Ptr, Num) \
        TERARK_FAST_ARRAY_EX(Type, Ptr, Num, 4096)

#define TERARK_FAST_CLEAN(Ptr, NumCons, NumAlloc) \
        TERARK_FAST_CLEAN_EX(Ptr, NumCons, NumAlloc, 4096)

#if 1
template<class T>
struct terark_identity {
	typedef T value_type;
    //typedef typename ParamPassType<T>::type param_pass_t; // DONT use!
	const T& operator()(const T& x) const { return x; }
};
#else
	#define terark_identity std::identity
#endif

template<class Object, class Field>
class DynaOffsetGetField {
    int m_offset;
public:
    typedef typename ParamPassType<Field>::type param_pass_t;
	param_pass_t operator()(const Object& x) const {
		return *reinterpret_cast<const Field*>(
				reinterpret_cast<const char *>(&x) + m_offset);
	}
    explicit DynaOffsetGetField(int offset) : m_offset(offset) {}
};
#define TERARK_DynaOffsetGetField_Type(Object, field) \
    DynaOffsetGetField<Object, decltype(((Object*)nullptr)->field)>

#define TERARK_DynaOffsetGetField(Object, field) \
    DynaOffsetGetField<Object, decltype(((Object*)nullptr)->field)> \
           (int(offsetof(Object, field)))

/////////////////////////////////////////////////////////////////////////////

	template<class FuncProto>
	class tfunc : public function<FuncProto> {
		typedef function<FuncProto> super;
	public:
		using super::super;
		template<class Functor>
		tfunc(const Functor* f) : super(ref(*f)) {}
	};

    template<class Functor, class... ArgList>
    auto bind(Functor* f, ArgList&&... args)
    ->decltype(bind(ref(*f), std::forward<ArgList>(args)...)) {
        return bind(ref(*f), std::forward<ArgList>(args)...);
    }

	template<class Func>
	class OnScopeExit {
		const Func& on_exit;
	public:
		OnScopeExit(const Func& f) : on_exit(f) {}
		~OnScopeExit() { on_exit(); }
	};
#define TERARK_SCOPE_EXIT(...) \
    auto TERARK_PP_CAT2(func_on_exit_,__LINE__) = [&]() { __VA_ARGS__; }; \
    terark::OnScopeExit< \
decltype(TERARK_PP_CAT2(func_on_exit_,__LINE__))> \
         TERARK_PP_CAT2(call_on_exit_,__LINE__)   \
        (TERARK_PP_CAT2(func_on_exit_,__LINE__))

    template<class R, class... Args>
    using c_callback_fun_t = R (*)(void*, Args...);

    template<class Lambda>
    struct c_callback_t {
        template<class R, class... Args>
        static R invoke(void* vlamb, Args... args) {
            return (*(Lambda*)vlamb)(std::forward<Args>(args)...);
        }

        template<class R, class... Args>
        operator c_callback_fun_t<R, Args...>() const {
            return &c_callback_t::invoke<R, Args...>;
        }
    };
    template<class Lambda>
    c_callback_t<Lambda> c_callback(Lambda&) {
       return c_callback_t<Lambda>();
    }

    template<class MemFuncType, MemFuncType MemFunc>
    struct mf_callback_t {
        template<class R, class Obj, class... Args>
        static R invoke(void* self, Args... args) {
            return (((Obj*)self)->*MemFunc)(std::forward<Args>(args)...);
        }
        template<class R, class... Args>
        operator c_callback_fun_t<R, Args...>() const {
            return &mf_callback_t::invoke<R, Args...>;
        }
    };

// do not need to pack <This, MemFunc> as a struct
#define TERARK_MEM_FUNC(MemFunc)  \
    mf_callback_t<decltype(MemFunc), MemFunc>()

///@param lambda lambda obj
///@note  this yield two args
#define TERARK_C_CALLBACK(lambda) terark::c_callback(lambda), &lambda

//--------------------------------------------------------------------
// User/Application defined MemPool
class TERARK_DLL_EXPORT UserMemPool : boost::noncopyable {
    UserMemPool();
public:
    virtual ~UserMemPool();
    virtual void* alloc(size_t);
    virtual void* realloc(void*, size_t);
    virtual void  sfree(void*, size_t);
    static UserMemPool* SysMemPool();
};

#define TERARK_COMPARATOR_OP(Name, expr) \
    struct Name { \
        template<class T> \
        bool operator()(const T& x, const T& y) const { return expr; } \
    }
TERARK_COMPARATOR_OP(CmpLT,   x < y );
TERARK_COMPARATOR_OP(CmpGT,   y < x );
TERARK_COMPARATOR_OP(CmpLE, !(y < x));
TERARK_COMPARATOR_OP(CmpGE, !(x < y));
TERARK_COMPARATOR_OP(CmpEQ,   x ==y );
TERARK_COMPARATOR_OP(CmpNE, !(x ==y));

struct cmp_placeholder{};
namespace terark_placeholders {
constexpr cmp_placeholder _cmp;  // gcc warns for unused static var
}

template<class Pred>
struct NotPredT {
    template<class T>
    bool operator()(const T& x) const { return !pred(x); }
    Pred pred;
};

///{@
///@arg x is the free  arg
///@arg y is the bound arg
#define TERARK_BINDER_CMP_OP(BinderName, expr) \
    template<class T> \
    struct BinderName {  const T  y; \
        template<class U> \
        BinderName(U&&y1) : y(std::forward<U>(y1)) {} \
        bool operator()(const T& x) const { return expr; } \
    }; \
    template<class T> \
    struct BinderName<T*> {  const T* y; \
        BinderName(const T* y1) : y(y1) {} \
        bool operator()(const T* x) const { return expr; } \
    }; \
    template<class T> \
    struct BinderName<reference_wrapper<const T> > { \
        const T& y; \
        BinderName(const T& y1) : y(y1) {} \
        bool operator()(const T& x) const { return expr; } \
    }
TERARK_BINDER_CMP_OP(BinderLT,   x < y );
TERARK_BINDER_CMP_OP(BinderGT,   y < x );
TERARK_BINDER_CMP_OP(BinderLE, !(y < x));
TERARK_BINDER_CMP_OP(BinderGE, !(x < y));
TERARK_BINDER_CMP_OP(BinderEQ,   x ==y );
TERARK_BINDER_CMP_OP(BinderNE, !(x ==y));
///@}

template<class KeyExtractor>
struct ExtractorLessT {
	KeyExtractor ex;
	template<class T>
	bool operator()(const T& x, const T& y) const { return ex(x) < ex(y); }
};
template<class KeyExtractor>
ExtractorLessT<KeyExtractor>
ExtractorLess(KeyExtractor ex) { return ExtractorLessT<KeyExtractor>{ex}; }

template<class KeyExtractor>
struct ExtractorGreaterT {
	KeyExtractor ex;
	template<class T>
	bool operator()(const T& x, const T& y) const { return ex(y) < ex(x); }
};
template<class KeyExtractor>
ExtractorGreaterT<KeyExtractor>
ExtractorGreater(KeyExtractor ex) { return ExtractorGreaterT<KeyExtractor>{ex}; }

template<class KeyExtractor>
struct ExtractorEqualT {
	KeyExtractor ex;
	template<class T>
	bool operator()(const T& x, const T& y) const { return ex(x) == ex(y); }
};
template<class KeyExtractor>
ExtractorEqualT<KeyExtractor>
ExtractorEqual(KeyExtractor ex) { return ExtractorEqualT<KeyExtractor>{ex}; }

template<class KeyExtractor, class KeyComparator>
struct ExtractorComparatorT {
	template<class T>
	bool operator()(const T& x, const T& y) const {
		return keyCmp(keyEx(x), keyEx(y));
	}
	KeyExtractor  keyEx;
	KeyComparator keyCmp;
};
template<class KeyExtractor, class Comparator>
ExtractorComparatorT<KeyExtractor, Comparator>
ExtractorComparator(KeyExtractor ex, Comparator cmp) {
	return ExtractorComparatorT<KeyExtractor, Comparator>{ex, cmp};
}

template<class Extractor1, class Extractor2>
struct CombineExtractor {
    Extractor1 ex1;
    Extractor2 ex2;
    template<class T>
    auto operator()(const T& x) const -> decltype(ex2(ex1(x))) {
        return ex2(ex1(x));
    }
};
template<class Extractor1>
struct CombinableExtractorT {
    Extractor1 ex1;

    NotPredT<CombinableExtractorT> operator!() const {
        return NotPredT<CombinableExtractorT>{*this};
    }

    ///@{
    /// operator+ as combine operator
    template<class Extractor2>
    CombineExtractor<Extractor1, Extractor2>
    operator+(Extractor2&& ex2) const {
        return CombineExtractor<Extractor1, Extractor2>
                         {ex1, std::forward<Extractor2>(ex2)};
    }
    ///@}

    ///@{
    /// operator| combine a comparator: less, greator, equal...
    template<class Comparator>
    ExtractorComparatorT<Extractor1, Comparator>
    operator|(Comparator&& cmp) const {
        return ExtractorComparator(ex1, std::forward<Comparator>(cmp));
    }
    ///@}

#define TERARK_CMP_OP(Name, op) \
    ExtractorComparatorT<Extractor1, Name> \
    operator op(cmp_placeholder) const { \
        return ExtractorComparator(ex1, Name()); \
    }

    TERARK_CMP_OP(CmpLT, < )
    TERARK_CMP_OP(CmpGT, > )
    TERARK_CMP_OP(CmpLE, <=)
    TERARK_CMP_OP(CmpGE, >=)
    TERARK_CMP_OP(CmpEQ, ==)
    TERARK_CMP_OP(CmpNE, !=)


#define TERARK_COMBINE_BIND_OP(Name, op) \
    template<class T> \
    CombineExtractor<Extractor1, Name<typename remove_reference<T>::type> > \
    operator op(T&& y) const { \
        return \
    CombineExtractor<Extractor1, Name<typename remove_reference<T>::type> > \
        {ex1, {std::forward<T>(y)}}; } \
    \
    template<class T> \
    CombineExtractor<Extractor1, Name<reference_wrapper<const T> > > \
    operator op(reference_wrapper<const T> y) const { \
        return \
    CombineExtractor<Extractor1, Name<reference_wrapper<const T> > > \
        {ex1, {y.get()}}; }

    ///@{
    /// operators: <, >, <=, >=, ==, !=
    /// use bound operator as Extractor, Extractor is a transformer in this case
    TERARK_COMBINE_BIND_OP(BinderLT, < )
    TERARK_COMBINE_BIND_OP(BinderGT, > )
    TERARK_COMBINE_BIND_OP(BinderLE, <=)
    TERARK_COMBINE_BIND_OP(BinderGE, >=)
    TERARK_COMBINE_BIND_OP(BinderEQ, ==)
    TERARK_COMBINE_BIND_OP(BinderNE, !=)
    ///@}

    /// forward the extractor
    template<class T>
    auto operator()(const T& x) const -> decltype(ex1(x)) { return ex1(x); }
};
template<class Extractor1>
CombinableExtractorT<Extractor1>
CombinableExtractor(Extractor1&& ex1) {
    return CombinableExtractorT<Extractor1>{std::forward<Extractor1>(ex1)};
}


///@param __VA_ARGS__ can be ' .template some_member_func<1,2,3>()'
///                       or '->template some_member_func<1,2,3>()'
///@note '.' or '->' before field is required
///@note TERARK_GET() is identity operator
#define TERARK_GET(...) terark::CombinableExtractor( \
  [](const auto&__x_)->decltype(auto){return(__x_ __VA_ARGS__);})


#define TERARK_FIELD_O_0() __x_
#define TERARK_FIELD_P_0() __x_
#define TERARK_FIELD_O_1(field) __x_.field
#define TERARK_FIELD_P_1(field) __x_->field

///@param __VA_ARGS__ can NOT be 'template some_member_func<1,2,3>()'
///@note decltype(auto) is required, () on return is required
///@note TERARK_FIELD() is identity operator
///@note '.' or '->' can not before field name
#define TERARK_FIELD(...) [](const auto&__x_)->decltype(auto) { \
  return (TERARK_PP_VA_NAME(TERARK_FIELD_O_,__VA_ARGS__)(__VA_ARGS__)); }

#define TERARK_FIELD_P(...) [](const auto&__x_)->decltype(auto) { \
  return (TERARK_PP_VA_NAME(TERARK_FIELD_P_,__VA_ARGS__)(__VA_ARGS__)); }

///@{
///@param d '.' or '->'
///@param f field
///@param o order/operator, '<' or '>'
#define TERARK_CMP1(d,f,o) \
    if (__x_ d f o __y_ d f) return true; \
    if (__y_ d f o __x_ d f) return false;

#define TERARK_CMP_O_2(f,o)     return __x_.f o __y_.f;
#define TERARK_CMP_O_4(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_2(__VA_ARGS__)
#define TERARK_CMP_O_6(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_4(__VA_ARGS__)
#define TERARK_CMP_O_8(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_6(__VA_ARGS__)
#define TERARK_CMP_O_a(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_8(__VA_ARGS__)
#define TERARK_CMP_O_c(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_a(__VA_ARGS__)
#define TERARK_CMP_O_e(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_c(__VA_ARGS__)
#define TERARK_CMP_O_g(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_e(__VA_ARGS__)
#define TERARK_CMP_O_i(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_g(__VA_ARGS__)
#define TERARK_CMP_O_k(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_i(__VA_ARGS__)
#define TERARK_CMP_O_m(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_k(__VA_ARGS__)
#define TERARK_CMP_O_o(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_m(__VA_ARGS__)
#define TERARK_CMP_O_q(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_o(__VA_ARGS__)
#define TERARK_CMP_O_s(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_q(__VA_ARGS__)
#define TERARK_CMP_O_u(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_s(__VA_ARGS__)
#define TERARK_CMP_O_w(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_u(__VA_ARGS__)
#define TERARK_CMP_O_y(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_w(__VA_ARGS__)
#define TERARK_CMP_O_A(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_y(__VA_ARGS__)
#define TERARK_CMP_O_C(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_A(__VA_ARGS__)
#define TERARK_CMP_O_E(f,o,...) TERARK_CMP1(. ,f,o)TERARK_CMP_O_C(__VA_ARGS__)

#define TERARK_CMP_P_2(f,o)     return __x_->f o __y_->f;
#define TERARK_CMP_P_4(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_2(__VA_ARGS__)
#define TERARK_CMP_P_6(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_4(__VA_ARGS__)
#define TERARK_CMP_P_8(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_6(__VA_ARGS__)
#define TERARK_CMP_P_a(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_8(__VA_ARGS__)
#define TERARK_CMP_P_c(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_a(__VA_ARGS__)
#define TERARK_CMP_P_e(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_c(__VA_ARGS__)
#define TERARK_CMP_P_g(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_e(__VA_ARGS__)
#define TERARK_CMP_P_i(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_g(__VA_ARGS__)
#define TERARK_CMP_P_k(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_i(__VA_ARGS__)
#define TERARK_CMP_P_m(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_k(__VA_ARGS__)
#define TERARK_CMP_P_o(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_m(__VA_ARGS__)
#define TERARK_CMP_P_q(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_o(__VA_ARGS__)
#define TERARK_CMP_P_s(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_q(__VA_ARGS__)
#define TERARK_CMP_P_u(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_s(__VA_ARGS__)
#define TERARK_CMP_P_w(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_u(__VA_ARGS__)
#define TERARK_CMP_P_y(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_w(__VA_ARGS__)
#define TERARK_CMP_P_A(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_y(__VA_ARGS__)
#define TERARK_CMP_P_C(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_A(__VA_ARGS__)
#define TERARK_CMP_P_E(f,o,...) TERARK_CMP1(->,f,o)TERARK_CMP_P_C(__VA_ARGS__)
///@}

///@param __VA_ARGS__ at least 1 field
///@note max support 20 fields, sample usage: TERARK_CMP(f1,>,f2,<,f3,<)
#define TERARK_CMP(...) \
 [](const auto& __x_, const auto& __y_) ->bool { \
  TERARK_PP_VA_NAME(TERARK_CMP_O_,__VA_ARGS__)(__VA_ARGS__) }

#define TERARK_CMP_P(...) \
 [](const auto& __x_, const auto& __y_) ->bool { \
  TERARK_PP_VA_NAME(TERARK_CMP_P_,__VA_ARGS__)(__VA_ARGS__) }

#define TERARK_EQUAL_MAP(c,f) if (!(__x_ f == __y_ f)) return false;
#define TERARK_EQUAL_IMP(...) [](const auto& __x_, const auto& __y_) { \
  TERARK_PP_APPLY(TERARK_PP_JOIN, \
                  TERARK_PP_MAP(TERARK_EQUAL_MAP, ~, __VA_ARGS__)); \
  return true; }

///@param __VA_ARGS__ can not be empty
#define TERARK_EQUAL(...) TERARK_EQUAL_IMP( \
        TERARK_PP_MAP(TERARK_PP_PREPEND, ., __VA_ARGS__))

///@param __VA_ARGS__ can not be empty
#define TERARK_EQUAL_P(...) TERARK_EQUAL_IMP( \
        TERARK_PP_MAP(TERARK_PP_PREPEND, ->, __VA_ARGS__))

} // namespace terark

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template<class T>
inline typename std::enable_if<std::is_fundamental<T>::value, T>::type
SmartDataForPrintf(T x) { return x; }

template<class Seq>
inline auto SmartDataForPrintf(const Seq& s) -> decltype(s.data()) {
	 return s.data();
}
template<class StdException>
inline auto SmartDataForPrintf(const StdException& e) -> decltype(e.what()) {
	 return e.what();
}
template<class T>
inline const T* SmartDataForPrintf(const T* x) { return x; }

#define TERARK_PP_SmartForPrintf(...) \
    TERARK_PP_MAP(TERARK_PP_APPLY, SmartDataForPrintf, __VA_ARGS__)

/// suffix _S means Smart format
#define TERARK_DIE_S(fmt, ...) \
  do { \
    fprintf(stderr, "%s:%d: %s: die: " fmt " !\n", \
            __FILE__, __LINE__, \
            TERARK_PP_SmartForPrintf(BOOST_CURRENT_FUNCTION, ##__VA_ARGS__)); \
    abort(); } while (0)

/// VERIFY indicate runtime assert in release build
#define TERARK_VERIFY_S(expr, fmt, ...) \
  do { if (terark_unlikely(!(expr))) { \
    fprintf(stderr, "%s:%d: %s: verify(%s) failed: " fmt " !\n", \
            __FILE__, __LINE__, BOOST_CURRENT_FUNCTION, \
            TERARK_PP_SmartForPrintf(#expr, ##__VA_ARGS__)); \
    abort(); }} while (0)

#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
#define TERARK_ASSERT_S TERARK_VERIFY_S
#else
#define TERARK_ASSERT_S(...)
#endif

/// _S_: String
#define TERARK_ASSERT_S_LT(x,y) TERARK_ASSERT_S(x <  y, "%s -:- %s", x, y)
#define TERARK_ASSERT_S_GT(x,y) TERARK_ASSERT_S(x >  y, "%s -:- %s", x, y)
#define TERARK_ASSERT_S_LE(x,y) TERARK_ASSERT_S(x <= y, "%s -:- %s", x, y)
#define TERARK_ASSERT_S_GE(x,y) TERARK_ASSERT_S(x >= y, "%s -:- %s", x, y)
#define TERARK_ASSERT_S_EQ(x,y) TERARK_ASSERT_S(x == y, "%s -:- %s", x, y)
#define TERARK_ASSERT_S_NE(x,y) TERARK_ASSERT_S(x != y, "%s -:- %s", x, y)

#define TERARK_VERIFY_S_LT(x,y) TERARK_VERIFY_S(x <  y, "%s -:- %s", x, y)
#define TERARK_VERIFY_S_GT(x,y) TERARK_VERIFY_S(x >  y, "%s -:- %s", x, y)
#define TERARK_VERIFY_S_LE(x,y) TERARK_VERIFY_S(x <= y, "%s -:- %s", x, y)
#define TERARK_VERIFY_S_GE(x,y) TERARK_VERIFY_S(x >= y, "%s -:- %s", x, y)
#define TERARK_VERIFY_S_EQ(x,y) TERARK_VERIFY_S(x == y, "%s -:- %s", x, y)
#define TERARK_VERIFY_S_NE(x,y) TERARK_VERIFY_S(x != y, "%s -:- %s", x, y)
