
dir=`dirname "$0"`
cd $dir
if [ -z "$CXX" ]; then
	CXX=g++
fi
tmpfile=$(mktemp compiler-XXXXXX)
${CXX} tools/configure/compiler.cpp -o ${tmpfile}.exe && ./${tmpfile}.exe && rm -f ${tmpfile}*
