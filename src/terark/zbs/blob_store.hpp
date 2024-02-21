#pragma once
#include <terark/valvec.hpp>
#include <terark/fstring.hpp>
#include <terark/util/function.hpp>
#include <atomic>

namespace terark {

class LruReadonlyCache;

template<bool ZipOffset>
struct BlobStoreRecBuffer;

class TERARK_DLL_EXPORT BlobStore : public CacheAlignedNewDelete {
public:
    struct TERARK_DLL_EXPORT Dictionary {
        Dictionary();
        explicit Dictionary(fstring mem);
        Dictionary(fstring mem, uint64_t hash);
        Dictionary(fstring mem, uint64_t hash, bool verified);
        Dictionary(size_t size, uint64_t hash);

        fstring  memory;
        uint64_t xxhash;
        bool verified = true;
    };
    enum MemoryCloseType : uint8_t {
        Clear, MmapClose, RiskRelease
    };
    static BlobStore* load_from_mmap(fstring fpath, bool mmapPopulate);
    static BlobStore* load_from_user_memory(fstring dataMem);
    static BlobStore* load_from_user_memory(fstring dataMem, const Dictionary&);

    struct Block {
        fstring name;
        fstring data;
        Block(fstring n, fstring d) : name(n), data(d) {}
        Block() {}
    };

    virtual const char* name() const = 0;
    virtual void get_meta_blocks(valvec<Block>* blocks) const = 0;
    virtual void get_data_blocks(valvec<Block>* blocks) const = 0;
    virtual void detach_meta_blocks(const valvec<Block>& blocks) = 0;
    valvec<Block> get_meta_blocks() const;
    valvec<Block> get_data_blocks() const;

    BlobStore();
    virtual ~BlobStore();
    bool support_zero_copy() const { return m_supportZeroCopy; }
    size_t num_records() const { return m_numRecords; }
    uint64_t total_data_size() const { return m_unzipSize; }
    virtual size_t mem_size() const = 0;

#ifdef _MSC_VER
    #define BlobStoreDefinePMF(Ret, pmf, ...) typedef Ret (BlobStore::*pmf)(__VA_ARGS__) const
    #define BlobStoreInvokePMF(pmf, ...)          (this->*pmf)(__VA_ARGS__)
    #define BlobStoreInvokePMF_EX(pmf, self, ...) (self->*pmf)(__VA_ARGS__)
    #define BlobStoreStaticCastPMF(pmf, ...)           static_cast<pmf>(__VA_ARGS__)
    #define BlobStoreReinterpretCastPMF(pmf, ...) reinterpret_cast<pmf>(__VA_ARGS__)
#else
    #define BlobStoreDefinePMF(Ret, pmf, ...) typedef Ret (*pmf)(const BlobStore*, ##__VA_ARGS__); typedef Ret (BlobStore::*pmf##_mf)(__VA_ARGS__) const
    #define BlobStoreInvokePMF(pmf, ...)          this->pmf(this, ##__VA_ARGS__)
    #define BlobStoreInvokePMF_EX(pmf, self, ...) pmf(self, ##__VA_ARGS__)
    #define BlobStoreStaticCastPMF(pmf, ...) (pmf)(pmf##_mf)(__VA_ARGS__)
    #define BlobStoreReinterpretCastPMF(pmf, ...) (pmf)reinterpret_cast<pmf##_mf>(__VA_ARGS__)
#endif

    terark_forceinline
    void get_record_append(size_t recID, valvec<byte_t>* recData) const {
        BlobStoreInvokePMF(m_get_record_append, recID, recData);
    }
    terark_forceinline
    void get_record(size_t recID, valvec<byte_t>* recData) const {
        recData->erase_all();
        BlobStoreInvokePMF(m_get_record_append, recID, recData);
    }
    terark_forceinline
    valvec<byte_t> get_record(size_t recID) const {
        valvec<byte_t> recData;
        BlobStoreInvokePMF(m_get_record_append, recID, &recData);
        return recData;
    }

