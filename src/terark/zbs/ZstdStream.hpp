#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#include <terark/io/IStream.hpp>
#include <terark/util/refcount.hpp>

namespace terark {

class TERARK_DLL_EXPORT ZstdInputStream : public IInputStream, public RefCounter {
    DECLARE_NONE_COPYABLE_CLASS(ZstdInputStream)

public:
    explicit ZstdInputStream(IInputStream*);
    ~ZstdInputStream();

    void resetIstream(IInputStream*);
    size_t read(void* buf, size_t size) throw();
    bool eof() const;

private:
    class Impl;
    Impl* m_impl;
};

class TERARK_DLL_EXPORT ZstdOutputStream : public IOutputStream, public RefCounter {
    DECLARE_NONE_COPYABLE_CLASS(ZstdOutputStream)

public:
    explicit ZstdOutputStream(IOutputStream*);
    ~ZstdOutputStream();

    void setCLevel(size_t l);
    void resetOstream(IOutputStream*);
    size_t write(const void* buf, size_t size) throw();
    void flush();
    void close();

private:
    class Impl;
    Impl* m_impl;
};

} // namespace terark