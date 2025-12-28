//
// Created by leipeng on 2020/9/10.
//
#ifdef _MSC_VER
#define _CRT_NONSTDC_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS
#endif

#include <terark/util/mmap.hpp>
#include <terark/util/profiling.hpp>
#include <terark/util/stat.hpp>
#include <terark/valvec.hpp>
#include <random>
#include <thread>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

using namespace terark;

void usage(const char* prog) {
	fprintf(stderr,
R"EOS(Usage: %s Options file_name
  Options:
    -t read threads
    -T write threads
    -r read op num per thread
    -m mmap read
    -w size of write file with random data
    -H huge page
    -h Show this help information
    -v Show verbose info
)EOS", prog);
	exit(1);
}

static void fill_rand(uint64_t* base, size_t fsize) {
    std::mt19937_64 rnd(fsize);
    for (size_t i = 0; i < fsize/(4096/8); i++) {
        uint64_t rv = rnd();
        for (size_t j = 0; j < 4096/8; ++j) base[j] = rv;
	base += 4096/8;
    }
}
static void w_thr_proc(size_t i, size_t tnum, void* base, size_t fsize) {
    size_t per_size = (fsize/4096/tnum) * 4096;
    size_t thr_size = i+1==tnum ? fsize - per_size*(tnum-1) : per_size;
    size_t thr_ppos = i * per_size;
    fprintf(stderr, "w_thr_proc: %3zd ppos = %7.3f GB size = %.3f GB\n",
            i, double(thr_ppos)/(1<<30), double(thr_size)/(1<<30));
    fill_rand((uint64_t*)((char*)base + thr_ppos), thr_size);
};


