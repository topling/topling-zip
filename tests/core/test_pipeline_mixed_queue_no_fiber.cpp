// Keep the mixed queue regression compilable when the library disables fibers.
//Makefile: CXXFLAGS: -ffunction-sections -fdata-sections
//Makefile: LDFLAGS: -Wl,--gc-sections
#define TOPLING_PIPELINE_WITH_FIBER 0
#include "test_pipeline_mixed_queue.cpp"
