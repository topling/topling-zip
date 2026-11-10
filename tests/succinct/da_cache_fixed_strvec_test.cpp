#include <terark/fsa/da_cache_fixed_strvec.hpp>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if (defined(__unix__) || defined(__APPLE__)) && UINTPTR_MAX > UINT32_MAX
#include <sys/mman.h>
#endif

using namespace terark;

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

size_t LowerBound(const DaCacheFixedStrVec& keys, fstring query) {
    size_t lo = 0, hi = keys.size();
    if (keys.has_cache()) {
        auto match = keys.da_match(query);
        lo = match.lo;
        hi = match.hi;
        if (query.size() == match.depth) return lo;
    }
    lo = keys.lower_bound_prefix(lo, hi, query);
    if (lo < keys.size() && query.size() > keys.m_fixlen &&
        memcmp(keys.nth_data(lo), query.data(), keys.m_fixlen) == 0) {
        ++lo;
    }
    return lo;
}

void CheckQueries(const DaCacheFixedStrVec& keys, const std::vector<std::string>& expected) {
    auto check = [&](const std::string& query) {
        size_t rank = std::lower_bound(expected.begin(), expected.end(), query) - expected.begin();
        Check(LowerBound(keys, fstring(query)) == rank, "lower_bound differs from sorted keys");
        if (query.size() == keys.m_fixlen) {
            size_t lo = 0, hi = keys.size();
            if (keys.has_cache()) {
                auto match = keys.da_match(fstring(query));
                lo = match.lo;
                hi = match.hi;
            }
            size_t found = keys.lower_bound_fixed(lo, hi, query.data());
            Check(found == rank, "fixed-length lower_bound differs from sorted keys");
        }
    };
    check("");
    check(std::string(1, '\0'));
    check(std::string(1, char(255)));
    for (size_t i = 0; i < expected.size(); i += 37) {
        check(expected[i]);
        check(expected[i].substr(0, 3));
        check(expected[i].substr(0, 7));
        check(expected[i] + "x");
        std::string missing = expected[i];
        missing[5] = 1;
        check(missing);
    }
    check(expected.back());
}

std::vector<byte_t> Save(const DaCacheFixedStrVec& keys) {
    std::vector<byte_t> memory;
    memory.reserve(keys.size_mmap());
    keys.save_mmap([&](const void* data, size_t size) {
        const auto* first = static_cast<const byte_t*>(data);
        if (size) memory.insert(memory.end(), first, first + size);
    });
    Check(memory.size() == keys.size_mmap(), "serialized size mismatch");
    return memory;
}

void TestRoundTrip(size_t leaf_size, size_t padding = 0) {
    DaCacheFixedStrVec keys;
    keys.m_fixlen = 8;
    std::vector<std::string> expected;
    for (size_t i = 0; i < 2048; ++i) {
        std::string key("pq\0\0X\0zq", 8);
        key[2] = char(i >> 8);
        key[3] = char(i);
        expected.push_back(key);
        keys.push_back(fstring(key));
    }
    keys.optimize_func();
    if (leaf_size) keys.build_cache(leaf_size);
    Check(keys.has_cache() == (leaf_size != 0), "initial cache state mismatch");
    CheckQueries(keys, expected);
    auto memory = Save(keys);
    const size_t image_size = memory.size();
    memory.resize(image_size + padding, 0);
    {
        DaCacheFixedStrVec loaded;
        loaded.load_mmap(memory.data(), memory.size());
        Check(loaded.has_cache() == keys.has_cache(), "cache state changed after load");
        Check(loaded.da_size() == keys.da_size(), "DA node count changed after load");
        fstring resident = loaded.da_memory();
        auto begin = reinterpret_cast<uintptr_t>(memory.data());
        auto end = begin + memory.size();
        auto resident_begin = reinterpret_cast<uintptr_t>(resident.data());
        Check(resident_begin >= begin && resident_begin <= end,
              "resident span starts outside image");
        Check(resident.size() <= end - resident_begin, "resident span ends outside image");
        if (!leaf_size) {
            Check(resident.empty(), "disabled cache has resident nodes");
            Check(resident_begin == begin + image_size,
                  "empty DA span must follow serialized keys");
        }
        CheckQueries(loaded, expected);
    }
    printf("PASS: DA cache roundtrip, leaf_size=%zu padding=%zu\n", leaf_size, padding);
}

