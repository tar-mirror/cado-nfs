#!/bin/sh

## test for y -> -y using Galois filtering

CADO_NFS_SOURCE_DIR=$1
CADO_NFS_BINARY_DIR=$2

NCPUS=$("`dirname $0`"/ncpus.sh)

t=`mktemp -d ${TMPDIR-/tmp}/cado-check.XXXXXXX`

POLYFILE=$t/p2dd10-JLSV1-moinsY-10-07-2015.poly
PARAMFILE=$t/p2dd10-JLSV1-moinsY-10-07-2015.params
WDIR=$t/p2dd10

mkdir $WDIR

cat > $POLYFILE <<EOF
n: 3141592661
# ell: 485263
skew: 1.000
poly0: -60040,0,1
poly1: -339,0,52325
# f := x^2 - 60040;
# smfexp := 485262;
# signature(f)=(2,0)
# alpha(f) =  1.84461e-1
# Murphy E (f) =  2.19652e-1
# log_2(Nf) = 16
# g := 52325*x^2 - 339;
# smgexp := 235480179168;
# signature(g)=(2,0)
# alpha(g) =  1.07314e0
# Murphy E (g) =  4.30826e-2
# Murphy E (f, g) =  2.57669e-3# log_2(Ng) = 16
# varphi := x^2 + 3141532621;
# varphi = gcd(f, g) mod p.
EOF


cat > $PARAMFILE <<EOF
name = p2dd10-JLSV1-moinsY-10-07-2015
dlp = true
N = 3141592661
ell = 485263

slaves.nrclients = $(((1+NCPUS)/2))
tasks.threads = 2
tasks.linalg.bwc.threads = $NCPUS
tasks.execpath = $CADO_NFS_BINARY_DIR
slaves.scriptpath = $CADO_NFS_SOURCE_DIR
tasks.workdir = $WDIR
slaves.basepath= $WDIR/client
slaves.hostnames = localhost

tasks.polyselect.import = $POLYFILE

tasks.galois = _y
tasks.sieve.freerel.pmax=3
tasks.lcideals = true

tasks.I = 10
tasks.polyselect.degree = 4
tasks.polyselect.admax = 0
tasks.polyselect.incr = 60
tasks.polyselect.adrange = 500
tasks.polyselect.P = 420
tasks.polyselect.nq = 1000

lim0 = 2000
lim1 = 2000
lpb0 = 15
lpb1 = 15
tasks.sieve.mfb0 = 17
tasks.sieve.mfb1 = 17
tasks.sieve.qrange = 100
# tasks.sieve.rels_wanted = 10000

tasks.linalg.allow_zero_on_rhs = 1
tasks.reconstructlog.partial = true
checkdlp = false
EOF

cleanup() {
    if ! [ "$CADO_DEBUG" ] ; then
        rm -rf $t
    else
        echo "(debug mode, temporary files are kept in $t)"
    fi
}

${CADO_NFS_SOURCE_DIR}/cado-nfs.py $PARAMFILE && cleanup