    terark_forceinline
    void get_record_append_fiber_vm_prefetch
            (size_t recID, valvec<byte_t>* recData) const {
        BlobStoreInvokePMF(m_get_record_append_fiber_vm_prefetch, recID, recData);
    }

    /// Used for optimizing Iterator
    struct CacheOffsets {
        valvec<byte_t> recData;
        size_t blockId = size_t(-1);
        size_t offsets[129]; // bind to SortedUintVec implementation
        // BlockSize can only be 64 or 128
        // offsets[BlockSize+1] store next block's first offset

        void invalidate_offsets_cache() noexcept { blockId = size_t(-1); }
    };
    terark_forceinline
    void get_record_append(size_t recID, CacheOffsets* co) const {
        BlobStoreInvokePMF(m_get_record_append_CacheOffsets, recID, co);
    }
    terark_forceinline
    void get_record(size_t recID, CacheOffsets* co) const {
        co->recData.erase_all();
        BlobStoreInvokePMF(m_get_record_append_CacheOffsets, recID, co);
    }
    terark_forceinline
    size_t get_zipped_size(size_t recID, CacheOffsets* co) const {
        return BlobStoreInvokePMF(m_get_zipped_size, recID, co);
    }

    virtual size_t lower_bound(size_t lo, size_t hi, fstring target, CacheOffsets* co) const;
    virtual size_t lower_bound(size_t lo, size_t hi, fstring target, valvec<byte_t>* recData) const;
    terark_forceinline
    bool is_offsets_zipped() const {
        return reinterpret_cast<get_record_append_func_t>
                             (m_get_record_append_CacheOffsets)
                          !=  m_get_record_append;
    }

    // fd is HANDLE for windows, so use type intptr_t
    // if cache is not NULL, fd is `fi` for cache
    terark_forceinline
    void pread_record_append(LruReadonlyCache* cache, intptr_t fi,
                             size_t baseOffset, size_t recID,
                             valvec<byte_t>* recData,
                             valvec<byte_t>* rdbuf) const {
        BlobStoreInvokePMF(m_pread_record_append, cache, fi, baseOffset, recID, recData, rdbuf);
    }
    void pread_record_append(LruReadonlyCache*, intptr_t fi,
                             size_t baseOffset, size_t recID,
                             valvec<byte_t>* recData) const;
    terark_forceinline
    void pread_record(LruReadonlyCache* cache, intptr_t fi,
                      size_t baseOffset, size_t recID,
                      valvec<byte_t>* recData,
                      valvec<byte_t>* rdbuf) const {
        recData->risk_set_size(0);
        BlobStoreInvokePMF(m_pread_record_append, cache, fi, baseOffset, recID, recData, rdbuf);
    }
    terark_forceinline
    void pread_record(LruReadonlyCache* cache, intptr_t fi,
                      size_t baseOffset, size_t recID,
                      valvec<byte_t>* recData) const {
        recData->risk_set_size(0);
        pread_record_append(cache, fi, baseOffset, recID, recData);
    }

    // pread_func_t should return rdbuf->data() by default, can return
    // other pointers if possible, such as BlobStoreLruCachePosRead
    typedef const byte_t* (*pread_func_t)(void* lambda, size_t offset, size_t len, valvec<byte_t>* rdbuf);
    terark_forceinline
    void fspread_record_append(pread_func_t fspread, void* lambda,
                               size_t baseOffset, size_t recID,
                               valvec<byte_t>* recData,
                               valvec<byte_t>* rdbuf) const {
        BlobStoreInvokePMF(m_fspread_record_append, fspread, lambda, baseOffset, recID, recData, rdbuf);
    }
    void fspread_record_append(pread_func_t fspread, void* lambda,
                               size_t baseOffset, size_t recID,
                               valvec<byte_t>* recData) const;
    terark_forceinline
    void fspread_record(pread_func_t fspread, void* lambda,
                        size_t baseOffset, size_t recID,
                        valvec<byte_t>* recData,
                        valvec<byte_t>* rdbuf) const {
        recData->risk_set_size(0);
        BlobStoreInvokePMF(m_fspread_record_append, fspread, lambda, baseOffset, recID, recData, rdbuf);
    }
    terark_forceinline
    void fspread_record(pread_func_t fspread, void* lambda,
                        size_t baseOffset, size_t recID,
                        valvec<byte_t>* recData) const {
        recData->risk_set_size(0);
        fspread_record_append(fspread, lambda, baseOffset, recID, recData);
    }

