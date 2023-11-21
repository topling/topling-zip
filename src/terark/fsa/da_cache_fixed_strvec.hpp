// Created by leipeng 2022-09-03 10:23
#pragma once

#include <memory>
#include <terark/valvec.hpp>
#include <terark/fstring.hpp>
#include <terark/util/sortable_strvec.hpp>

namespace terark {

// now just support FixedLenStrVec
class TERARK_DLL_EXPORT DaCacheFixedStrVec : public FixedLenStrVec {
protected:
    struct MyHeader;
    class MyAppendOnlyTrie;
    class MyDoubleArrayNode;
    class MyDoubleArrayTrie;
    MyDoubleArrayNode* m_da_data;
    size_t m_da_size;
    size_t m_da_free;
public:
    DaCacheFixedStrVec();
    virtual ~DaCacheFixedStrVec();

    bool has_cache() const { return nullptr != m_da_data; }
    size_t da_size() const { return m_da_size; }
    size_t da_free() const { return m_da_free; }
    void build_cache(size_t stop_range_len = 512);
    struct MatchStatus {
        size_t lo;
        size_t hi;
        size_t depth;
        size_t freq() const { return hi - lo; }
    };
    void sa_print_stat() const;

    MatchStatus match_max_length(const byte*, size_t len) const noexcept;
    MatchStatus match_max_length(fstring input) const noexcept {
        return match_max_length(input.udata(), input.size());
    }
    MatchStatus da_match(const byte*, size_t len) const noexcept;
    MatchStatus da_match(fstring input) const noexcept {
        return da_match(input.udata(), input.size());
    }

    MatchStatus da_match_with_hole(const byte*, size_t len, const uint16_t* holeMeta) const noexcept;
    MatchStatus da_match_with_hole(fstring input, const uint16_t* holeMeta) const noexcept {
        return da_match_with_hole(input.udata(), input.size(), holeMeta);
    }

    // memory layout, all data are in m_strpool
    //   0. header
    //   1. m_fixlen * m_size for strpool data
    //   2. padding to 16 bytes
    //   3. double array memory, aligned to 16 : sizeof(MyDoubleArrayNode) == 16
    /// @returns written bytes
    size_t save_mmap(std::function<void(const void*, size_t)> write) const;
    size_t size_mmap() const;
    void load_mmap(fstring mem) { load_mmap(mem.udata(), mem.size()); }
    void load_mmap(const byte_t* mem, size_t len);
    fstring da_memory() const noexcept;
};

} // namespace terark
