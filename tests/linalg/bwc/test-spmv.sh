#!/usr/bin/env bash

# This tests that the matrix times vector product does what it is
# expected to do, and also checks that the permutations do what they are
# expected to do as well.

: ${TMPDIR=/tmp}
nrows=100
ncols=100
density=10
seed=1
bindir=
bwc_extra=()
mf_bal_extra=()
nh=1
nv=1
prime=2

set -e

if [ "$CADO_DEBUG" ] ; then set -x ; fi

usage() {
    echo "Usage: $0 <N>" >&2
    exit 1
}

while [ $# -gt 0 ] ; do
    if [ "$1" = "--matrix-size" ] ; then
        shift
        nrows=`echo $1 | cut -d, -f1`
        ncols=`echo $1 | cut -d, -f2`   # = nrows if no comma
        shift
    elif [ "$1" = "--density" ] ; then
        shift
        density=$1
        shift
    elif [ "$1" = "--seed" ] ; then
        shift
        seed=$1
        shift
    elif [ "$1" = "--bindir" ] ; then
        shift
        bindir=$1
        shift
    elif [ "$1" = "--arith-layer" ] ; then
        shift
        arith_layer=$1
        shift
    elif [ "$1" = "--backends" ] ; then
        shift
        backends=($1)
        shift
    elif [ "$1" = "--prime" ] ; then
        shift
        prime=$1
        shift
    else
        case "$1" in
            thr=*|mpi=*)
                x=`echo $1 | cut -d= -f2 | cut -dx -f1`
                nh=$((x*nh))
                x=`echo $1 | cut -d= -f2 | cut -dx -f2`
                nv=$((x*nv))
                bwc_extra=("${bwc_extra[@]}" "$1")
                shift
                ;;
            *) echo "Unexpected arg: $1" >&2 ; usage;;
        esac
    fi
done

redirect_unless_debug() {
    file="$1"
    shift
    if [ "$CADO_DEBUG" ] ; then
        if ! "$@" > >(tee "$file") 2>&1 ; then
            echo "Failed command: $@" >&2
            exit 1
        fi
    else
        if ! "$@" > $file 2>&1 ; then
            echo "Failed command: $@" >&2
            exit 1
        fi
    fi
}

if ! [ "$nrows" ] ; then usage ; fi

wdir=$(mktemp -d  $TMPDIR/cado.XXXXXXXX)
cleanup() { if ! [ "$CADO_DEBUG" ] ; then rm -rf $wdir ; fi ; }
argh() { echo "Failed on command error" >&2 ; cleanup ; }
trap cleanup EXIT
trap argh ERR

extra_args_random_matrix=()
extra_args_mf=()
if [ $prime != 2 ] ; then 
    extra_args_random_matrix=(-c 2)
    extra_args_mf=(--withcoeffs)
fi

redirect_unless_debug $wdir/random_matrix.out $bindir/linalg/bwc/random_matrix $nrows $ncols -d $density  --binary -o $wdir/mat.bin -s $seed --freq "${extra_args_random_matrix[@]}"

if [ $nrows != $ncols ] ; then
    mf_bal_extra=(--rectangular --reorder both)
else
    mf_bal_extra=(--reorder columns)
fi
# mf_bal_extra=("${mf_bal_extra[@]}" skip_decorrelating_permutation=true)
redirect_unless_debug $wdir/bal.out $bindir/linalg/bwc/mf_bal $nh $nv $wdir/mat.bin out=$wdir/bal.bin "${mf_bal_extra[@]}" "${extra_args_mf[@]}"
B=$wdir/bal.bin

echo "## arith layer is $arith_layer"
echo "## backends to test: ${backends[*]}"

if [ "$prime" = 2 ] ; then
    case "$arith_layer" in
        u64k*) n=$((`echo $arith_layer | cut -c5-` * 64)); m=$n;;
        *) echo "unknown arithmetic layer $arith_layer" >&2; exit 1;;
    esac
    bwc_common=(m=$m n=$n)
    nullspace_values=(left RIGHT)
else
    bwc_common=(m=1 n=1)
    nullspace_values=(LEFT right)
fi


for impl in "${backends[@]}" ; do
    rm -f $wdir/Xa.0 $wdir/Xb.0
    rm -f $wdir/Xa.1 $wdir/Xb.1
    rm -f $wdir/XTa.0 $wdir/XTb.0
    rm -f $wdir/XTa.1 $wdir/XTb.1
    rm -f $wdir/MY.0 $wdir/sMY.0
    rm -f $wdir/WM.0 $wdir/sWM.0

    # The nullspace argument is just for selecting the preferred
    # direction for the matrix times vector product. But in the spmv_test
    # file, we unconditionally compute M times Y in the file MY, and W
    # times M in the file WM. Which of these products uses the "fast" or
    # "slow" code depdens on $ns.
    for ns in ${nullspace_values[@]} ; do
        redirect_unless_debug $wdir/spmv-$impl-left.out $bindir/linalg/bwc/bwc.pl :mpirun ${bwc_extra} -- $bindir/tests/linalg/bwc/spmv_test wdir=$wdir "${bwc_common[@]}" prime=$prime balancing=$B matrix=$wdir/mat.bin nullspace=$ns mm_impl=$impl no_save_cache=1 "${bwc_extra[@]}" skip_bw_early_rank_check=1
        # check done within the C code.
        # diff -q $wdir/Z.0 $wdir/ZI.0
        # diff -q $wdir/Z.0 $wdir/ZII.0
        diff -q $wdir/Xa.0 $wdir/Xb.0
        diff -q $wdir/Xa.1 $wdir/Xb.1
        diff -q $wdir/XTa.0 $wdir/XTb.0
        diff -q $wdir/XTa.1 $wdir/XTb.1
    done
    $bindir/tests/linalg/bwc/short_matmul -p $prime $wdir/mat.bin  $wdir/Y.0  $wdir/sMY.0 > /dev/null 2>&1
    diff -q $wdir/MY.0 $wdir/sMY.0
    $bindir/tests/linalg/bwc/short_matmul -p $prime -t $wdir/mat.bin  $wdir/W.0  $wdir/sWM.0 > /dev/null 2>&1
    diff -q $wdir/WM.0 $wdir/sWM.0
    echo "spmv ${nrows}x${ncols} $impl left ok ${bwc_extra[@]}"
done