    bool is_mmap_aio() const { return m_mmap_aio; }
    void set_mmap_aio(bool mmap_aio) { m_mmap_aio = mmap_aio; }

    size_t min_prefetch_pages() const { return m_min_prefetch_pages; }
    void set_min_prefetch_pages(size_t val) { m_min_prefetch_pages = (uint16_t)val; }

    virtual Dictionary get_dict() const = 0;
    virtual fstring get_mmap() const = 0;
    virtual void init_from_memory(fstring dataMem, Dictionary dict) = 0;

protected:
    size_t      m_numRecords;
    uint64_t    m_unzipSize;
    bool        m_mmap_aio;
    bool        m_supportZeroCopy;
    uint16_t    m_min_prefetch_pages;
	std::atomic<int32_t> m_refcnt{0};

	friend void intrusive_ptr_add_ref(BlobStore* p) {
		assert(NULL != p);
		assert(p->m_refcnt >= 0);
		++p->m_refcnt;
	}
	friend void intrusive_ptr_release(BlobStore* p) {
		assert(NULL != p);
		assert(p->m_refcnt > 0);
		if (0 == --p->m_refcnt)
			delete p;
	}

    BlobStoreDefinePMF(void, get_record_append_func_t, size_t recID, valvec<byte_t>* recData);
    get_record_append_func_t m_get_record_append;
    get_record_append_func_t m_get_record_append_fiber_vm_prefetch;

    BlobStoreDefinePMF(void, get_record_append_CacheOffsets_func_t, size_t recID, CacheOffsets*);
    get_record_append_CacheOffsets_func_t m_get_record_append_CacheOffsets;

    BlobStoreDefinePMF(void, fspread_record_append_func_t,
                        pread_func_t,
                        void* lambdaObj,
                        size_t baseOffset,
                        size_t recID,
                        valvec<byte_t>* recData,
                        valvec<byte_t>* buf);
    fspread_record_append_func_t m_fspread_record_append;

    BlobStoreDefinePMF(void, pread_record_append_func_t,
                        LruReadonlyCache* cache,
                        intptr_t fd,
                        size_t baseOffset,
                        size_t recID,
                        valvec<byte_t>* recData,
                        valvec<byte_t>* buf);
    pread_record_append_func_t m_pread_record_append;

    BlobStoreDefinePMF(size_t, get_zipped_size_func_t, size_t recID, CacheOffsets*);
    get_zipped_size_func_t m_get_zipped_size;

    void pread_record_append_default_impl(
                        LruReadonlyCache* cache,
                        intptr_t fd,
                        size_t baseOffset,
                        size_t recID,
                        valvec<byte_t>* recData,
                        valvec<byte_t>* buf) const;

    static const byte_t* os_fspread(void* lambda, size_t offset, size_t len,
                                    valvec<byte_t>* rdbuf);
};

template<> struct BlobStoreRecBuffer<true> : BlobStore::CacheOffsets {
    const
    valvec<byte_t>& getRecData() const { return this->recData; }
    valvec<byte_t>& getRecData()       { return this->recData; }
};
template<> struct BlobStoreRecBuffer<false> : valvec<byte_t> {
    const
    valvec<byte_t>& getRecData() const { return *this; }
    valvec<byte_t>& getRecData()       { return *this; }
    void invalidate_offsets_cache() noexcept {} // do nothing
};

} // namespace terark

