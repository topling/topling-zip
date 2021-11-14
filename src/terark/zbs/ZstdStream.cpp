#include "ZstdStream.hpp"
#include <algorithm>
#include <string.h>
#include <zstd/zstd.h>

namespace terark {

/*! CHECK
 * Check that the condition holds. If it doesn't print a message and die.
 */
#define CHECK(cond, ...)                     \
    do {                                     \
        if (!(cond)) {                       \
            fprintf(stderr,                  \
                "%s:%d CHECK(%s) failed: ",  \
                __FILE__,                    \
                __LINE__,                    \
                #cond);                      \
            fprintf(stderr, "" __VA_ARGS__); \
            fprintf(stderr, "\n");           \
            exit(1);                         \
        }                                    \
    } while (0)

/*! CHECK_ZSTD
 * Check the zstd error code and die if an error occurred after printing a
 * message.
 */
#define CHECK_ZSTD(fn, ...)                                      \
    do {                                                         \
        size_t const err = (fn);                                 \
        CHECK(!ZSTD_isError(err), "%s", ZSTD_getErrorName(err)); \
    } while (0)


class ZstdInputStream::Impl {
public:
    IInputStream* istream;
    ZSTD_DCtx* dctx;
    void* buffIn;
    size_t buffInSize;
    ZSTD_inBuffer input;
    void* buffOut;
    size_t buffOutSize;
    ZSTD_outBuffer output;
};

ZstdInputStream::ZstdInputStream(IInputStream* istream)
    : m_impl(new ZstdInputStream::Impl)
{
    assert(m_impl->istream != nullptr);
    m_impl->istream = istream;
    m_impl->dctx = ZSTD_createDCtx();
    CHECK(m_impl->dctx != NULL, "ZSTD_createDCtx() failed!");

    m_impl->buffInSize = ZSTD_DStreamInSize();
    m_impl->buffIn = malloc(m_impl->buffInSize);
    m_impl->input = { m_impl->buffIn, m_impl->buffInSize, m_impl->buffInSize };

    m_impl->buffOutSize = ZSTD_DStreamOutSize();
    m_impl->buffOut = malloc(m_impl->buffOutSize);
}

ZstdInputStream::~ZstdInputStream()
{
    ZSTD_freeDCtx(m_impl->dctx);
    free(m_impl->buffIn);
    free(m_impl->buffOut);
    delete m_impl;
}

void ZstdInputStream::resetIstream(IInputStream* istream)
{
    // only allow to reset the m_impl->istream when the previous m_impl->istream has been decompressed
    assert(eof() && istream != nullptr);
    m_impl->istream = istream;
    ZSTD_DCtx_reset(m_impl->dctx, ZSTD_reset_session_and_parameters);
    m_impl->input = { m_impl->buffIn, m_impl->buffInSize, m_impl->buffInSize };
}

size_t ZstdInputStream::read(void* buf, size_t size) throw()
{
    size_t const toRead = m_impl->buffInSize;
    size_t read;
    size_t lastRet = 0;
    int isEmpty = 1;
    size_t usable_out_size = size;

    while (usable_out_size > 0) {
        if (m_impl->input.pos < m_impl->input.size) {
            isEmpty = 0;
        } else if ((read = m_impl->istream->read(m_impl->buffIn, toRead))) {
            m_impl->input = { m_impl->buffIn, read, 0 };
            isEmpty = 0;
        } else {
            break;
        }

        while (usable_out_size > 0 && m_impl->input.pos < m_impl->input.size) {
            m_impl->output = { m_impl->buffOut, std::min(usable_out_size, m_impl->buffOutSize), 0 };
            size_t const ret = ZSTD_decompressStream(m_impl->dctx, &m_impl->output, &m_impl->input);
            CHECK_ZSTD(ret);
            memcpy((char*)buf + size - usable_out_size, m_impl->buffOut, m_impl->output.pos);
            usable_out_size -= m_impl->output.pos;
            lastRet = ret;
        }
    }

    if (isEmpty) {
        fprintf(stderr, "the instream is empty\n");
        exit(1);
    }
    if (usable_out_size > 0 && lastRet != 0) {
        fprintf(stderr, "EOF before end of stream: %zu\n", lastRet);
        exit(1);
    }
    return size - usable_out_size;
}

bool ZstdInputStream::eof() const
{
    return m_impl->istream->eof() && m_impl->input.pos >= m_impl->input.size;
}

///////////////////////////////////////////////////////

class ZstdOutputStream::Impl {
public:
    IOutputStream* ostream;
    ZSTD_CCtx* cctx;
    size_t cLevel; // compress level 1-19 (the lageset is 22)
    void* buffOut;
    size_t buffOutSize;
    ZSTD_outBuffer output;
};

ZstdOutputStream::ZstdOutputStream(IOutputStream* ostream)
    : m_impl(new ZstdOutputStream::Impl)
{
    assert(ostream != nullptr);
    m_impl->ostream = ostream;
    m_impl->cctx = ZSTD_createCCtx();
    CHECK(m_impl->cctx != NULL, "ZSTD_createCCtx() failed!");
    m_impl->cLevel = 3;

    m_impl->buffOutSize = ZSTD_DStreamOutSize();
    m_impl->buffOut = malloc(m_impl->buffOutSize);
}

ZstdOutputStream::~ZstdOutputStream()
{
    if (m_impl->ostream != nullptr) {
        close();
    }
    ZSTD_freeCCtx(m_impl->cctx);
    free(m_impl->buffOut);
    delete m_impl;
}


void ZstdOutputStream::setCLevel(size_t l) {
    m_impl->cLevel = l;
};

void ZstdOutputStream::resetOstream(IOutputStream* ostream)
{
    // only allow to reset the ostream when the previous ostream has been closed
    assert(m_impl->ostream == nullptr && ostream != nullptr);
    m_impl->ostream = ostream;
    ZSTD_CCtx_reset(m_impl->cctx, ZSTD_reset_session_and_parameters);
}

size_t ZstdOutputStream::write(const void* buf, size_t size) throw()
{
    assert(m_impl->ostream != nullptr);
    ZSTD_inBuffer input = { buf, size, 0 };
    size_t remaining;

    CHECK_ZSTD(ZSTD_CCtx_setParameter(m_impl->cctx, ZSTD_c_compressionLevel, m_impl->cLevel));

    do {
        m_impl->output = { m_impl->buffOut, m_impl->buffOutSize, 0 };
        remaining = ZSTD_compressStream2(m_impl->cctx, &m_impl->output, &input, ZSTD_e_continue);
        CHECK_ZSTD(remaining);
        m_impl->ostream->write(m_impl->buffOut, m_impl->output.pos);
    } while (remaining != 0);
    CHECK(input.pos == input.size,
        "Impossible: zstd only returns 0 when the input is completely consumed!");
    return input.pos;
}

void ZstdOutputStream::flush()
{
    assert(m_impl->ostream != nullptr);
    m_impl->output = { m_impl->buffOut, m_impl->buffOutSize, 0 };
    ZSTD_flushStream(m_impl->cctx, &m_impl->output);
    m_impl->ostream->write(m_impl->buffOut, m_impl->output.pos);
}

void ZstdOutputStream::close()
{
    assert(m_impl->ostream != nullptr);
    m_impl->output = { m_impl->buffOut, m_impl->buffOutSize, 0 };
    ZSTD_endStream(m_impl->cctx, &m_impl->output);
    m_impl->ostream->write(m_impl->buffOut, m_impl->output.pos);
    m_impl->ostream = nullptr;
}

} // namespace terark
