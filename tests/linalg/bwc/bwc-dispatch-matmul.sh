#!/usr/bin/env bash

set -e

if [ "$CADO_DEBUG" ] ; then set -x ; fi

# only arguments understood are mpi= thr= seed=

thr=1x1
mpi=1x1
nrows=65536
density=2
seed=$RANDOM    # can be overridden !

while [ $# -gt 0 ] ; do
    if [[ "$1" =~ ^(thr|mpi)=([0-9]+)x([0-9]+)$ ]] ; then
        eval "$1"
        shift
        continue
    elif [[ "$1" =~ ^(seed|nrows|density)=[0-9]+$ ]] ; then
        eval "$1"
        shift
        continue
    elif [[ "$1" =~ ^hostfile=(.+)$ ]] ; then
        hostfile="${BASH_REMATCH[1]}"
        if ! [ -f "$hostfile" ] ; then
            echo "hostfile $hostfile does not exist" >&2
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
        echo "only arguments understood are mpi= thr= seed= nrows= density= bindir= hostfile=" >&2
        exit 1
    fi
done

if ! [ -d "$bindir" ] ; then
    echo "bindir $bindir does not exist" >&2
    exit 1
fi


if [[ $thr =~ ^([0-9]+)x([0-9]+)$ ]] ; then
    thr1="${BASH_REMATCH[1]}"
    thr2="${BASH_REMATCH[2]}"
else
    echo "Uh ?? $thr" >&2
    exit 1
fi
if [[ $mpi =~ ^([0-9]+)x([0-9]+)$ ]] ; then
    mpi1="${BASH_REMATCH[1]}"
    mpi2="${BASH_REMATCH[2]}"
else
    echo "Uh ?? $mpi" >&2
    exit 1
fi

nh=$((thr1 * mpi1))
nv=$((thr2 * mpi2))

M=x$seed
D=`mktemp -d ${TMPDIR-/tmp}/bwc.XXXXXXXXXXX`

cleanup() {
    if ! [ "$CADO_DEBUG" ] ; then
        rm -rf $D
    fi
}

trap cleanup EXIT

# MPI=/localdisk/ethome/Packages/openmpi-1.8.2 DEBUG=1 make -j8

"$bindir/mf_scan" --ascii-in --mfile <("$bindir/random_matrix" $nrows -d $density -s $seed)  --binary-out --freq --ofile $D/$M.bin

$bindir/mf_bal mfile=$D/$M.bin out=$D/ $nh $nv

set +e

set \
    nullspace=left	\
    wdir=$D	\
    thr=$thr	\
    mpi=$mpi	\
    mn=64	\
    verbose_flags=^all-cmdline,^bwc-timing-grids,^all-bwc-dispatch,^bwc-cache-major-info,^perl-cmdline,^perl-sections,^perl-checks	\
    matrix=$D/$M.bin	\
    sequential_cache_build=1	\
    sanity_check_vector=H1	\
    rebuild_cache=1	\
    skip_bw_early_rank_check=1       \
    matmul_bucket_methods=small1,small2,large

if [ "$hostfile" ] ; then
    set "$@" hostfile="$hostfile"
fi

$bindir/bwc.pl dispatch "$@"

rc=$?
# $bindir/dispatch nullspace=left wdir=/tmp/$M thr=${thr1}x${thr2} interval=20 mn=64 prime=2 verbose_flags=^all-cmdline,^bwc-timing-grids matrix=/tmp/$M.bin balancing=$bfile ys=0..64 sequential_cache_build=1 sanity_check_vector=H1 rebuild_cache=1 matmul_bucket_methods=small1,small2,large

exit $rc