void TestLargePayload() {
#if (defined(__unix__) || defined(__APPLE__)) && UINTPTR_MAX > UINT32_MAX
    const size_t key_size = size_t(1) << 20;
    const size_t num_keys = 4096;
    const size_t payload_size = key_size * num_keys;
    const size_t mapping_size = payload_size + (1 << 20);
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    struct Mapping {
        byte_t* data;
        size_t size;
        ~Mapping() { if (data != MAP_FAILED) munmap(data, size); }
    } mapping{static_cast<byte_t*>(mmap(nullptr, mapping_size, PROT_READ | PROT_WRITE,
                                       flags, -1, 0)), mapping_size};
    if (mapping.data == MAP_FAILED) {
        printf("SKIP: 4 GiB sparse mapping unavailable: %s\n", strerror(errno));
        return;
    }
#ifdef MADV_NOHUGEPAGE
    madvise(mapping.data, mapping.size, MADV_NOHUGEPAGE);
#endif
    DaCacheFixedStrVec keys;
    const size_t header_size = keys.size_mmap();
    keys.m_fixlen = uint32_t(key_size);
    keys.m_size = num_keys;
    keys.m_strpool_mem_type = MemType::User;
    keys.m_strpool.risk_set_data(mapping.data + header_size);
    keys.m_strpool.risk_set_size(payload_size);
    keys.m_strpool.risk_set_capacity(payload_size);
    // Anonymous mmap supplies zero suffixes; only one page per key is written.
    for (size_t i = 0; i < num_keys; ++i) {
        byte_t* key = keys.mutable_nth_data(i);
        key[0] = byte_t(i >> 8);
        key[1] = byte_t(i);
    }
    keys.optimize_func();
    keys.build_cache(512);
    Check(keys.size_mmap() <= mapping.size, "sparse image exceeds mapping");
    size_t offset = 0;
    keys.save_mmap([&](const void* data, size_t size) {
        Check(size <= mapping.size - offset, "sparse serialization exceeds mapping");
        // Payload is already at its serialized position: do not fault in 4 GiB.
        if (size && data != mapping.data + offset) memcpy(mapping.data + offset, data, size);
        offset += size;
    });
    Check(offset == keys.size_mmap(), "sparse serialized size mismatch");
    const size_t ranks[] = {0, 256, num_keys - 1};
    for (size_t rank : ranks) {
        Check(LowerBound(keys, keys[rank]) == rank, "large lookup failed before load");
    }
    DaCacheFixedStrVec loaded;
    loaded.load_mmap(mapping.data, offset);
    Check(loaded.m_strpool.size() == payload_size, "4 GiB key payload length overflowed on load");
    Check(loaded.da_memory().data() == (const char*)mapping.data + header_size + payload_size,
          "DA offset does not follow the full 4 GiB payload");
    Check(loaded.has_cache(), "large loaded image lost its cache");
    for (size_t rank : ranks) {
        Check(LowerBound(loaded, keys[rank]) == rank, "large lookup failed after load");
    }
    printf("PASS: 4 GiB sparse key payload roundtrip\n");
#else
    printf("SKIP: 4 GiB sparse key payload requires 64-bit POSIX mmap\n");
#endif
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "small") && strcmp(argv[1], "large"))) {
        fprintf(stderr, "Usage: %s [small|large]\n", argv[0]);
        return 2;
    }
    if (argc == 1 || strcmp(argv[1], "large") != 0) {
        TestRoundTrip(0);
        TestRoundTrip(0, 64);
        TestRoundTrip(64);
        TestRoundTrip(512);
        TestRoundTrip(4096);
        TestRoundTrip(512, 64);
    }
    if (argc == 1 || strcmp(argv[1], "small") != 0) TestLargePayload();
    return 0;
}