int main(int argc, char* argv[]) {
    size_t fsize = 0;
    size_t thr_num = 1;
    size_t w_thr_num = 1;
    size_t rd_num = 0;
    bool mmap_read = false;
    bool mmap_huge = false;
	for (;;) {
		int opt = getopt(argc, argv, "r:t:T:mw:hH");
		switch (opt) {
		case -1:
			goto GetoptDone;
        case 'r':
            rd_num = strtol(optarg, NULL, 10);
            break;
		case 'w':
            fsize = (size_t)terark::ParseSizeXiB(optarg);
            fsize = align_up(fsize, 4*1024);
            break;
        case 't':
            thr_num = strtoul(optarg, NULL, 10);
            break;
        case 'T':
            w_thr_num = strtoul(optarg, NULL, 10);
            break;
        case 'm':
            mmap_read = true;
            break;
		case 'v':
		//	verbose = true;
			break;
		case 'H':
		    mmap_huge = true;
		    break;
		case '?':
		case 'h':
		default:
			usage(argv[0]);
		}
	}
GetoptDone:
    const char* fname = NULL;
    if (optind >= argc) {
        usage(argv[0]);
    }
    fname = argv[optind];
    if (0 == rd_num) {
        rd_num = fsize / 4096;
    }
    if (0 == thr_num) {
        thr_num = 1;
    }
    if (0 == w_thr_num) {
        w_thr_num = 1;
    }
    profiling pf;
    if (fsize) {
        int fd = open(fname, O_RDWR|O_CREAT, 0446);
        if (fd < 0) {
            fprintf(stderr, "ERROR: open(%s, O_RDWR|O_CREAT, 0446) = %s\n",
                    fname, strerror(errno));
            return 1;
        }
        int err = ftruncate(fd, fsize);
        if (err < 0) {
            fprintf(stderr, "ERROR: ftruncate(%s, %zd) = %s\n",
                    fname, fsize, strerror(errno));
            return 1;
        }
    	terark::valvec<std::thread> w_thr_vec(w_thr_num-1, valvec_reserve());
        auto run = [&](void* base) {
            for (size_t i = 0; i < w_thr_num-1; ++i) {
                w_thr_vec.unchecked_emplace_back(&w_thr_proc, i, w_thr_num, base, fsize);
            }
	    w_thr_proc(w_thr_num-1, w_thr_num, base, fsize);
	    for (auto& t : w_thr_vec) t.join();
        };
        auto t0 = pf.now();
        if (mmap_huge) {
          #if BOOST_OS_WINDOWS
            #error TODO:
          #else
            // huge page is not supported on file backed shared mmap yet
            auto base = mmap(NULL, fsize, PROT_READ|PROT_WRITE,
                             MAP_SHARED, fd, 0);
            close(fd);
            if (MAP_FAILED == base) {
                fprintf(stderr, "mmap(%s, len=%zd, SHARED) = %s\n",
                        fname, fsize, strerror(errno));
                return 1;
            }
			run(base);
            munmap(base, fsize);
          #endif
        } else {
            close(fd);
            MmapWholeFile fm(fname, true);
            run(fm.base);
        }
        auto t1 = pf.now();
        fprintf(stderr,
                "write %s: fsize = %zd, time = %.3f sec, speed = %.3f MiB/sec\n",
                fname, fsize, pf.sf(t0,t1), fsize/pf.sf(t0,t1)/(1<<20));
    }
    int fd = open(fname, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "ERROR: open(%s, O_RDONLY) = %s\n",
                fname, strerror(errno));
        return 1;
    }
    {
        struct stat st; // NOLINT
        if (ll_fstat(fd, &st) < 0) {
            fprintf(stderr, "ERROR: fstat(%s) = %s\n", fname, strerror(errno));
            return 1;
        }
        if (fsize != size_t(st.st_size)) {
            fprintf(stderr, "INFO: cmd fsize = %zd, st_size(%s) = %zd, use st_size\n",
                    fsize, fname, size_t(st.st_size));
            fsize = size_t(st.st_size);
        }
    }
    size_t gsum = 0;
    terark::valvec<std::thread> thr_vec(thr_num-1, valvec_reserve());
    llong t0, t1;
    if (mmap_read) {
        MmapWholeFile fm(fname);
        madvise(fm.base, fm.size, MADV_RANDOM);
        auto rd_func = [&](size_t tno) {
            std::mt19937_64 rnd(tno + t0);
            size_t sum = 0;
            for(size_t i = 0, n = rd_num; i < n; ++i) {
                size_t pos = (rnd() % (fsize/4096)) * 4096;
                sum += ((byte_t*)fm.base)[pos];
            }
            gsum += sum;
        };
        t0 = pf.now();
        for (size_t i = 0; i < thr_num-1; ++i) {
            thr_vec.unchecked_emplace_back(rd_func, i);
        }
        rd_func(thr_num-1);
        for (auto& t : thr_vec) t.join();
        t1 = pf.now();
    }
    else {
      #if defined(POSIX_FADV_RANDOM)
        posix_fadvise(fd, 0, fsize, POSIX_FADV_RANDOM);
      #else
        fprintf(stderr, "WARN: posix_fadvise is not supported on the platform, ignored\n");
      #endif
        auto rd_func = [&](size_t tno) {
            std::mt19937_64 rnd(tno + t0);
            size_t sum = 0;
            for(size_t i = 0, n = rd_num; i < n; ++i) {
                size_t pos = (rnd() % (fsize/4096)) * 4096;
                byte_t b;
                if (1 == pread(fd, &b, 1, pos)) {
                    sum += b;
                } else {
                    fprintf(stderr, "ERROR: tno = %zd: pread(%s, %zd) = %s\n",
                            tno, fname, pos, strerror(errno));
                    exit(1);
                }
            }
            gsum += sum;
        };
        t0 = pf.now();
        for (size_t i = 0; i < thr_num-1; ++i) {
            thr_vec.emplace_back(rd_func, i);
        }
        rd_func(thr_num-1);
        for (auto& t : thr_vec) t.join();
        t1 = pf.now();
    }
    fprintf(stderr, "%zd threads, total read op = %zd, %sread = %f M op/sec\n",
            thr_num, rd_num*thr_num, mmap_read ? "mmap_" : "p",
            double(rd_num*thr_num)/pf.uf(t0,t1));

    close(fd);

    return 0;
}
