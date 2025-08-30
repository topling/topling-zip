export SHELL=bash

AM_DEFAULT_VERBOSITY ?= 0

AM_V_GEN = $(am__v_GEN_$(V))
am__v_GEN_ = $(am__v_GEN_$(AM_DEFAULT_VERBOSITY))
am__v_GEN_0 = @echo "  GEN     " $@;
am__v_GEN_1 =

AM_V_at = $(am__v_at_$(V))
am__v_at_ = $(am__v_at_$(AM_DEFAULT_VERBOSITY))
am__v_at_0 = @
am__v_at_1 =

#CC_VARIANT_DIR = $(patsubst %/$(addsuffix .o,$(basename $<)),%,$@)
CC_VARIANT_DIR = $(BUILD_ROOT)/$(firstword $(subst /, ,$(patsubst $(BUILD_ROOT)/%,%,$@)))
LD_VARIANT_DIR = $(BUILD_ROOT)/$(firstword $(subst /, ,$(patsubst $(BUILD_ROOT)/%,%,$(firstword $(filter $(BUILD_ROOT)/%.o,$^)))))
AM_V_CC = ${AM_V_at}${TIME_CMD} -f "%e %S" -o >(printf '${CC_VARIANT_DIR} CC ${suffix $@} %6.2f %5.2f  %s\n' `cat` $<)
AM_V_LD = ${AM_V_at}${TIME_CMD} -f "%e %S" -o >(printf '${LD_VARIANT_DIR} LD so %6.2f %5.2f  %s\n' `cat` lib_shared/$(notdir $@))
AM_V_AR = ${AM_V_at}${TIME_CMD} -f "%e %S" -o >(printf '${LD_VARIANT_DIR} AR .a %6.2f %5.2f  %s\n' `cat` lib_static/$(notdir $@))

ifeq ($(USE_LTO),1)
  COMMON_C_FLAGS += -flto
  LDFLAGS += -flto=auto -fuse-linker-plugin
endif

#DBG_ASAN ?= -fsanitize=address
#AFR_ASAN ?= -fsanitize=address
#RLS_ASAN ?=

# default disable dwarf
DBG_DWARF ?=

DBG_FLAGS ?= -O0 -D_DEBUG ${DBG_DWARF} -g3 ${DBG_ASAN}
RLS_FLAGS ?= -O3 -DNDEBUG ${DBG_DWARF} -g3 ${RLS_ASAN}
# 'AFR' means Assert For Release
# gcc-12: -Og yield error: inlining failed in call to 'always_inline' 'ZSTD_HcFindBestMatch_selectMLS': function not considered for inlining
# gcc-12: -O1 is OK
#AFR_FLAGS ?= -Og ${DBG_DWARF} -g3 ${AFR_ASAN}
AFR_FLAGS ?= -O1 ${DBG_DWARF} -g3 ${AFR_ASAN}

MARCH ?= $(shell uname -m)
ifeq "${MARCH}" "x86_64"
WITH_BMI2 ?= $(shell bash ./cpu_has_bmi2.sh)
else
# not available
WITH_BMI2 ?= na
endif
CMAKE_INSTALL_PREFIX ?= /usr

BOOST_INC ?= -Iboost-include

export CHECK_TERARK_FSA_LIB_UPDATE=0

ifeq "$(origin LD)" "default"
  LD := ${CXX}
endif
#ifeq "$(origin CC)" "default"
#  CC := ${CXX}
#endif

ifndef COMPILER
# Makefile is stupid to parsing $(shell echo ')')
tmpfile := $(shell mktemp -u compiler-XXXXXX)
COMPILER := $(shell ${CXX} tools/configure/compiler.cpp -o ${tmpfile}.exe && ./${tmpfile}.exe && rm -f ${tmpfile}*)
#$(warning COMPILER=${COMPILER})
UNAME_MachineSystem := $(shell uname -m -s | sed 's:[ /]:-:g')
endif
BUILD_NAME := ${UNAME_MachineSystem}-${COMPILER}-bmi2-${WITH_BMI2}
BUILD_ROOT := build/${BUILD_NAME}
ddir:=${BUILD_ROOT}/dbg
rdir:=${BUILD_ROOT}/rls
adir:=${BUILD_ROOT}/afr

TERARK_ROOT:=${PWD}
COMMON_C_FLAGS  += -fdiagnostics-color
COMMON_C_FLAGS  += -Wformat=2 -Wcomment
COMMON_C_FLAGS  += -Wall -Wextra
COMMON_C_FLAGS  += -Wno-unused-parameter
#COMMON_C_FLAGS  += -Wno-alloc-size-larger-than # unrecognize

gen_sh := $(dir $(lastword ${MAKEFILE_LIST}))gen_env_conf.sh

err := $(shell env BOOST_INC=${BOOST_INC} bash ${gen_sh} "${CXX}" ${COMPILER} ${BUILD_ROOT}/env.mk; echo $$?)
ifneq "${err}" "0"
   $(error err = ${err} MAKEFILE_LIST = ${MAKEFILE_LIST}, PWD = ${PWD}, gen_sh = ${gen_sh} "${CXX}" ${COMPILER} ${BUILD_ROOT}/env.mk)
endif

TERARK_INC := -Isrc -I3rdparty/zstd ${BOOST_INC}

include ${BUILD_ROOT}/env.mk

UNAME_System ?= $(shell uname | sed 's/^\([0-9a-zA-Z]*\).*/\1/')
ifeq (CYGWIN, ${UNAME_System})
  FPIC =
  # lazy expansion
  CYGWIN_LDFLAGS = -Wl,--out-implib=$@ \
                   -Wl,--export-all-symbols \
                   -Wl,--enable-auto-import
  DLL_SUFFIX = .dll.a
  CYG_DLL_FILE = $(shell echo $@ | sed 's:\(.*\)/lib\([^/]*\)\.a$$:\1/cyg\2:')
  COMMON_C_FLAGS += -D_GNU_SOURCE
else
  ifeq (Darwin,${UNAME_System})
    DLL_SUFFIX = .dylib
	TIME_CMD ?= gtime
  else
    DLL_SUFFIX = .so
  endif
  FPIC = -fPIC
  CYG_DLL_FILE = $@
endif

TIME_CMD ?= /usr/bin/time

RPATH_FLAGS := -Wl,-rpath,'$$ORIGIN'

ifeq (Linux,${UNAME_System})
  # Allow users set -Iincdir (for liburing) in CXXFLAGS
  # Allow users set -Llibdir (for liburing) in LDFLAGS
  HAS_LIBURING := $(shell ${CXX} -x c - ${CXXFLAGS} ${LDFLAGS} -luring \
                    -o /dev/null <<< "int main(){return 0;}" \
                    2> /dev/null && echo 1)
  ifeq (${TOPLING_IO_WITH_URING},1)
    LINK_LIBURING := -luring
    CXXFLAGS += -DTOPLING_IO_WITH_URING=1
    ifneq (${HAS_LIBURING},1)
      $(warning force TOPLING_IO_WITH_URING=1 but liburing is not detected)
    endif
  else ifeq (${TOPLING_IO_WITH_URING},0)
    CXXFLAGS += -DTOPLING_IO_WITH_URING=0
    ifeq (${HAS_LIBURING},1)
      $(warning force TOPLING_IO_WITH_URING=0 but liburing is detected)
    endif
  else
    ifeq (${HAS_LIBURING},1)
      LINK_LIBURING := -luring
      CXXFLAGS += -DTOPLING_IO_WITH_URING=1
    else
      CXXFLAGS += -DTOPLING_IO_WITH_URING=0
      $(warning liburing is not detected)
    endif
  endif
