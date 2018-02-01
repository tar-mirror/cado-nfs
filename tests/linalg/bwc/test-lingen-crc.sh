#!/usr/bin/env bash

set -e

if [ "$CADO_DEBUG" ] ; then set -x ; fi

seed=0
m=64
n=64
random_stem=30000
sequence_length=200

while [ $# -gt 0 ] ; do
    if [[ "$1" =~ ^(seed|m|n|random_stem|sequence_length|expect_crc_[a-zA-Z]+)=[0-9a-f]+$ ]] ; then
        eval "$1"
        shift
        continue
    elif [[ "$1" =~ ^wdir=(.+)$ ]] ; then
        wdir="${BASH_REMATCH[1]}"
        if ! [ -d "$wdir" ] ; then
            echo "wdir $wdir does not exist" >&2
            exit 1
        fi
        shift
        continue
    elif [[ "$1" =~ ^bindir=(.+)$ ]] ; then
        bindir="${BASH_REMATCH[1]}"
        if ! [ -d "$bindir" ] ; then
            echo "bindir $bindir does not exist" >&2
            exit 1
        fi
        shift
        continue
    else
        echo "argument $1 not understood" >&2
        exit 1
    fi
done

if ! [ "$expect_crc_pi" ] || ! [ "$expect_crc_F" ] ; then
    echo "Please set expect_crc_pi and expect_crc_F on the command line" >&2
    exit 1
fi

if ! [ -d "$bindir" ] ; then
    echo "bindir $bindir does not exist" >&2
    exit 1
fi

if ! [ -d "$wdir" ] ; then
    echo "wdir $wdir does not exist" >&2
    exit 1
fi

"$(dirname $0)/perlrandom.pl" $random_stem $seed > $wdir/X
A_length=$((m*n*sequence_length/8))
nn=0
while [ $nn -lt $A_length ] ; do
    cat $wdir/X
    let nn+=$random_stem
done | dd ibs=1 count=$A_length > $wdir/seq

if ! "$bindir/lingen" m=$m n=$n lingen-input-file=$wdir/seq lingen-output-file=$wdir/seq.gen > >(tee $wdir/output > /dev/stderr) ; then
    echo "Error running lingen >&2"
    exit 1
fi

rc=0
for s in "crc(pi)=$expect_crc_pi" "crc(F)=$expect_crc_F" ; do
    if ! grep -q "$s" $wdir/output ; then
        echo "String $s" not found in $wdir/output >&2
        rc=1
    fi
done
exit $rc
