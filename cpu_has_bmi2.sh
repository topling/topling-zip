#!/bin/bash
set -e
if [ -z "$CXX" ]; then
	CXX=g++
fi
if [ -z "$TMPDIR" ]; then
    TMPDIR=/tmp
fi
tmpfile=$(mktemp ${TMPDIR}/detect_bmi2-XXXXXX)
if [ -z "$CPU" ]; then
    # default bmi2 flags is native
    CPU=-march=native
fi
cat > ${tmpfile}.cpp << EOF
#include <stdio.h>
int main() {
  #ifdef __BMI2__
    printf("1");
  #else
    printf("0");
  #endif
    return 0;
}
EOF
${CXX} ${CPU} ${tmpfile}.cpp -o ${tmpfile} && ${tmpfile} && rm -f ${tmpfile}*
