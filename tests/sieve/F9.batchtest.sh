#!/usr/bin/env bash

# This file defines the sieving parameter and the reference SHA1 value, then calls batchtest.sh

FB="$1"
LAS="$2"
SRCDIR="$3"
CHECKSUM_FILE="$4"
SOURCE_TEST_DIR="`dirname "$0"`"
shift 4

# the checksum should not change in batch mode, which should find the maximum
# number of relations (except if the order of primes changes)
REFERENCE_SHA1="74bf09179736a350cc706685a83fd155cbb75f33" # 272 relations
REFERENCE_REVISION="6613a1fa323715ee61314078d509b51e4b03c41c"

lim0=2300000
lim1=1200000
lpb0=26
lpb1=24
maxbits=10
mfb0=52
mfb1=72
lambda0=2.2
lambda1=2.9
I=12
q0=1200000
q1=1200200

export lim0 lim1 lpb0 lpb1 maxbits mfb0 mfb1 lambda0 lambda1 I q0 q1
"${SOURCE_TEST_DIR}"/batchtest.sh "${FB}" "${LAS}" "${SRCDIR}/parameters/polynomials/F9.poly" "${REFERENCE_SHA1}" "${REFERENCE_REVISION}" "${CHECKSUM_FILE}" --adjust-strategy 0 "$@" || exit 1

exit 0
