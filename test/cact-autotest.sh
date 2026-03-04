#!/bin/bash

# show the command executed by the script
#set -x
LIBCACTIO_PATH=../build
TEST_PATH=../test
TEMP_PATH=../temp
CACT_COMPILER=../build/compiler
SPIKE=/opt/riscv/bin/spike
RVCC=/opt/riscv/bin/riscv64-unknown-elf-gcc

PREFIX="\033[;32mINFO: \033[0m"
if [ $# -eq 0 ]; then
	echo "Usage: $0 <CACT source file> options"
	exit 1
fi

mkdir -p $TEMP_PATH
# substitute the file extension ".cact" with ".s" in `$1`
asmfile=${1%.cact}-ct.s
exefile_ct=${1%.cact}-ct
# remove the path in `asmfile`
asmfile=${asmfile##*/}
exefile_ct=${exefile_ct##*/}

echo -e $PREFIX"Compiling CACT file to RV64 ASM..."
$CACT_COMPILER $1  -o $TEMP_PATH/$asmfile $2 $3 $4 $5 $6 $7 $8
if [ $? -ne 0 ]; then
	exit $?
fi

echo -e $PREFIX"Assembling and linking RV64 ASM file to ELF..."
$RVCC $TEMP_PATH/$asmfile -o $TEMP_PATH/$exefile_ct -L$LIBCACTIO_PATH -lcactio
if [ $? -ne 0 ]; then
	exit $?
fi

cfile=${1%.cact}.c

# remove the path in `cfile`
cfile=${cfile##*/}

exefile_gcc=${1%.cact}-gcc
exefile_gcc=${exefile_gcc##*/}


echo -e $PREFIX"Generating C code..."
cat $TEST_PATH/cactio.c $1 > $TEMP_PATH/$cfile

echo -e $PREFIX"Compiling C file with gcc..."
gcc $TEMP_PATH/$cfile -o $TEMP_PATH/$exefile_gcc

if [ $? -ne 0 ]; then
	exit $?
fi

echo -e $PREFIX"Press Enter to start the ELF file from CACT compiler... "
read -p "> "

# `-s`: show the instruction count
$SPIKE pk -s $TEMP_PATH/$exefile_ct

echo -e $PREFIX"Press Enter to start the ELF file from gcc... "
read -p "> "

$TEMP_PATH/$exefile_gcc
