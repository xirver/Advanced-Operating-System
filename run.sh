#!/bin/bash

list_test_file() {

    printf "Test files:\n"

    for FP in user/*; do
        basename=$(basename ${FP})
        printf "\t%s\n" $basename
    done
}

usage() {
    printf "\nUsage:\n"
    printf "\t./run.sh <test file name> <lock type> [gdb or debug]\n\n"
    printf "Lock types: BKL, FGL\n\n"

    list_test_file
}

# Check CLI arguments are passed
if [ $# -eq 0 ]; then
    usage
    exit
fi

TEST=$1
CFLAGS=""
DEBUG=""
GDB=""

# Check if doing make grade
if [ "$TEST" == "grade" ]; then
    TEST_NAME="grade"
else
    TEST_NAME="run-${TEST}"
    GDB="-nox"
fi

# Check for "Big Kernel Lock" or "Fine Grained Lock"
if [ "${2^^}" == "BKL" ]; then
    printf "\n\tBIG KERNEL LOCK\n"
    CFLAGS="-DUSE_BIG_KERNEL_LOCK"
elif [ "${2^^}" == "FGL" ]; then
    printf "\n\tFINE-GRAINED LOCKING\n"
    CFLAGS=""
elif [ -n "${2}" ]; then
    printf "Unknown parameter "$2"\n";
    exit;
else 
    printf "Choose a lock type.\n"
    usage 
    exit;
fi

# Check for GDB or Debug
if [ "${3^^}" == "GDB" ]; then
    GDB="-gdb"
elif [ "${3^^}" == "DEBUG" ]; then
    DEBUG="-d int"
fi

# Run the command
clear
cmd="clear; CPUS=2 CFLAGS+=\"${CFLAGS}\" QEMUEXTRA+=\"${DEBUG}\" make $TEST_NAME$GDB"
echo $cmd
#eval $(echo $cmd)
#printf "\n$cmd\n"
#eval "$("$cmd")"
#CPUS=2 CFLAGS+=\"${CFLAGS}\" QEMUEXTRA+=\"${DEBUG}\" make $TEST_NAME$GDB


# CPUS=2 CFLAGS+="-DUSE_BIG_KERNEL_LOCK" make grade
# CPUS=2 CFLAGS+="-DUSE_BIG_KERNEL_LOCK" make run-yield-nox