endif

CFLAGS += ${FPIC}
CXXFLAGS += ${FPIC}
LDFLAGS += ${FPIC}

ASAN_LDFLAGS_a := ${AFR_ASAN}
ASAN_LDFLAGS_d := ${DBG_ASAN}
ASAN_LDFLAGS_r := ${RLS_ASAN}
ASAN_LDFLAGS = ${ASAN_LDFLAGS_$(patsubst %-a,a,$(patsubst %-d,d,$(@:%${DLL_SUFFIX}=%)))}
# ---------- ^-- lazy evaluation, must be '='

CXX_STD := -std=gnu++17

ifeq "$(shell a=${COMPILER};echo $${a:0:3})" "g++"
  ifeq (Linux, ${UNAME_System})
    LDFLAGS += -rdynamic
  endif
  ifeq (${UNAME_System},Darwin)
    COMMON_C_FLAGS += -Wa,-q
  endif
  ifeq "$(shell echo ${COMPILER} | awk -F- '{if ($$2 >= 9.0) print 1;}')" "1"
    COMMON_C_FLAGS += -Wno-alloc-size-larger-than
  endif
  ifeq "${MARCH}" "x86_64"
    COMMON_C_FLAGS += -mcx16
  endif
  CXXFLAGS += -Wno-class-memaccess
endif

# icc or icpc
ifeq "$(shell a=${COMPILER};echo $${a:0:2})" "ic"
  CXXFLAGS += -xHost -fasm-blocks
  CPU ?= -xHost
else
  ifeq "${MARCH}" "x86_64"
    ifeq (${WITH_BMI2},1)
      CPU ?= -march=haswell
    endif
  endif
  COMMON_C_FLAGS  += -Wno-deprecated-declarations
  ifeq "$(shell a=${COMPILER};echo $${a:0:5})" "clang"
    COMMON_C_FLAGS  += -Wno-deprecated-builtins
    COMMON_C_FLAGS  += -Wno-unused-but-set-variable
    COMMON_C_FLAGS  += -fstrict-aliasing
  else
    COMMON_C_FLAGS  += -Wstrict-aliasing=3
  endif
endif

ifeq (${WITH_BMI2},1)
  CPU += -mbmi -mbmi2
else ifeq (${WITH_BMI2},0)
  CPU += -mno-bmi -mno-bmi2
endif

ifeq (${TOPLING_USE_DYNAMIC_TLS},1)
  DEFS += -DTOPLING_USE_DYNAMIC_TLS
else
  ARG_TLS_MODEL := -ftls-model=initial-exec
endif

ifndef DISABLE_JEMALLOC
ifeq ($(shell ${CXX} -std=c89 -w -x c - -ljemalloc <<< 'main(){mallocx(8,0);}' >> /dev/null && echo 1),1)
  DISABLE_JEMALLOC ?= 0
else
  DISABLE_JEMALLOC ?= 1
endif
endif # DISABLE_JEMALLOC
ifeq (${DISABLE_JEMALLOC},0)
  # -ljemalloc should be the first
  LIBS := -ljemalloc ${LIBS}
else
  DEFS += -DTOPLING_DISABLE_JEMALLOC
endif
ifneq (${WITH_TBB},)
  COMMON_C_FLAGS += -DTERARK_WITH_TBB=${WITH_TBB}
  LIBS += -ltbb
endif

COMMON_C_FLAGS  += ${ARG_TLS_MODEL}

ifeq "$(shell a=${COMPILER};echo $${a:0:5})" "clang"
  COMMON_C_FLAGS += -fcolor-diagnostics
  #LDFLAGS += -latomic
endif

#CXXFLAGS +=
#CXXFLAGS += -fpermissive
#CXXFLAGS += -fexceptions
#CXXFLAGS += -fdump-translation-unit -fdump-class-hierarchy

CFLAGS += ${COMMON_C_FLAGS}
CXXFLAGS += ${COMMON_C_FLAGS}
#$(error ${CXXFLAGS} "----" ${COMMON_C_FLAGS})

DEFS += -DDIVSUFSORT_API=
CFLAGS   += ${DEFS}
CXXFLAGS += ${DEFS}

INCS := ${TERARK_INC} ${INCS}

LIBBOOST ?=
#LIBBOOST += -lboost_thread${BOOST_SUFFIX}
#LIBBOOST += -lboost_date_time${BOOST_SUFFIX}
#LIBBOOST += -lboost_system${BOOST_SUFFIX}

#LIBS += -ldl
#LIBS += -lpthread
#LIBS += ${LIBBOOST}

#extf = -pie
extf = -fno-stack-protector
#extf+=-fno-stack-protector-all
CFLAGS += ${extf}
#CFLAGS += -g3
CXXFLAGS += ${extf}
#CXXFLAGS += -g3
#CXXFLAGS += -fnothrow-opt

ifeq (, ${prefix})
  ifeq (root, ${USER})
    prefix := /usr
  else
    prefix := /home/${USER}
  endif
endif

ifeq ($(filter install, ${MAKECMDGOALS}),install)
  ifneq ($(wildcard ${prefix}/lib64),)
    INSTALL_DIR :=  ${prefix}/lib64
  else
    INSTALL_DIR :=  ${prefix}/lib
  endif
  ifeq ($(wildcard  ${INSTALL_DIR}),)
    $(error not exists: INSTALL_DIR = ${INSTALL_DIR})
  endif
endif

#$(warning prefix=${prefix} LIBS=${LIBS})

#obsoleted_src =  \
#    $(wildcard src/obsoleted/terark/thread/*.cpp) \
#    $(wildcard src/obsoleted/terark/thread/posix/*.cpp) \
#    $(wildcard src/obsoleted/wordseg/*.cpp)
#LIBS += -liconv

ifneq "$(shell a=${COMPILER};echo $${a:0:5})" "clang"
  LIBS += -lgomp
endif

c_src := \
   $(wildcard src/terark/c/*.c) \
   $(wildcard src/terark/c/*.cpp)

trbxx_src := src/terark/trb_cxx.cpp

zip_src := \
    src/terark/io/BzipStream.cpp \
    src/terark/io/GzipStream.cpp

rpc_src := \
   $(wildcard src/terark/inet/*.cpp) \
   $(wildcard src/terark/rpc/*.cpp)

core_src := \
   $(wildcard src/terark/*.cpp) \
   $(wildcard src/terark/io/*.cpp) \
   $(wildcard src/terark/util/*.cpp) \
   $(wildcard src/terark/thread/*.cpp) \
   $(wildcard src/terark/succinct/*.cpp) \
   ${obsoleted_src}

ifeq (CYGWIN, ${UNAME_System})
  core_src := $(filter-out src/terark/lru_map.cpp \
                           src/terark/util/process.cpp \
                , ${core_src})
endif

ifeq (${TOPLING_DISABLE_FIBER_AIO},1)
  core_src := $(filter-out src/terark/thread/fiber_%, ${core_src})
  CXXFLAGS += -DTOPLING_PIPELINE_WITH_FIBER=0
endif

core_src := $(filter-out ${trbxx_src} ${zip_src}, ${core_src})

fsa_src := $(wildcard src/terark/fsa/*.cpp)
fsa_src += $(wildcard src/terark/zsrch/*.cpp)

zbs_src := $(wildcard src/terark/entropy/*.cpp)
zbs_src += $(wildcard src/terark/zbs/*.cpp)

#idx_src := $(wildcard src/terark/idx/*.cpp)
idx_src := $(wildcard src/terark/idx/idx_dummy_placeholder.cpp)

zstd_src := $(wildcard 3rdparty/zstd/zstd/common/*.c)
zstd_src += $(wildcard 3rdparty/zstd/zstd/compress/*.c)
zstd_src += $(wildcard 3rdparty/zstd/zstd/decompress/*.c)
zstd_src += $(wildcard 3rdparty/zstd/zstd/deprecated/*.c)
zstd_src += $(wildcard 3rdparty/zstd/zstd/dictBuilder/*.c)
zstd_src += $(wildcard 3rdparty/zstd/zstd/legacy/*.c)

zbs_src += ${zstd_src}

#function definition
#@param:${1} -- targets var prefix, such as core | fsa | zbs | idx
#@param:${2} -- build type: d | r | a
objs = $(addprefix ${${2}dir}/, $(addsuffix .o, $(basename ${${1}_src}))) \
       ${${2}dir}/${${2}dir}/git-version-${1}.o

zstd_d_o := $(call objs,zstd,d)
zstd_r_o := $(call objs,zstd,r)
zstd_a_o := $(call objs,zstd,a)

core_d_o := $(call objs,core,d)
core_r_o := $(call objs,core,r)
core_a_o := $(call objs,core,a)
shared_core_d := ${BUILD_ROOT}/lib_shared/libterark-core-${COMPILER}-d${DLL_SUFFIX}
shared_core_r := ${BUILD_ROOT}/lib_shared/libterark-core-${COMPILER}-r${DLL_SUFFIX}
shared_core_a := ${BUILD_ROOT}/lib_shared/libterark-core-${COMPILER}-a${DLL_SUFFIX}
static_core_d := ${BUILD_ROOT}/lib_static/libterark-core-${COMPILER}-d.a
static_core_r := ${BUILD_ROOT}/lib_static/libterark-core-${COMPILER}-r.a
static_core_a := ${BUILD_ROOT}/lib_static/libterark-core-${COMPILER}-a.a

fsa_d_o := $(call objs,fsa,d)
fsa_r_o := $(call objs,fsa,r)
fsa_a_o := $(call objs,fsa,a)
shared_fsa_d := ${BUILD_ROOT}/lib_shared/libterark-fsa-${COMPILER}-d${DLL_SUFFIX}
shared_fsa_r := ${BUILD_ROOT}/lib_shared/libterark-fsa-${COMPILER}-r${DLL_SUFFIX}
shared_fsa_a := ${BUILD_ROOT}/lib_shared/libterark-fsa-${COMPILER}-a${DLL_SUFFIX}
static_fsa_d := ${BUILD_ROOT}/lib_static/libterark-fsa-${COMPILER}-d.a
static_fsa_r := ${BUILD_ROOT}/lib_static/libterark-fsa-${COMPILER}-r.a
static_fsa_a := ${BUILD_ROOT}/lib_static/libterark-fsa-${COMPILER}-a.a

zbs_d_o := $(call objs,zbs,d)
zbs_r_o := $(call objs,zbs,r)
zbs_a_o := $(call objs,zbs,a)
shared_zbs_d := ${BUILD_ROOT}/lib_shared/libterark-zbs-${COMPILER}-d${DLL_SUFFIX}
shared_zbs_r := ${BUILD_ROOT}/lib_shared/libterark-zbs-${COMPILER}-r${DLL_SUFFIX}
shared_zbs_a := ${BUILD_ROOT}/lib_shared/libterark-zbs-${COMPILER}-a${DLL_SUFFIX}
static_zbs_d := ${BUILD_ROOT}/lib_static/libterark-zbs-${COMPILER}-d.a
static_zbs_r := ${BUILD_ROOT}/lib_static/libterark-zbs-${COMPILER}-r.a
static_zbs_a := ${BUILD_ROOT}/lib_static/libterark-zbs-${COMPILER}-a.a

idx_d_o := $(call objs,idx,d)
idx_r_o := $(call objs,idx,r)
idx_a_o := $(call objs,idx,a)
shared_idx_d := ${BUILD_ROOT}/lib_shared/libterark-idx-${COMPILER}-d${DLL_SUFFIX}
shared_idx_r := ${BUILD_ROOT}/lib_shared/libterark-idx-${COMPILER}-r${DLL_SUFFIX}
shared_idx_a := ${BUILD_ROOT}/lib_shared/libterark-idx-${COMPILER}-a${DLL_SUFFIX}
static_idx_d := ${BUILD_ROOT}/lib_static/libterark-idx-${COMPILER}-d.a
static_idx_r := ${BUILD_ROOT}/lib_static/libterark-idx-${COMPILER}-r.a
static_idx_a := ${BUILD_ROOT}/lib_static/libterark-idx-${COMPILER}-a.a

rpc_d_o := $(call objs,rpc,d)
rpc_r_o := $(call objs,rpc,r)
rpc_a_o := $(call objs,rpc,a)
shared_rpc_d := ${BUILD_ROOT}/lib_shared/libterark-rpc-${COMPILER}-d${DLL_SUFFIX}
shared_rpc_r := ${BUILD_ROOT}/lib_shared/libterark-rpc-${COMPILER}-r${DLL_SUFFIX}
shared_rpc_a := ${BUILD_ROOT}/lib_shared/libterark-rpc-${COMPILER}-a${DLL_SUFFIX}
static_rpc_d := ${BUILD_ROOT}/lib_static/libterark-rpc-${COMPILER}-d.a
static_rpc_r := ${BUILD_ROOT}/lib_static/libterark-rpc-${COMPILER}-r.a
static_rpc_a := ${BUILD_ROOT}/lib_static/libterark-rpc-${COMPILER}-a.a

core := ${shared_core_d} ${shared_core_r} ${shared_core_a} ${static_core_d} ${static_core_r} ${static_core_a}
fsa  := ${shared_fsa_d}  ${shared_fsa_r}  ${shared_fsa_a}  ${static_fsa_d}  ${static_fsa_r}  ${static_fsa_a}
zbs  := ${shared_zbs_d}  ${shared_zbs_r}  ${shared_zbs_a}  ${static_zbs_d}  ${static_zbs_r}  ${static_zbs_a}
idx  := ${shared_idx_d}  ${shared_idx_r}  ${shared_idx_a}  ${static_idx_d}  ${static_idx_r}  ${static_idx_a}
rpc  := ${shared_rpc_d}  ${shared_rpc_r}  ${shared_rpc_a}  ${static_rpc_d}  ${static_rpc_r}  ${static_rpc_a}

ALL_TARGETS = ${MAYBE_DBB_DBG} ${MAYBE_DBB_RLS} ${MAYBE_DBB_AFR} core fsa zbs idx rpc tools
DBG_TARGETS = ${MAYBE_DBB_DBG} ${shared_core_d} ${shared_fsa_d} ${shared_zbs_d} ${shared_idx_d} ${shared_rpc_d}
RLS_TARGETS = ${MAYBE_DBB_RLS} ${shared_core_r} ${shared_fsa_r} ${shared_zbs_r} ${shared_idx_r} ${shared_rpc_r}
AFR_TARGETS = ${MAYBE_DBB_AFR} ${shared_core_a} ${shared_fsa_a} ${shared_zbs_a} ${shared_idx_a} ${shared_rpc_a}
ifeq (${PKG_WITH_STATIC},1)
  DBG_TARGETS += ${static_core_d} ${static_fsa_d} ${static_zbs_d} ${static_idx_d} ${static_rpc_d}
  RLS_TARGETS += ${static_core_r} ${static_fsa_r} ${static_zbs_r} ${static_idx_r} ${static_rpc_r}
  AFR_TARGETS += ${static_core_a} ${static_fsa_a} ${static_zbs_a} ${static_idx_a} ${static_rpc_a}
endif

ifeq (${TERARK_BIN_USE_STATIC_LIB},1)
  TERARK_BIN_DEP_LIB := ${static_core_d} ${static_fsa_d} ${static_zbs_d} ${static_idx_d}
else
  TERARK_BIN_DEP_LIB := ${shared_core_d} ${shared_fsa_d} ${shared_zbs_d} ${shared_idx_d}
endif

.PHONY : default all core fsa zbs idx rpc

default : core fsa zbs idx tools
all : ${ALL_TARGETS}
core: ${core}
fsa: ${fsa}
zbs: ${zbs}
idx: ${idx}
rpc: ${rpc}

OpenSources := $(shell find -H src 3rdparty -name '*.h' -o -name '*.hpp' -o -name '*.cc' -o -name '*.cpp' -o -name '*.c')
ObfuseFiles := \
    src/terark/fsa/fsa_cache_detail.hpp \
    src/terark/fsa/nest_louds_trie.cpp \
    src/terark/fsa/nest_louds_trie.hpp \
    src/terark/fsa/nest_louds_trie_inline.hpp \
    src/terark/zbs/dict_zip_blob_store.cpp \
    src/terark/zbs/suffix_array_dict.cpp

NotObfuseFiles := $(filter-out ${ObfuseFiles}, ${OpenSources})

allsrc = ${core_src} ${fsa_src} ${zbs_src} ${idx_src} ${rpc_src}
alldep = $(addprefix ${rdir}/, $(addsuffix .d, $(basename ${allsrc}))) \
         $(addprefix ${adir}/, $(addsuffix .d, $(basename ${allsrc}))) \
         $(addprefix ${ddir}/, $(addsuffix .d, $(basename ${allsrc})))

.PHONY : dbg rls afr
dbg: ${DBG_TARGETS}
rls: ${RLS_TARGETS}
afr: ${AFR_TARGETS}

.PHONY: obfuscate
obfuscate: $(addprefix ../obfuscated-terark/, ${ObfuseFiles})
	mkdir -p               ../obfuscated-terark/tools
	cp -rf tools/configure ../obfuscated-terark/tools
	cp -rf *.sh ../obfuscated-terark
	@for f in `find 3rdparty -name 'Makefile*'` \
				Makefile ${NotObfuseFiles}; \
	do \
		dir=`dirname ../obfuscated-terark/$$f`; \
		mkdir -p $$dir; \
		echo cp -a $$f $$dir; \
		cp -a $$f $$dir; \
	done

../obfuscated-terark/%: % tools/codegen/fuck_bom_out.exe
	@mkdir -p $(dir $@)
	tools/codegen/fuck_bom_out.exe < $< | perl ./obfuscate.pl > $@

tools/codegen/fuck_bom_out.exe: tools/codegen/fuck_bom_out.cpp
	g++ -o $@ $<

#${core_d} ${core_r} : LIBS += -lz -lbz2 -lrt
ifneq ($(patsubst android%,android,${UNAME_System}),android)
ifneq (${UNAME_System},Darwin)
${shared_core_d} ${shared_core_r} ${shared_core_a} : LIBS += -lrt -lpthread
ifneq (${UNAME_System},CYGWIN)
${shared_core_d} ${shared_core_r} ${shared_core_a} : LIBS += -laio ${LINK_LIBURING}
endif
endif
endif

${shared_core_d} : LIBS := $(filter-out -lterark-core-${COMPILER}-d, ${LIBS})
${shared_core_r} : LIBS := $(filter-out -lterark-core-${COMPILER}-r, ${LIBS})
${shared_core_a} : LIBS := $(filter-out -lterark-core-${COMPILER}-a, ${LIBS})

${shared_fsa_d} : LIBS := $(filter-out -lterark-fsa-${COMPILER}-d, -L${BUILD_ROOT}/lib_shared -lterark-core-${COMPILER}-d ${LIBS})
${shared_fsa_r} : LIBS := $(filter-out -lterark-fsa-${COMPILER}-r, -L${BUILD_ROOT}/lib_shared -lterark-core-${COMPILER}-r ${LIBS})
${shared_fsa_a} : LIBS := $(filter-out -lterark-fsa-${COMPILER}-a, -L${BUILD_ROOT}/lib_shared -lterark-core-${COMPILER}-a ${LIBS})

${shared_zbs_d} : LIBS := -L${BUILD_ROOT}/lib_shared -lterark-fsa-${COMPILER}-d -lterark-core-${COMPILER}-d ${LIBS}
${shared_zbs_r} : LIBS := -L${BUILD_ROOT}/lib_shared -lterark-fsa-${COMPILER}-r -lterark-core-${COMPILER}-r ${LIBS}
${shared_zbs_a} : LIBS := -L${BUILD_ROOT}/lib_shared -lterark-fsa-${COMPILER}-a -lterark-core-${COMPILER}-a ${LIBS}

${shared_idx_d} : LIBS := -L${BUILD_ROOT}/lib_shared -lterark-zbs-${COMPILER}-d -lterark-fsa-${COMPILER}-d -lterark-core-${COMPILER}-d ${LIBS}
${shared_idx_r} : LIBS := -L${BUILD_ROOT}/lib_shared -lterark-zbs-${COMPILER}-r -lterark-fsa-${COMPILER}-r -lterark-core-${COMPILER}-r ${LIBS}
${shared_idx_a} : LIBS := -L${BUILD_ROOT}/lib_shared -lterark-zbs-${COMPILER}-a -lterark-fsa-${COMPILER}-a -lterark-core-${COMPILER}-a ${LIBS}

${shared_rpc_d} : LIBS := -L${BUILD_ROOT}/lib_shared -lterark-core-${COMPILER}-d ${LIBS} -lpthread
${shared_rpc_r} : LIBS := -L${BUILD_ROOT}/lib_shared -lterark-core-${COMPILER}-r ${LIBS} -lpthread
${shared_rpc_a} : LIBS := -L${BUILD_ROOT}/lib_shared -lterark-core-${COMPILER}-a ${LIBS} -lpthread

${zstd_d_o} ${zstd_r_o} ${zstd_a_o} : CFLAGS   += -Wno-sign-compare -Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-uninitialized -I3rdparty/zstd/zstd -I3rdparty/zstd/zstd/common -Wno-ignored-qualifiers

${shared_fsa_d} : $(call objs,fsa,d) ${shared_core_d}
${shared_fsa_r} : $(call objs,fsa,r) ${shared_core_r}
${shared_fsa_a} : $(call objs,fsa,a) ${shared_core_a}
${static_fsa_d} : $(call objs,fsa,d)
${static_fsa_r} : $(call objs,fsa,r)
${static_fsa_a} : $(call objs,fsa,a)

${shared_zbs_d} : $(call objs,zbs,d) ${shared_fsa_d} ${shared_core_d}
${shared_zbs_r} : $(call objs,zbs,r) ${shared_fsa_r} ${shared_core_r}
${shared_zbs_a} : $(call objs,zbs,a) ${shared_fsa_a} ${shared_core_a}
${static_zbs_d} : $(call objs,zbs,d)
${static_zbs_r} : $(call objs,zbs,r)
${static_zbs_a} : $(call objs,zbs,a)

${shared_idx_d} : $(call objs,idx,d) ${shared_zbs_d} ${shared_fsa_d} ${shared_core_d}
${shared_idx_r} : $(call objs,idx,r) ${shared_zbs_r} ${shared_fsa_r} ${shared_core_r}
${shared_idx_a} : $(call objs,idx,a) ${shared_zbs_a} ${shared_fsa_a} ${shared_core_a}
${static_idx_d} : $(call objs,idx,d)
${static_idx_r} : $(call objs,idx,r)
${static_idx_a} : $(call objs,idx,a)

${shared_rpc_d} : $(call objs,rpc,d) ${shared_core_d}
${shared_rpc_r} : $(call objs,rpc,r) ${shared_core_r}
${shared_rpc_a} : $(call objs,rpc,a) ${shared_core_a}
${static_rpc_d} : $(call objs,rpc,d)
${static_rpc_r} : $(call objs,rpc,r)
${static_rpc_a} : $(call objs,rpc,a)

ifneq ($(patsubst android%,android,${UNAME_System}),android)
${shared_core_d}:${core_d_o} 3rdparty/base64/lib/libbase64.o ${ddir}/boost-shared/build.done
${shared_core_r}:${core_r_o} 3rdparty/base64/lib/libbase64.o ${rdir}/boost-shared/build.done
${shared_core_a}:${core_a_o} 3rdparty/base64/lib/libbase64.o ${rdir}/boost-shared/build.done
${static_core_d}:${core_d_o} 3rdparty/base64/lib/libbase64.o ${ddir}/boost-static/build.done
${static_core_r}:${core_r_o} 3rdparty/base64/lib/libbase64.o ${rdir}/boost-static/build.done
${static_core_a}:${core_a_o} 3rdparty/base64/lib/libbase64.o ${rdir}/boost-static/build.done
endif

${shared_core_d}: BOOST_BUILD_DIR := ${ddir}/boost-shared
${shared_core_r}: BOOST_BUILD_DIR := ${rdir}/boost-shared
${shared_core_a}: BOOST_BUILD_DIR := ${rdir}/boost-shared

${static_core_d}: BOOST_BUILD_DIR := ${ddir}/boost-static
${static_core_r}: BOOST_BUILD_DIR := ${rdir}/boost-static
${static_core_a}: BOOST_BUILD_DIR := ${rdir}/boost-static

# must use '=' for lazy evaluation, do not use ':='
THIS_LIB_OBJS = $(sort $(filter %.o,$^) \
  $(shell if [ -n "${BOOST_BUILD_DIR}" ]; then \
             find "${BOOST_BUILD_DIR}/bin.v2/libs" -name '*.o' \
       -not -path "${BOOST_BUILD_DIR}/bin.v2/*/config/*" \
       -not -path "${BOOST_BUILD_DIR}/bin.v2/libs/config/*"; fi))

define GenGitVersionSRC
${1}/git-version-core.cpp: GIT_PATH_ARG := ':!'{src/terark,tools}/fsa ':!'{src/terark,tools}/zbs
${1}/git-version-fsa.cpp:  GIT_PATH_ARG :=     {src/terark,tools}/fsa
${1}/git-version-zbs.cpp:  GIT_PATH_ARG :=                                {src/terark,tools}/zbs
${1}/git-version-core.cpp: ${core_src}
${1}/git-version-fsa.cpp: ${fsa_src} ${core_src}
${1}/git-version-zbs.cpp: ${zbs_src} ${fsa_src} ${core_src}
${1}/git-version-%.cpp: Makefile
	$${AM_V_GEN} mkdir -p $$(dir $$@)
	$${AM_V_at}rm -f $$@.tmp
	@echo -n '__attribute__ ((visibility ("default"))) const char*' \
		  'git_version_hash_info_'$$(patsubst git-version-%.cpp,%,$$(notdir $$@))\
		  '() { return R"StrLiteral(git_version_hash_info_is:' > $$@.tmp
	@env LC_ALL=C git log -n1 $${GIT_PATH_ARG} >> $$@.tmp
	@echo GIT_PATH_ARG = $${GIT_PATH_ARG}  >> $$@.tmp
	@env LC_ALL=C git diff $${GIT_PATH_ARG} >> $$@.tmp
	@env LC_ALL=C $$(CXX) --version >> $$@.tmp
	@echo INCS = $${INCS}           >> $$@.tmp
	@echo DEFS = $$(filter -D%,$${CXXFLAGS})  >> $$@.tmp
	@echo CXXFLAGS = $$(filter-out -W% -D%,$${CXXFLAGS})  >> $$@.tmp
	@echo WARNINGS = $$(filter -W%,$${CXXFLAGS})  >> $$@.tmp
	@echo ${2} >> $$@.tmp # DBG_FLAGS | RLS_FLAGS | AFR_FLAGS
	@echo WITH_BMI2 = ${WITH_BMI2} >> $$@.tmp
	@#echo WITH_TBB  = ${WITH_TBB}  >> $$@.tmp
	@echo compile_cpu_flag: $(CPU) >> $$@.tmp
	@#echo machine_cpu_flag: Begin  >> $$@.tmp
	@#bash ./cpu_features.sh        >> $$@.tmp
	@#echo machine_cpu_flag: End    >> $$@.tmp
	@echo LDFLAGS = $${LDFLAGS}    >> $$@.tmp
	@echo ')''StrLiteral";}' >> $$@.tmp
	@#      ^^----- To prevent diff causing git-version compile fail
	@if test -f "$$@" && cmp "$$@" $$@.tmp; then \
		rm $$@.tmp; \
	else \
		mv $$@.tmp $$@; \
	fi
endef

$(eval $(call GenGitVersionSRC, ${ddir}, "DBG_FLAGS = ${DBG_FLAGS}"))
$(eval $(call GenGitVersionSRC, ${rdir}, "RLS_FLAGS = ${RLS_FLAGS}"))
$(eval $(call GenGitVersionSRC, ${adir}, "AFR_FLAGS = ${AFR_FLAGS}"))

3rdparty/base64/lib/libbase64.o:
	$(MAKE) -C 3rdparty/base64 clean; \
	$(MAKE) -C 3rdparty/base64 lib/libbase64.o \
		CFLAGS="-fPIC -std=c99 -O3 -Wall -Wextra -pedantic"
		#AVX2_CFLAGS=-mavx2 SSE41_CFLAGS=-msse4.1 SSE42_CFLAGS=-msse4.2 AVX_CFLAGS=-mavx

ifeq (Darwin,${UNAME_System})
  # use clonefile
  CP_FAST = cp -X
  USER_GCC = echo use default CXX
else
  # use hard link as possible
  CP_FAST = cp -l
  USER_GCC = echo "using gcc : : ${CXX} ;" > tools/build/src/user-config.jam
endif

${rdir}/boost-static/build.done:
	@rm -rf $(dir $@)
	@mkdir -p $(dir $@)
ifneq (${TOPLING_DISABLE_FIBER_AIO},1)
	${AM_V_at}cd $(dir $@) \
	 && ln -s -f ../../../../boost-include/*     . && rm -f tools bin.v2 \
	 && ${CP_FAST} -r ../../../../boost-include/tools . \
	 && ${USER_GCC} \
	 && env CC=${CC} CXX=${CXX} bash bootstrap.sh --with-libraries=fiber,context,system \
	 && env CC=${CC} CXX=${CXX} ./b2 cxxflags="-fPIC -std=gnu++17 ${DBG_DWARF} -g3 ${ARG_TLS_MODEL} -Wno-deprecated-builtins" \
		                     -j8   cflags="-fPIC ${DBG_DWARF} -g3" threading=multi link=static variant=release
endif
	touch $@
${rdir}/boost-shared/build.done:
	@rm -rf $(dir $@)
	@mkdir -p $(dir $@)
ifneq (${TOPLING_DISABLE_FIBER_AIO},1)
	${AM_V_at}cd $(dir $@) \
	 && ln -s -f ../../../../boost-include/*     . && rm -f tools bin.v2 \
	 && ${CP_FAST} -r ../../../../boost-include/tools . \
	 && ${USER_GCC} \
	 && env CC=${CC} CXX=${CXX} bash bootstrap.sh --with-libraries=fiber,context,system \
	 && env CC=${CC} CXX=${CXX} ./b2 cxxflags="-fPIC -std=gnu++17 ${ARG_TLS_MODEL} -Wno-deprecated-builtins" \
		                     -j8   cflags="-fPIC" threading=multi link=shared variant=release
endif
	touch $@
${ddir}/boost-static/build.done:
	@rm -rf $(dir $@)
	@mkdir -p $(dir $@)
ifneq (${TOPLING_DISABLE_FIBER_AIO},1)
	${AM_V_at}cd $(dir $@) \
	 && ln -s -f ../../../../boost-include/*     . && rm -f tools bin.v2 \
	 && ${CP_FAST} -r ../../../../boost-include/tools . \
	 && ${USER_GCC} \
	 && env CC=${CC} CXX=${CXX} bash bootstrap.sh --with-libraries=fiber,context,system \
	 && env CC=${CC} CXX=${CXX} ./b2 cxxflags="-fPIC -std=gnu++17 -Wno-deprecated-builtins" \
		                     -j8   cflags="-fPIC" threading=multi link=static variant=debug
endif
	touch $@
${ddir}/boost-shared/build.done:
	@rm -rf $(dir $@)
	@mkdir -p $(dir $@)
ifneq (${TOPLING_DISABLE_FIBER_AIO},1)
	${AM_V_at}cd $(dir $@) \
	 && ln -s -f ../../../../boost-include/*     . && rm -f tools bin.v2 \
	 && ${CP_FAST} -r ../../../../boost-include/tools . \
	 && ${USER_GCC} \
	 && env CC=${CC} CXX=${CXX} bash bootstrap.sh --with-libraries=fiber,context,system \
	 && env CC=${CC} CXX=${CXX} ./b2 cxxflags="-fPIC -std=gnu++17 -Wno-deprecated-builtins" \
		                     -j8   cflags="-fPIC" threading=multi link=shared variant=debug
endif
	touch $@

%${DLL_SUFFIX}:
ifeq (${V},1)
	@echo "----------------------------------------------------------------------------------"
	@echo "Creating shared library: $@"
	@echo BOOST_INC=${BOOST_INC} BOOST_SUFFIX=${BOOST_SUFFIX} BOOST_BUILD_DIR=${BOOST_BUILD_DIR}
	@echo -e "OBJS:" $(addprefix "\n  ",${THIS_LIB_OBJS})
	@echo -e "LIBS:" $(addprefix "\n  ",${LIBS})
	@echo ASAN_LDFLAGS = ${ASAN_LDFLAGS}
endif
	@mkdir -p ${BUILD_ROOT}/lib_shared
ifeq (Darwin, ${UNAME_System})
	@cd ${BUILD_ROOT}; ln -sfh lib_shared lib
else
	@cd ${BUILD_ROOT}; ln -sfT lib_shared lib
endif
	@rm -f $@
	${AM_V_LD} ${LD} -shared ${THIS_LIB_OBJS} ${ASAN_LDFLAGS} ${LIBS} ${RPATH_FLAGS} -o ${CYG_DLL_FILE} ${CYGWIN_LDFLAGS} ${LDFLAGS}
	${AM_V_at}cd $(dir $@); ln -sf $(notdir $@) $(subst -${COMPILER},,$(notdir $@))
ifeq (CYGWIN, ${UNAME_System})
	@cp -l -f ${CYG_DLL_FILE} /usr/bin
endif

%.a:
ifeq (${V},1)
	@echo "----------------------------------------------------------------------------------"
	@echo "Creating static library: $@"
	@echo BOOST_INC=${BOOST_INC} BOOST_SUFFIX=${BOOST_SUFFIX} BOOST_BUILD_DIR=${BOOST_BUILD_DIR}
	@echo -e "OBJS:" $(addprefix "\n  ",${THIS_LIB_OBJS})
	@echo -e "LIBS:" $(addprefix "\n  ",${LIBS})
endif
	@mkdir -p $(dir $@)
	@rm -f $@
	${AM_V_AR} ${AR} rcs $@ ${THIS_LIB_OBJS}
	${AM_V_at}cd $(dir $@); ln -sf $(notdir $@) $(subst -${COMPILER},,$(notdir $@))

.PHONY : install
install : zbs fsa core
	cp -ap ${BUILD_ROOT}/lib_shared/* ${INSTALL_DIR}

.PHONY : clean
clean:
	@for f in `find * -name "*${BUILD_NAME}*"`; do \
		echo rm -rf $${f}; \
		rm -rf $${f}; \
	done

.PHONY : cleanall
cleanall: clean
	@for f in `find build tools tests gtests -name build`; do \
		echo rm -rf $${f}; \
		rm -rf $${f}; \
	done
	rm -rf pkg

.PHONY : depends
depends : ${alldep}

.PHONY : tools
tools : ${BUILD_ROOT}/done.tools
${BUILD_ROOT}/done.tools: ${fsa} ${zbs} \
                          $(wildcard tools/zbs/*.cpp) \
                          $(wildcard tools/fsa/*.cpp) \
                          $(wildcard tools/general/*.cpp)
	+${MAKE} CHECK_TERARK_FSA_LIB_UPDATE=0 -C tools/zbs
	+${MAKE} CHECK_TERARK_FSA_LIB_UPDATE=0 -C tools/fsa
	+${MAKE} CHECK_TERARK_FSA_LIB_UPDATE=0 -C tools/general
	touch $@

.PHONY : fuse
fuse: ${fsa} ${core}
	+${MAKE} CHECK_TERARK_FSA_LIB_UPDATE=0 -C tools/fuse_proxy

TarBallBaseName := terark-fsa_all-${BUILD_NAME}
TarBall := pkg/${TarBallBaseName}

PKG_WITH_EXE ?= 1
ifeq (${PKG_WITH_EXE},1)
  PKG_EXE_TARGETS := ${BUILD_ROOT}/done.tools
endif

.PHONY : pkg
.PHONY : tgz
pkg : ${TarBall}
tgz : ${TarBall}.tgz
scp : ${TarBall}.tgz.scp.done
oss : ${TarBall}.tgz.oss.done
${TarBall}.tgz.scp.done : ${TarBall}.tgz
	scp -P 22    $< root@115.29.173.226:/var/www/html/download/
	touch $@
${TarBall}.tgz.oss.done : ${TarBall}.tgz
ifeq (${REVISION},)
	$(error var REVISION must be defined for target oss)
endif
	ossutil.sh cp $< oss://terark-downloads/tools/${REVISION}/$(notdir $<) -f
	touch $@

${TarBall}: $(wildcard tools/general/*.cpp) \
            $(wildcard tools/fsa/*.cpp) \
            $(wildcard tools/zbs/*.cpp) \
            ${core} ${fsa} ${zbs} ${idx} \
            ${PKG_EXE_TARGETS}
	+${MAKE} CHECK_TERARK_FSA_LIB_UPDATE=0 -C tools/fsa
	+${MAKE} CHECK_TERARK_FSA_LIB_UPDATE=0 -C tools/zbs
	+${MAKE} CHECK_TERARK_FSA_LIB_UPDATE=0 -C tools/general
	rm -rf ${TarBall}
	mkdir -p ${TarBall}/bin
	mkdir -p ${TarBall}/lib_shared
	cd ${TarBall};ln -s lib_shared lib
ifeq (${PKG_WITH_SRC},1)
	mkdir -p ${TarBall}/include/terark/entropy
	mkdir -p ${TarBall}/include/terark/idx
	mkdir -p ${TarBall}/include/terark/thread
	mkdir -p ${TarBall}/include/terark/succinct
	mkdir -p ${TarBall}/include/terark/io/win
	mkdir -p ${TarBall}/include/terark/util
	mkdir -p ${TarBall}/include/terark/fsa
	mkdir -p ${TarBall}/include/terark/fsa/ppi
	mkdir -p ${TarBall}/include/terark/zbs
	mkdir -p ${TarBall}/include/zstd/common
	cp    src/terark/bits_rotate.hpp             ${TarBall}/include/terark
	cp    src/terark/bitfield_array.hpp          ${TarBall}/include/terark
	cp    src/terark/bitfield_array_access.hpp   ${TarBall}/include/terark
	cp    src/terark/bitmanip.hpp                ${TarBall}/include/terark
	cp    src/terark/bitmap.hpp                  ${TarBall}/include/terark
	cp    src/terark/config.hpp                  ${TarBall}/include/terark
	cp    src/terark/cxx_features.hpp            ${TarBall}/include/terark
	cp    src/terark/fstring.hpp                 ${TarBall}/include/terark
	cp    src/terark/histogram.hpp               ${TarBall}/include/terark
	cp    src/terark/int_vector.hpp              ${TarBall}/include/terark
	cp    src/terark/lcast.hpp                   ${TarBall}/include/terark
	cp    src/terark/*hash*.hpp                  ${TarBall}/include/terark
	cp    src/terark/heap_ext.hpp                ${TarBall}/include/terark
	cp    src/terark/mempool*.hpp                ${TarBall}/include/terark
	cp    src/terark/node_layout.hpp             ${TarBall}/include/terark
	cp    src/terark/num_to_str.hpp              ${TarBall}/include/terark
	cp    src/terark/objbox.hpp                  ${TarBall}/include/terark
	cp    src/terark/ptree.hpp                   ${TarBall}/include/terark
	cp    src/terark/parallel_lib.hpp            ${TarBall}/include/terark
	cp    src/terark/pass_by_value.hpp           ${TarBall}/include/terark
	cp    src/terark/rank_select.hpp             ${TarBall}/include/terark
	cp    src/terark/stdtypes.hpp                ${TarBall}/include/terark
	cp    src/terark/valvec.hpp                  ${TarBall}/include/terark
	cp    src/terark/entropy/*.hpp               ${TarBall}/include/terark/entropy
	cp    src/terark/idx/*.hpp                   ${TarBall}/include/terark/idx
	cp    src/terark/io/*.hpp                    ${TarBall}/include/terark/io
	cp    src/terark/io/win/*.hpp                ${TarBall}/include/terark/io/win
	cp    src/terark/util/*.hpp                  ${TarBall}/include/terark/util
	cp    src/terark/fsa/*.hpp                   ${TarBall}/include/terark/fsa
	cp    src/terark/fsa/*.inl                   ${TarBall}/include/terark/fsa
	cp    src/terark/fsa/ppi/*.hpp               ${TarBall}/include/terark/fsa/ppi
	cp    src/terark/zbs/*.hpp                   ${TarBall}/include/terark/zbs
	cp    src/terark/thread/*.hpp                ${TarBall}/include/terark/thread
	cp    src/terark/succinct/*.hpp              ${TarBall}/include/terark/succinct
	cp    3rdparty/zstd/zstd/*.h                 ${TarBall}/include/zstd
	cp    3rdparty/zstd/zstd/common/*.h          ${TarBall}/include/zstd/common
ifeq (${WITH_MRE_MATCH_API},1)
	mkdir -p ${TarBall}/include/terark/fsa
	cp    src/terark/fsa/mre_match.hpp           ${TarBall}/include/terark/fsa
endif
endif # PKG_WITH_SRC
ifeq (${PKG_WITH_EXE},1)
  ifeq "$(shell uname -r | grep -i Microsoft)" "0"
  endif
	mkdir -p ${TarBall}/zsrch/bin
	mkdir -p ${TarBall}/zsrch/lib
  ifneq (CYGWIN, ${UNAME_System})
	#cp -H tools/log_search/rls/log_indexer.exe ${TarBall}/zsrch/bin
	#cp -H tools/log_search/rls/log_realtime.exe ${TarBall}/zsrch/bin
	#cp -H tools/log_search/rls/default_log_parser.so ${TarBall}/zsrch/lib
	#cp -H tools/log_search/default_log_parser.conf ${TarBall}/zsrch
  endif
	cp -H tools/fsa/${rdir}/*.exe ${TarBall}/bin
	cp -H tools/zbs/${rdir}/*.exe ${TarBall}/bin
	#cp -H tools/fuse_proxy/${rdir}/*.exe ${TarBall}/bin
	cp -H tools/general/${rdir}/*.exe ${TarBall}/bin
endif
ifeq (${PKG_WITH_SRC},1)
	mkdir -p ${TarBall}/include/terark/zsrch
	cp -H src/terark/zsrch/*.hpp            ${TarBall}/include/terark/zsrch
endif # PKG_WITH_SRC
ifeq (${PKG_WITH_DBG},1)
	cp -a ${BUILD_ROOT}/lib_shared/libterark-{idx,fsa,zbs,core}-*d${DLL_SUFFIX} ${TarBall}/lib_shared
	cp -a ${BUILD_ROOT}/lib_shared/libterark-{idx,fsa,zbs,core}-*a${DLL_SUFFIX} ${TarBall}/lib_shared
  ifeq (${PKG_WITH_STATIC},1)
	mkdir -p ${TarBall}/lib_static
	cp -a ${BUILD_ROOT}/lib_static/libterark-{idx,fsa,zbs,core}-{${COMPILER}-,}d.a ${TarBall}/lib_static
	cp -a ${BUILD_ROOT}/lib_static/libterark-{idx,fsa,zbs,core}-{${COMPILER}-,}a.a ${TarBall}/lib_static
  endif
endif
	cp -a ${BUILD_ROOT}/lib_shared/libterark-{idx,fsa,zbs,core}-*r${DLL_SUFFIX} ${TarBall}/lib_shared
	echo $(shell date "+%Y-%m-%d %H:%M:%S") > ${TarBall}/package.buildtime.txt
	echo $(shell git log | head -n1) >> ${TarBall}/package.buildtime.txt
ifeq (${PKG_WITH_STATIC},1)
	mkdir -p ${TarBall}/lib_static
	cp -a ${BUILD_ROOT}/lib_static/libterark-{idx,fsa,zbs,core}-{${COMPILER}-,}r.a ${TarBall}/lib_static
endif
	cp -L tools/*/rls/*.exe ${TarBall}/bin/

${TarBall}.tgz: ${TarBall}
	cd pkg; tar czf ${TarBallBaseName}.tgz ${TarBallBaseName}

.PHONY : fsa_sample_code
fsa_sample_code : pkg/fsa_sample_code.tgz

.PHONY: test
.PHONY: test_dbg
.PHONY: test_afr
.PHONY: test_rls
test: test_dbg test_afr test_rls

test_dbg: ${TERARK_BIN_DEP_LIB}
	+$(MAKE) -C tests/core        test_dbg  CHECK_TERARK_FSA_LIB_UPDATE=0
	+$(MAKE) -C tests/tries       test_dbg  CHECK_TERARK_FSA_LIB_UPDATE=0
	+$(MAKE) -C tests/succinct    test_dbg  CHECK_TERARK_FSA_LIB_UPDATE=0

test_afr: ${TERARK_BIN_DEP_LIB}
	+$(MAKE) -C tests/core        test_afr  CHECK_TERARK_FSA_LIB_UPDATE=0
	+$(MAKE) -C tests/tries       test_afr  CHECK_TERARK_FSA_LIB_UPDATE=0
	+$(MAKE) -C tests/succinct    test_afr  CHECK_TERARK_FSA_LIB_UPDATE=0

test_rls: ${TERARK_BIN_DEP_LIB}
	+$(MAKE) -C tests/core        test_rls  CHECK_TERARK_FSA_LIB_UPDATE=0
	+$(MAKE) -C tests/tries       test_rls  CHECK_TERARK_FSA_LIB_UPDATE=0
	+$(MAKE) -C tests/succinct    test_rls  CHECK_TERARK_FSA_LIB_UPDATE=0

${ddir}/shared_lib_obj_list.mk: BOOST_BUILD_DIR := ${ddir}/boost-shared
${adir}/shared_lib_obj_list.mk: BOOST_BUILD_DIR := ${rdir}/boost-shared
${rdir}/shared_lib_obj_list.mk: BOOST_BUILD_DIR := ${rdir}/boost-shared

ifneq ($(patsubst android%,android,${UNAME_System}),android)
${ddir}/shared_lib_obj_list.mk: ${ddir}/boost-shared/build.done 3rdparty/base64/lib/libbase64.o
${adir}/shared_lib_obj_list.mk: ${rdir}/boost-shared/build.done 3rdparty/base64/lib/libbase64.o
${rdir}/shared_lib_obj_list.mk: ${rdir}/boost-shared/build.done 3rdparty/base64/lib/libbase64.o
endif

${ddir}/shared_lib_obj_list.mk: $(call objs,zbs,d) $(call objs,fsa,d) ${core_d_o}
	@echo define TOPLING_LIB_OBJ_LIST_VAR > $@
	@for f in ${THIS_LIB_OBJS}; do echo $$f >> $@; done
	@echo endef >> $@
	@echo define TOPLING_LIB_SRC_LIST_VAR >> $@ # without boost src
	@for f in ${core_src} ${fsa_src} ${zbs_src}; do echo $$f >> $@; done
	@echo endef >> $@
	@echo "Generated $@"

${adir}/shared_lib_obj_list.mk: $(call objs,zbs,a) $(call objs,fsa,a) ${core_a_o}
	@echo define TOPLING_LIB_OBJ_LIST_VAR > $@
	@for f in ${THIS_LIB_OBJS}; do echo $$f >> $@; done
	@echo endef >> $@
	@echo define TOPLING_LIB_SRC_LIST_VAR >> $@ # without boost src
	@for f in ${core_src} ${fsa_src} ${zbs_src}; do echo $$f >> $@; done
	@echo endef >> $@
	@echo "Generated $@"

${rdir}/shared_lib_obj_list.mk: $(call objs,zbs,r) $(call objs,fsa,r) ${core_r_o}
	@echo define TOPLING_LIB_OBJ_LIST_VAR > $@
	@for f in ${THIS_LIB_OBJS}; do echo $$f >> $@; done
	@echo endef >> $@
	@echo define TOPLING_LIB_SRC_LIST_VAR >> $@ # without boost src
	@for f in ${core_src} ${fsa_src} ${zbs_src}; do echo $$f >> $@; done
	@echo endef >> $@
	@echo "Generated $@"

ifneq ($(MAKECMDGOALS),cleanall)
ifneq ($(MAKECMDGOALS),clean)
-include ${alldep}
endif
endif

#@param ${1} file name suffix: cpp | cxx | cc
#@PARAM ${2} build dir       : ddir | rdir | adir
#@param ${3} debug flag      : DBG_FLAGS | RLS_FLAGS | AFR_FLAGS
define COMPILE_CXX
${2}/%.o: %.${1}
	@mkdir -p $$(dir $$@)
	$${AM_V_CC} $${CXX} $${CXX_STD} $${CPU} -c ${3} $${CXXFLAGS} $${INCS} $$< -o $$@
${2}/%.s : %.${1}
	@echo file: $$< "->" $$@
	@mkdir -p $$(dir $$@)
	$${CXX} -S -fverbose-asm $${CXX_STD} $${CPU} ${3} $${CXXFLAGS} $${INCS} $$< -o $$@
${2}/%.d : %.${1}
	@mkdir -p $$(dir $$@)
	-$${AM_V_CC} $${CXX} $${CXX_STD} ${3} -M -MT $$(basename $$@).o $${INCS} $$< > $$@
endef

define COMPILE_C
${2}/%.o : %.${1}
	@mkdir -p $$(dir $$@)
	$${AM_V_CC} $${CC} -c $${CPU} ${3} $${CFLAGS} $${INCS} $$< -o $$@
${2}/%.s : %.${1}
	@echo file: $$< "->" $$@
	@mkdir -p $$(dir $$@)
	$${CC} -S -fverbose-asm $${CPU} ${3} $${CFLAGS} $${INCS} $$< -o $$@
${2}/%.d : %.${1}
	@mkdir -p $$(dir $$@)
	-$${AM_V_CC} $${CC} ${3} -M -MT $$(basename $$@).o $${INCS} $$< > $$@
endef

$(eval $(call COMPILE_CXX,cpp,${ddir},${DBG_FLAGS}))
$(eval $(call COMPILE_CXX,cxx,${ddir},${DBG_FLAGS}))
$(eval $(call COMPILE_CXX,cc ,${ddir},${DBG_FLAGS}))
$(eval $(call COMPILE_CXX,cpp,${rdir},${RLS_FLAGS}))
$(eval $(call COMPILE_CXX,cxx,${rdir},${RLS_FLAGS}))
$(eval $(call COMPILE_CXX,cc ,${rdir},${RLS_FLAGS}))
$(eval $(call COMPILE_CXX,cpp,${adir},${AFR_FLAGS}))
$(eval $(call COMPILE_CXX,cxx,${adir},${AFR_FLAGS}))
$(eval $(call COMPILE_CXX,cc ,${adir},${AFR_FLAGS}))
$(eval $(call COMPILE_C  ,c  ,${ddir},${DBG_FLAGS}))
$(eval $(call COMPILE_C  ,c  ,${rdir},${RLS_FLAGS}))
$(eval $(call COMPILE_C  ,c  ,${adir},${AFR_FLAGS}))

# disable buildin suffix-rules
.SUFFIXES:
