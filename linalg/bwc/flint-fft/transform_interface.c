
/* 
 * 
 * Copyright 2013, Emmanuel Thomé.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the author(s).
 * 
 */

#include <string.h>
#include <assert.h>
#include <errno.h>
#include "gmp.h"
#include "flint.h"
#include "fft.h"
#include "ulong_extras.h"
#include "fft_tuning.h"
#include "timing.h"

#ifndef iceildiv
/* unfortunately this fails miserably if x+y-1 overflows */
#define iceildiv(x,y)	(((x)+(y)-1)/(y))
#endif

/* This is copied from mul_fft_main.c ; given the size of the tuning
 * table for this code, there's no bloat danger */
static int fft_tuning_table[5][2] = FFT_TAB;

/* {{{ utilities */
/* return the # of transform terms which are actually computed. Depending
 * on whether we use MFA or not, the truncation point varies.
 */
static inline mp_size_t fti_trunc(struct fft_transform_info * fti)
{
    mp_size_t depth1 = fti->depth / 2;
    mp_size_t n = 1 << fti->depth;
    mp_size_t n1 = 1 << depth1; /* for MFA */
    mp_size_t trunc = fti->trunc0;
    if (trunc <= 2*n) trunc = 2*n+1;
    if (fti->alg == 0) {
        /* trunc must be even and greater than 2n */
        trunc += trunc & 1;
    } else {
        /* trunc must be greater than 2n and multiple of 2*n1 */
        trunc = 2 * n1 * ((trunc + 2 * n1 - 1) / (2 * n1));
    }
    return trunc;
}

/* return the one less than the # of words used to store coefficients in
 * R. */
static inline mp_size_t fti_rsize0(struct fft_transform_info * fti)
{
    mp_size_t w = fti->w;
    mp_size_t n = 1 << fti->depth;
    mp_size_t rsize0 = n*w/FLINT_BITS;  /* need rsize0+1 words for x\in R */
    return rsize0;
}
/*}}}*/

/* {{{ transform info, alloc, etc */
/* Put into fti the w and depth values which are suitable for
 * accumulating nacc products of bits1 times bits2 bits.
 *
 * We are basing our estimates on the same logic as above. As far as
 * tuning is concerned, this is probably not optimal.
 *
 */

/* j1 = ceiling(x1/b)
 * j2 = ceiling(x2/b)
 *
 * Lemma: x1+x2 <= 4*n*b implies j1+j2-1<=4*n
 *
 * Proof:
 *     bj1+bj2-e1-e2 <= 4*n*b
 *     j1+j2-(e1+e2)/b <= 4n
 *     Let now Z=j1+j2-(e1+e2+1)/b.
 *     We have: Z < j1+j2-(e1+e2)/b <= 4n
 *            e1+e2+1 <= 2*b-1, whence 0 < (e1+e2+1)/b < 2
 *     Thus j1+j2-1 <= Ceiling(Z) <= j1 + j2, and Ceiling(Z) <= 4*n.
 *
 * Conclusion: checking minwrap as in 4*n*bits >= minwrap is actually
 * more restrictive than just checking 4*n >= j1+j2-1 
 *
 * (example b=30 x1=150 x2=95 j1=5 j2=4 j1+j2-1=8 ; 4*n=8. This wraps at
 * 240 bits, but the degree 7 coefficient of the polynomials contains all
 * the needed bits. Nothing will appear in the coefficient of degee 8 in
 * the polynomial product.
 *
 */

/* returns the index of the first wrapped bit when multiplying P1 * P2 in
 * R[X] modulo X^(4*n)\pm 1, with the integer evaluation of P1 at 2^bits
 * is the integer a1, and P1 has j1 coefficients.
 */
static unsigned long firstwrap(mp_size_t j1, mp_size_t j2, mp_bitcnt_t bits, mp_size_t n)
{
    if (j1 + j2 - 1 <= 4*n) {
        /* Then no single coefficient wraps. We're happy */
        return ULONG_MAX;
    } else {
        /* The coefficient at degree 4*n will be the first to wrap, but
         * bits from the previous coefficient will also have to be
         * added/subtracted in order to compute exactly the modular
         * result.
         */
        return 4*n*bits;
    }
}

/* set the depth, w, and bits fields to something at least reasonable */
void fft_transform_info_set_first_guess(struct fft_transform_info * fti)
{
    mp_bitcnt_t bits1 = fti->bits1;
    mp_bitcnt_t bits2 = fti->bits2;
    unsigned int nacc = fti->nacc;
    mp_bitcnt_t minwrap = fti->minwrap;

    mp_size_t depth = 6;
    mp_size_t w = 1;
    mp_size_t n = ((mp_size_t) 1 << depth);
    unsigned int log_nacc = FLINT_CLOG2(nacc);
    mp_bitcnt_t bits = (n * w - (depth + 1) - log_nacc) / 2;
    mp_size_t j1 = iceildiv(bits1, bits);
    mp_size_t j2 = iceildiv(bits2, bits);

    if (minwrap == 0)
        minwrap = bits1 + bits2;

    assert(j1 + j2 - 1 > 2 * n);

    while (firstwrap(j1, j2, bits, n) < (unsigned long) minwrap) {
	if (w == 1)
	    w = 2;
	else {
	    depth++;
	    w = 1;
	    n *= 2;
	}

        /* Take [[bits]] as small as we can, subject to the constraint
         * that coefficients may be multiplied in R=Z/2^(nw)+1 with no
         * wraparound.
         *
         * Based on this, we cannot set [[bits]] to a higher value.
         *
         * If we elect to set [[bits]] to a value which is smaller than
         * the upper bound we computed, then this would
         * increase the degrees j1 and j2 somewhat. A second effect
         * is that 4*n*bits is even smaller. As a consequence, we always
         * set [[bits]] to the upper bound.
         */
        log_nacc = FLINT_CLOG2(nacc*iceildiv(4*n, j1+j2-1));
        bits = (n * w - (depth + 1) - log_nacc) / 2;

#if 0
        /* XXX Hack for debugging. Make sure that bits is a multiple of
         * four, so that we split at nibbles.
         */
        bits &= ~(mp_bitcnt_t) 3;
#endif

	j1 = iceildiv(bits1, bits);
	j2 = iceildiv(bits2, bits);
    }

    assert(j1 * bits >= bits1);
    assert(j2 * bits >= bits2);
    assert(firstwrap(j1, j2, bits, n) >= (unsigned long) minwrap);
    assert(2*bits + (depth + 1) + log_nacc <= (mp_bitcnt_t) n*w);
    assert(w==1 || w==2);

    fti->depth = depth;
    fti->w = w;
    fti->bits = bits;
}

/* This provides a mechanism to adjust the fft depth int the direction of
 * a shorter transform, with a larger base ring (that is, [adj] is
 * subtracted from the transform depth).
 *
 * Of course, this changes fti incompatibly: data which has been computed
 * with fti as it was before is not compatible with this new fti, and the
 * fti methods corresponding to the new objects may not operate on
 * objects which were taken care of (or just allocated) through the old
 * fti object.
 */
void fft_transform_info_adjust_depth(struct fft_transform_info * fti, unsigned int adj)
{
    fft_transform_info_set_first_guess(fti);

    ASSERT_ALWAYS(adj < fti->depth);

    mp_bitcnt_t bits1 = fti->bits1;
    mp_bitcnt_t bits2 = fti->bits2;
    unsigned int nacc = fti->nacc;
    mp_bitcnt_t minwrap = fti->minwrap;

    mp_bitcnt_t bits = fti->bits;
    mp_size_t depth = fti->depth;
    mp_size_t w = fti->w;
    unsigned int log_nacc = FLINT_CLOG2(nacc);

    mp_size_t off;
    mp_size_t n = ((mp_size_t) 1 << depth);

    mp_size_t j1 = iceildiv(bits1, bits);
    mp_size_t j2 = iceildiv(bits2, bits);

    if (minwrap == 0)
        minwrap = bits1 + bits2;
    
    /* We want to work in R=Z/(2^(wn)+1) (for a start, w=1 or 2).
     *
     * There, sqrt(2)^w is a 4n-th root of unity.
     *
     * Both operands will be written as polynomials with
     * [bits]-coefficients, evaluated at 2^bits. Those polynomials will
     * be multiplied as polynomials in R[x], having respectively j1 and
     * j2 coefficients. Their product is a polynomial with at most
     * j1+j2-1 coefficients. The coefficients of the product are bounded
     * by (number of accumulated products)*(number of summands)*(bound on
     * input coeffs)^2, which is more accurately: nacc *
     * min(j1,j2)*(2^(2*bits)) Since j1+j2-1<=4*n, we have
     * min(j1,j2)<=2n+1/2, whence min(j1,j2)<=2n since j1,j2 are
     * integers. So a bound is 2^(log_nacc + depth + 1 + 2*bits). n*w
     * must be above that for padding to work. And of course the final
     * product size must be compatible with the final FFT length.
     */

    if (depth < 11) {
	mp_size_t wadj = 1;

	off = adj;	/* adjust n and w */
	depth -= off;
	n = ((mp_size_t) 1 << depth);
	w *= ((mp_size_t) 1 << (2 * off));

        /* sqrt(2)^(w * 2^(2*off)) is a root of unity of order
         * 4n/2^(2*off) in the same ring as before Z/(2^(wn)+1). When we
         * compute (sqrt(2)^(w * 2^(2*off)))^(4n/2^off), we get 
         *
         * 2^(2nw*2^off) = 2^(2n*w'/2^off) = 2^(2n'w') with n'=n'/2^off.
         *
         * So in effect, by doing this, we are considering shorter dfts
         * over larger rings, but using roots of shorter order than what
         * is possible (here, we have sqrt(2)^w' which is a 4n'-th root,
         * sure, but w' is divisible by a large power of two)
         *
         * Admissible chunk sizes for these shorter DFTs are prescribed
         * by the modulus: we can have chunks of size almost n'w' (not
         * subtracting the transform length), and 4n' of these means a
         * total product size 4n'^2w'.  Hence the balance n/2^off with
         * w*2^off
         *
         * Practical case. Our initial value is depth=6, w=2, n=64. Say
         * that this corresponds, for instance, to a product of 15000
         * bits, the two operands being split into 125 chunks of 60 bits.
         * Accumulating 125 squares of 60 bits sums up to 127 bits, which
         * fits modulo 2^128+1.
         *
         * We restrict here to depth=2, n'=4, w'=512, i.e. working modulo
         * 2^2048+1. We can afford chunks of quite a few bits here:
         * 1022. A transform of length 4n' means a product of 16000 bits,
         * this fits.
         *
         */

	if (depth < 6)
	    wadj = ((mp_size_t) 1 << (6 - depth));

        /* Making wadj a multiple of 64/n ensures that n*w remains a
         * multiple of 64. */

	if (w > wadj) {
            do {		/* see if a smaller w will work. This can
                                   reduce the ring size. */
                w -= wadj;
		bits = (n * w - (depth + 1) - log_nacc) / 2;
#if 0
                /* XXX Hack for debugging. Make sure that bits is a multiple of
                 * four, so that we split at nibbles.
                 */
                bits &= ~(mp_bitcnt_t) 3;
#endif
		j1 = iceildiv(bits1, bits);
		j2 = iceildiv(bits2, bits);
	    } while (firstwrap(j1, j2, bits, n) >= minwrap && w > wadj);
	    w += wadj;
	}
        fti->alg = 0;
    } else {
	if (j1 + j2 - 1 <= 3 * n) {
            /* make the transform length twice smaller, with 2^wn only
             * moderately larger, as in this case we can afford it */
	    depth--;
            n = n / 2;
	    w *= 3;
	}
        /* Probably discriminating on the depth field could suffice. */
        fti->alg = 1;
    }

    log_nacc = FLINT_CLOG2(nacc*iceildiv(4*n, j1+j2-1));
    bits = (n * w - (depth + 1) - log_nacc) / 2;
#if 0
    /* XXX Hack for debugging. Make sure that bits is a multiple of
     * four, so that we split at nibbles.
     */
    bits &= ~(mp_bitcnt_t) 3;
#endif
    j1 = iceildiv(bits1, bits);
    j2 = iceildiv(bits2, bits);
    assert(firstwrap(j1, j2, bits, n) >= (unsigned long) minwrap);
    assert(2*bits + (depth + 1) + log_nacc <= (mp_bitcnt_t) n*w);
    fti->w = w;
    fti->depth = depth;
    fti->bits = bits;
    if (j1 + j2 - 1 <= 4 * n) {
        fti->trunc0 = j1 + j2 - 1;
    } else {
        fti->trunc0 = 4 * n;
    }
    // fprintf(stderr, "/* DEPTH = %zu, ALG = %d */\n", fti->depth, fti->alg);
}

void fft_get_transform_info_mulmod(struct fft_transform_info * fti, mp_bitcnt_t bits1, mp_bitcnt_t bits2, unsigned int nacc, mp_bitcnt_t minwrap)
{

    memset(fti, 0, sizeof(*fti));
    fti->bits1 = bits1;
    fti->bits2 = bits2;
    fti->nacc = nacc;
    fti->minwrap = minwrap;

    fft_transform_info_set_first_guess(fti);

    mp_size_t depth = fti->depth;
    mp_size_t w = fti->w;

    if (depth < 11) {
        fft_transform_info_adjust_depth(fti, fft_tuning_table[depth - 6][w - 1]);
    } else {
        /* adjust_depth is unused for MFA */
        fft_transform_info_adjust_depth(fti, 0);
    }

}

void fft_get_transform_info(struct fft_transform_info * fti, mp_bitcnt_t bits1, mp_bitcnt_t bits2, unsigned int nacc)
{
    fft_get_transform_info_mulmod(fti, bits1, bits2, nacc, 0);
}

#if 0
/* This is almost the same as
 *
 *      fft_get_transform_info(fti, bitsmin, bitsmax-bitsmin, nacc)
 *
 * except that there is one extra optimization we can't do. When we
 * assert above that j1+j2-1 has to be less than 4n, this still allows
 * the evaluated polynomial (PQ)(2^bits) has coefficients of index up to
 * 4*n*bits + (bitsize(n*w)-bits). For the middle product context, we do
 * want the full input to be captured when clamping at 2^bits. So the
 * constraints differ. We want iceildiv(bitsmax, bits) <= 4*n
 *
 */
void fft_get_transform_info_mp(struct fft_transform_info * fti, mp_bitcnt_t bitsmin, mp_bitcnt_t bitsmax, unsigned int nacc)
{
    assert(bitsmax >= bitsmin);
    mp_bitcnt_t bits1 = bitsmin;
    mp_bitcnt_t bits2 = bitsmax-bitsmin;
    /* our input takes iceildiv(bitsmax, bits), which is more than just
     * j1+j2-1 bits-sized coefficients. Because this j1+j2-1 implicitly
     * considers _products_ which are wider than [bits]
     *
     * Bottom line: we want j1+j2, not j1+j2-1, to fit within 4*n
     */

    fft_get_transform_info_generic(fti, bits1, bits2, nacc, 1);
}
#endif

/* Provide the transform info necessary for accumulating nacc products of
 * polynomials having respectively n1 and n2 coefficients, over GF(p),
 * using Kronecker substitution.
 * (we hare talking *length* n1 and n2, hence degrees n1-1 and n2-1).
 */
void fft_get_transform_info_fppol(struct fft_transform_info * fti, mpz_srcptr p, mp_size_t n1, mp_size_t n2, unsigned int nacc)
{
    /* The maximum number of summands is MIN(n1, n2) */
    mp_bitcnt_t cbits = 2 * mpz_sizeinbase(p, 2) + FLINT_CLOG2(nacc * FLINT_MIN(n1, n2));
    /* It is a limitation of the current code that we want at least 4096
     * bits for the product. */
    if ((n1+n2)*cbits < 4096)
        cbits=iceildiv(4096, n1+n2);
    fft_get_transform_info(fti, n1 * cbits, n2 * cbits, nacc);
    fti->ks_coeff_bits = cbits;
}
void fft_get_transform_info_fppol_mp(struct fft_transform_info * fti, mpz_srcptr p, mp_size_t nmin, mp_size_t nmax, unsigned int nacc)
{
    mp_bitcnt_t cbits = 2 * mpz_sizeinbase(p, 2) + FLINT_CLOG2(nacc * nmin);
    /* See above */
    if (nmax*cbits < 4096)
        cbits=iceildiv(4096, nmax);
    fft_get_transform_info_mulmod(fti,
            nmin * cbits,
            nmax * cbits,
            nacc,
            nmax * cbits + 1);
    /* The maximum number of summands is nmin */
    fti->ks_coeff_bits = cbits;
}


/* This returns the needed temporary storage for the different steps of a
 * fft multiplication process whose size corresponds
 * to the info given in *fti.
 * [0]: space to be allocated (size_t) for each transform.
 * [1]: temp space to be passed alongside with each transform
 *      (needs be allocated only once). This same amount is also needed
 *      when calling fft_addmul
 * [2]: temp space to be passed alongside with each fft_mul or fft_addmul
 *       convolution (needs be allocated only once).
 *
 * spaces returned in [1] and [2] are independent and the same area may
 * be used for both, but of course the caller must then ensure that they
 * are not used concurrently.
 *
 * Note that the size returned in [0] for each transform entails slight
 * overallocation due to pointer swap tricks here and there.
 */
void fft_get_transform_allocs(size_t sizes[3], struct fft_transform_info * fti)
{
    mp_size_t w = fti->w;
    mp_size_t n = 1 << fti->depth;
    size_t chunks = ((w * n) / FLINT_BITS + 1) * sizeof(mp_limb_t);

    /* alloc sizes are the same irrespective of whether or not we use MFA */

    /* See mul_truncate_sqrt2 and fft_truncate_sqrt2 */
    /* 4n ptrs to areas of [size] limbs, with size=(n*w/64)+1 */
    /* plus two areas which are here for pointer swaps */
    sizes[0] = (4 * n + 2) * (sizeof(mp_limb_t *) + chunks);
    /* transform temp: 1 temp areas of [size] limbs */
    sizes[1] = chunks;
    /* conv temp: 1 temp areas of 2*[size] limbs */
    sizes[2] = 2 * chunks;
}

#define VOID_POINTER_ADD(x, k) (((char*)(x))+(k))

void fft_transform_prepare(void * x, struct fft_transform_info * fti)
{
    mp_size_t n = 1 << fti->depth;
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** ptrs = (mp_limb_t **) x;
    mp_limb_t * data = (mp_limb_t*) VOID_POINTER_ADD(x, (4*n+2)*sizeof(mp_limb_t *));
    for(mp_size_t i = 0 ; i < 4*n+2 ; i++) {
        ptrs[i] = data + i * (rsize0 + 1);
    }
}

void fft_transform_export(void * x, struct fft_transform_info * fti)
{
    mp_size_t n = 1 << fti->depth;
    mp_limb_t ** ptrs = (mp_limb_t **) x;
    if (sizeof(unsigned long) == sizeof(mp_limb_t *)) {
        unsigned long * offs = (unsigned long *) x;
        for(mp_size_t i = 0 ; i < 4*n+2 ; i++) {
            offs[i] = ptrs[i] - (mp_limb_t *) x;
        }
    } else if (sizeof(unsigned int) == sizeof(mp_limb_t *)) {
        unsigned int * offs = (unsigned int *) x;
        for(mp_size_t i = 0 ; i < 4*n+2 ; i++) {
            offs[i] = ptrs[i] - (mp_limb_t *) x;
        }
    } else {
        abort();
    }
}


void fft_transform_import(void * x, struct fft_transform_info * fti)
{
    mp_size_t n = 1 << fti->depth;
    mp_limb_t ** ptrs = (mp_limb_t **) x;
    if (sizeof(unsigned long) == sizeof(mp_limb_t *)) {
        unsigned long * offs = (unsigned long *) x;
        for(mp_size_t i = 0 ; i < 4*n+2 ; i++) {
            ptrs[i] = (mp_limb_t *) x + offs[i];
        }
    } else if (sizeof(unsigned int) == sizeof(mp_limb_t *)) {
        unsigned int * offs = (unsigned int *) x;
        for(mp_size_t i = 0 ; i < 4*n+2 ; i++) {
            ptrs[i] = (mp_limb_t *) x + offs[i];
        }
    } else {
        abort();
    }
}

/* Hardly useful, in fact */
void * fft_transform_alloc(struct fft_transform_info * fti)
{
    size_t allocs[3];
    fft_get_transform_allocs(allocs, fti);
    void * data = malloc(allocs[0]);
    fft_transform_prepare(data, fti);
    return data;
}
/* }}} */

/* {{{ kronecker-schönhage */
void fft_split_fppol(void * y, mp_limb_t * x, mp_size_t cx, struct fft_transform_info * fti, mpz_srcptr p)
{
    /* XXX We are implicitly asserting that the transform here is
     * straight out of fft_transform_prepare(). If it is not, then we
     * will have trouble. Should we enforce this as an API requirement,
     * or sanitize the transform area by ourselves ? If the latter, is
     * there any point then in keeping fft_transform_prepare called
     * within fft_transform_alloc ? Probably not */
    mp_size_t n = 1 << fti->depth;
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** ptrs = (mp_limb_t **) y;

    mp_size_t np = mpz_size(p);
    mp_size_t nx = cx * np;
    mp_size_t ks_coeff_bits = fti->ks_coeff_bits;
    /* We re-implement fft_split_bits */
    mp_limb_t * area = ptrs[0]; // assumes fft_transform_prepare()
    mpn_zero(area, (4*n+2) * (rsize0 + 1));


    /* source area: polynomial over Fp[x], flat.
     *  pointer x
     *  coefficient index j
     *  limb index j*np, because coefficients in Fp are taken to span an
     *     integer number of words.
     *  coefficient stride np words
     *  length (number of coefficients) jmax
     */
    /*
     *
     * This is to be expanded with stride becoming ks_coeff_bits, and
     * later separated in chunk of width fti->bits, each in a
     * coefficient of the destination area.
     *
     * destination area: polynomial over R[x].
     *  pointer array ptrs[]
     *  each coefficient will contain fti->bits bits of data, but has
     *      room for more because of the needed legroom for
     *      schönhage-strassen.
     *
     * intermediate area (not created):
     *  zbit for the bit index.
     *
     */

    /* Source area: polynomial over Fp
     * limbs for chunk number xchunk are those starting at x + xchunk*np
     */
    mp_size_t xchunk = 0;
    mp_size_t xword_offset = 0; /* relative to chunk start */
    mp_size_t xbit_offset = 0;
    mp_size_t xuntil_fence = 0;
    mp_size_t xnchunks = nx / np;
    mp_limb_t * xr = x;

    /* Intermediate area (not created): integer. The integer in itself
     * has no fence. */
    mp_size_t ybit = 0;

    /* Destination area: polynomial over R.
     * limbs for chunk number zchunk are those starting at ptrs[zchunk]
     */
    mp_size_t zchunk = 0;
    mp_size_t zword_offset = 0; /* relative to chunk start */
    mp_size_t zbit_offset = 0;
    mp_size_t zuntil_fence = 0;
    mp_limb_t * zw = ptrs[0];

    /* Load the initial fence values */
    xuntil_fence = mpz_sizeinbase(p, 2);
    zuntil_fence = fti->bits;

    for( ; xchunk < xnchunks ; ) {
        if (xuntil_fence == 0) {
            /* Reload for another coefficient */
            xchunk++;
            xr = x + xchunk * np;
            xword_offset = 0;
            xbit_offset = 0;
            xuntil_fence = mpz_sizeinbase(p, 2);
            continue;
        }
        ybit = xchunk * ks_coeff_bits + xword_offset * FLINT_BITS + xbit_offset;
        zchunk = ybit / fti->bits;
        zbit_offset = ybit % fti->bits;
        zuntil_fence = fti->bits - (zbit_offset % fti->bits);
        zword_offset = 0;
        for( ; zbit_offset >= FLINT_BITS ; ) {
            zbit_offset -= FLINT_BITS;
            zword_offset++;
        }
        zw = ptrs[zchunk] + zword_offset;


        /* Current copy amount. The bit amount is driven by the fences.
         * The word amount is simply driven by the position of the last
         * bit read */
        mp_bitcnt_t ellbits = FLINT_MIN(xuntil_fence, zuntil_fence);
        mp_size_t ell = (xbit_offset + ellbits + FLINT_BITS - 1) / FLINT_BITS;

        mp_limb_t old = *zw;
        /*
        fprintf(stderr, "// bits [%zu..%zu[ of [x^%zu]P go into"
                " bits [%zu..%zu[ of [t^%zu]Q\n",
                xword_offset * FLINT_BITS + xbit_offset,
                xword_offset * FLINT_BITS + xbit_offset + ellbits,
                xchunk,
                zword_offset * FLINT_BITS + zbit_offset,
                zword_offset * FLINT_BITS + zbit_offset + ellbits,
                zchunk);
                */
        if (zbit_offset > xbit_offset) {
            mp_limb_t out = mpn_lshift(zw, xr, ell, zbit_offset - xbit_offset);
            zw[ell] = out;
        } else if (xbit_offset > zbit_offset) {
            mpn_rshift(zw, xr, ell, xbit_offset-zbit_offset);
        } else {
            mpn_copyi(zw, xr, ell);
        }
        *zw ^= old;

        xuntil_fence -= ellbits;
        xbit_offset += ellbits;
        for( ; xbit_offset >= FLINT_BITS ; ) {
            xbit_offset -= FLINT_BITS;
            xword_offset++;
            xr++;
        }
    }
    /* Now clear any trailing bits in the coefficients of the polynomial
     * in R[x]. By design, we might have left some.
     */
    for (mp_size_t j = 0; j < 4*n; j++) {
        mp_size_t h = fti->bits / FLINT_BITS;
        if (fti->bits % FLINT_BITS) {
            ptrs[j][h] &= (1UL << (fti->bits % FLINT_BITS)) - 1UL;
            h++;
        }
        mpn_zero(ptrs[j] + h, rsize0 + 1 - h);
    }
}

/* Cast y, which is a polynomial over R[x], into an integer polynomial,
 * and evaluate it at 2^bits. We expect to obtain zero bits everywhere
 * except in bit windows [j*ks_coeff_bits..(j+1)*ks_coeff_bits[. Note
 * that the evaluation at 2^bits involves additions.
 *
 * Because of possible carry propagation, we must scan through the entire
 * input set, and compute the additions, in order to be able to recognize
 * the correct bits.
 *
 * The nx first coefficients of the result (each occupying mpz_size(p)
 * limbs) go to the limb array x.
 */
void fft_combine_fppol(mp_limb_t * x, mp_size_t cx, void * y, struct fft_transform_info * fti, mpz_srcptr p)
{
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** ptrs = (mp_limb_t **) y;

    mp_size_t np = mpz_size(p);
    mp_size_t nx = cx * np;
    mpn_zero(x, nx);

    /* XXX silly placeholder, doing extra allocation */
    mp_bitcnt_t bxtemp = fti->bits1 + fti->bits2;
    mp_size_t lxtemp = 1 + (bxtemp-1) / fti->bits;
    mp_size_t nxtemp = (1 + (fti->bits - 1) / FLINT_BITS) * lxtemp;
    mp_limb_t * xtemp = malloc(nxtemp * sizeof(mp_limb_t));
    mpn_zero(xtemp, nxtemp);
    /* TODO: remove this middle man, and read directly from ptrs */
    fft_addcombine_bits(xtemp, ptrs, fti->trunc0, fti->bits, rsize0, nxtemp);
    mp_size_t ksspan = (fti->ks_coeff_bits / FLINT_BITS + 2);
    mp_limb_t * temp = malloc((2*ksspan+1) * sizeof(mp_limb_t));

    /* Beware. Because we're doing Kronecker-Schönhage, we know that at
     * least a full half-coefficient is zero on top of both source
     * operands. This means that we have a corresponding amount of zeros
     * on top of the product, namely one full coefficient. Hence the +1
     * in the condition below.
     */
    mp_limb_t topmask = (1UL << (fti->ks_coeff_bits % FLINT_BITS)) - 1UL;

    for(mp_size_t j = 0 ; (j+1) * fti->ks_coeff_bits < bxtemp ; j++) {
        mp_bitcnt_t bit0 = j * fti->ks_coeff_bits;
        mp_bitcnt_t bit1 = (j+1) * fti->ks_coeff_bits;
        assert(j * np < nx);
        assert((j+1) * np <= nx);

        /* TODO: rather read these from ptrs directly */
        /* How much words does [bit0..bit1[ span ? */
        mp_size_t w0 = bit0 / FLINT_BITS;
        mp_size_t w1 = (bit1 + FLINT_BITS - 1) / FLINT_BITS;

        assert(w1 - w0 <= ksspan);

        mp_size_t nwritten = (fti->ks_coeff_bits + FLINT_BITS - 1) / FLINT_BITS;
        assert(nwritten == w1 - w0 || nwritten+1 == w1-w0);
        if (bit0 % FLINT_BITS) {
            mpn_rshift(temp, xtemp + w0, w1 - w0, bit0 % FLINT_BITS);
        } else {
            mpn_copyi(temp, xtemp + w0, w1 - w0);
        }
        if (topmask) {
            temp[nwritten-1] &= topmask;
        }
        /* TODO: Barrett ! */
        mpn_tdiv_qr(temp + ksspan, x + j * np, 0, temp, nwritten, p->_mp_d, mpz_size(p));
    }
    free(temp);
    free(xtemp);
    /* We go through all limbs of the evaluated polynomial, calculating
     * them as we go
     *
     * limb starting at bit k*bits + j receives contribution from bits j
     * and above from coefficient k, but also from bits j+bits and above
     * from coefficient k-1, and even from the top bit of coefficient k-2
     * if j happens to be zero.
     *
     * As we do the calculations, we fill a temp window of size
     * ks_coeff_bits. When filling is done, its contents are reduced to
     * form a new coefficient of the resulting polynomial.
     *
     */
}

/* y is a polynomial over R[x]. We want to evaluate it at 2^bits,
 * and pick the resulting coefficients from bit windows 
 * [d0*ks_coeff_bits..(d1+1)*ks_coeff_bits[. Note
 * that the evaluation at 2^bits involves additions.
 *
 * Because of possible carry propagation, our first "easy" approach does
 * the full evaluation. Contributions to the first of the bits we're
 * interested in may include a carry from previous coefficients. The
 * presence of a carry is directly decided from the presence of the bit
 * just before the lowest polynomial coefficient in R[x].
 */
void fft_combine_fppol_mp(mp_limb_t * x, mp_size_t cx, void * y, struct fft_transform_info * fti, mpz_srcptr p)
{
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** ptrs = (mp_limb_t **) y;

    mp_size_t np = mpz_size(p);
    mp_size_t nx = cx * np;
    mpn_zero(x, nx);

    /* XXX silly placeholder, doing extra allocation */

    /* bits1 and bits2 being in fact multiples of ks_coeff_bits, the
     * reality is that the "product", which in fact is one of our
     * operands, since we're doing the transposed operation, is smaller
     * than just bits1 + bits2.
     */

    /* MP context: y=MP(x, z)
     * bits1 = MIN(nx,nz) * ks_coeff_bits
     * bits2 = ny * ks_coeff_bits
     *
     * (ny = MAX(nx, nz) - MIN(nx, nz) + 1).
     *
     * --> bxtemp = 
     *  MAX(nx, nz) * ks_coeff_bits = bits1 + bits2 - ks_coeff_bits
     */

    mp_bitcnt_t bxtemp = fti->bits1 + fti->bits2 - fti->ks_coeff_bits;
    /* Then lxtemp is normally eactly the number of coefficients of the
     * largest of the two operands in R[x] */
    mp_size_t lxtemp = 1 + (bxtemp-1) / fti->bits;
    assert(lxtemp <= (4 << fti->depth));

    /* nxtemp is an approximation of the number of words it takes to
     * write the evaluated integer.
     */
    mp_size_t nxtemp = (1 + (fti->bits - 1) / FLINT_BITS) * (lxtemp - 1);
    /* even though we evaluate at 2^bits, coeffs are in R. So we should
     * take into account the fact that R is slightly larger... */
    nxtemp += (1 + ((fti->w << fti->depth) - 1) / FLINT_BITS);

    mp_limb_t * xtemp = malloc(nxtemp * sizeof(mp_limb_t));
    mpn_zero(xtemp, nxtemp);
    /* TODO: remove this middle man, and read directly from ptrs */
    fft_addcombine_bits(xtemp, ptrs, fti->trunc0, fti->bits, rsize0, nxtemp);

    mp_size_t nmin = fti->bits1 / fti->ks_coeff_bits - 1;
    mp_size_t nmax = bxtemp / fti->ks_coeff_bits - 1;

    mp_size_t ksspan = (fti->ks_coeff_bits / FLINT_BITS + 2);
    mp_limb_t * temp = malloc((2*ksspan+1) * sizeof(mp_limb_t));

    /* Beware. Because we're doing Kronecker-Schönhage, we know that at
     * least a full half-coefficient is zero on top of both source
     * operands. This means that we have a corresponding amount of zeros
     * on top of the product, namely one full coefficient. Hence the +1
     * in the condition below.
     */
    mp_limb_t topmask = (1UL << (fti->ks_coeff_bits % FLINT_BITS)) - 1UL;

    for(mp_size_t j = nmin ; j < nmax ; j++) {
        mp_bitcnt_t bit0 = j * fti->ks_coeff_bits;
        mp_bitcnt_t bit1 = (j+1) * fti->ks_coeff_bits;
        assert((j     - nmin) * np <  nx);
        assert((j + 1 - nmin) * np <= nx);

        /* TODO: rather read these from ptrs directly */
        /* How much words does [bit0..bit1[ span ? */
        mp_size_t w0 = bit0 / FLINT_BITS;
        mp_size_t w1 = (bit1 + FLINT_BITS - 1) / FLINT_BITS;

        assert(w1 - w0 <= ksspan);

        mp_size_t nwritten = (fti->ks_coeff_bits + FLINT_BITS - 1) / FLINT_BITS;
        assert(nwritten == w1 - w0 || nwritten+1 == w1-w0);
        if (bit0 % FLINT_BITS) {
            mpn_rshift(temp, xtemp + w0, w1 - w0, bit0 % FLINT_BITS);
        } else {
            mpn_copyi(temp, xtemp + w0, w1 - w0);
        }
        if (topmask) {
            temp[nwritten-1] &= topmask;
        }
        /* TODO: Barrett ! */
        mpn_tdiv_qr(temp + ksspan, x + (j - nmin) * np, 0, temp, nwritten, p->_mp_d, mpz_size(p));
    }
    free(temp);
    free(xtemp);
}
/* }}} */

#ifdef DEBUG_FFT/*{{{*/
void fft_debug_print_ft(const char * filename, void * y, struct fft_transform_info * fti)
{
    mp_size_t n = 1 << fti->depth;
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** ptrs = (mp_limb_t **) y;
    FILE * f = fopen(filename, "w");
    if (f == NULL) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        return;
    }
    fprintf(f, "data:=[");
    int i1 = 4*n;
    int z = 1;
    for( ; z && i1 > 0 ; i1-=z) {
        mp_limb_t * x = ptrs[i1-1];
        for(int i = 0 ; z && i < rsize0 + 1 ; i++) {
            z = (x[i] == 0);
        }
    }
    for(int i = 0 ; i < i1 ; i++) {
        if (i) fprintf(f, ", ");
        // fprintf(f, "\n");
        mp_limb_t * x = ptrs[i];
        fprintf(f, "[");
        int j1 = rsize0 + 1;
        for( ; j1 > 0 ; j1--) {
            if (x[j1-1] != 0) break;
        }
        for(int i = 0 ; i < j1 ; i++) {
            if (i) fprintf(f, ", ");
            fprintf(f, "%lu", x[i]);
        }
        fprintf(f, "]");
    }
    fprintf(f, "];\n");
    fclose(f);
}
#endif/*}}}*/

/* {{{ dft/ift backends */
static void fft_do_dft_backend(void * y, void * temp, struct fft_transform_info * fti)
{
#ifdef DEBUG_FFT
    fft_debug_print_ft("/tmp/before_dft.m", y, fti);
#endif
#ifdef TIME_FFT
    fti->dft.t -= seconds();
#endif
    mp_size_t n = 1 << fti->depth;
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** ptrs = (mp_limb_t **) y;
    mp_size_t trunc = fti_trunc(fti);

    mp_limb_t ** tslot0 = ptrs + 4 * n;
    mp_limb_t ** tslot1 = ptrs + 4 * n + 1;
    mp_limb_t * s1 = temp;

    if (fti->alg == 0) {
        fft_truncate_sqrt2(y, n, fti->w, tslot0, tslot1, &s1, trunc);
        for (mp_size_t j = 0; j < trunc; j++) {
            mpn_normmod_2expp1(ptrs[j], rsize0);
        }
    } else {
        /* 4n coeffs split into 2n2 rows of n1 coeffs */
        /* depth1+depth2 = depth + 1 */
        mp_size_t depth1 = fti->depth / 2;
        mp_size_t depth2 = fti->depth + 1 - fti->depth / 2;
        mp_size_t n1 = 1 << depth1; /* for MFA */
        mp_size_t n2 = 1 << depth2; /* for MFA */
        /* The first n2 rows need no truncation. The rest is truncated at
         * (trunc), which means that we take only trunc2 rows. */
        mp_size_t trunc2 = (trunc - 2 * n) / n1;

        /* outer */
        fft_mfa_truncate_sqrt2_outer(ptrs, n, fti->w, tslot0, tslot1, &s1, n1, trunc);

        /* inner layers */
        for (mp_size_t s = 0; s < trunc2; s++) {
            /* Truncation apparently appears only with bitrev semantics */
            mp_limb_t ** row = ptrs + 2*n + n_revbin(s, depth2) * n1;
            fft_radix2(row, n1 / 2, fti->w * n2, tslot0, tslot1);
            for (mp_size_t j = 0; j < n1; j++)    /* normalize right now */
                mpn_normmod_2expp1(row[j], rsize0);
        }
        for (mp_size_t i = 0; i < n2; i++) {
            mp_limb_t ** row = ptrs + i * n1;
            fft_radix2(row, n1 / 2, fti->w * n2, tslot0, tslot1);
            for (mp_size_t j = 0; j < n1; j++)    /* normalize right now */
                mpn_normmod_2expp1(row[j], rsize0);
        }
        /* Continue following fft_mfa_truncate_sqrt2_inner. iffts follow,
         * and then outer steps */
    }
#ifdef DEBUG_FFT
    fft_debug_print_ft("/tmp/after_dft.m", y, fti);
#endif
#ifdef TIME_FFT
    fti->dft.t += seconds();
    fti->dft.n++;
#endif
}

static void fft_do_ift_backend(void * y, void * temp, struct fft_transform_info * fti)
{
#ifdef DEBUG_FFT
    fft_debug_print_ft("/tmp/before_ift.m", y, fti);
#endif
#ifdef TIME_FFT
    fti->ift.t -= seconds();
#endif
    mp_size_t n = 1 << fti->depth;
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** ptrs = (mp_limb_t **) y;
    mp_size_t trunc = fti_trunc(fti);

    mp_limb_t ** tslot0 = ptrs + 4 * n;
    mp_limb_t ** tslot1 = ptrs + 4 * n + 1;
    mp_limb_t * s1 = temp;

    /* TODO: do we have to have inputs reduced ? */

    if (fti->alg == 0) {
        ifft_truncate_sqrt2(y, n, fti->w, tslot0, tslot1, &s1, trunc);
        for (mp_size_t j = 0; j < trunc; j++) {
            mpn_div_2expmod_2expp1(ptrs[j], ptrs[j], rsize0, fti->depth + 2);
            mpn_normmod_2expp1(ptrs[j], rsize0);
        }
    } else {
        /* 4n coeffs split into 2n2 rows of n1 coeffs */
        /* depth1+depth2 = depth + 1 */
        mp_size_t depth1 = fti->depth / 2;
        mp_size_t depth2 = fti->depth + 1 - fti->depth / 2;
        mp_size_t n1 = 1 << depth1; /* for MFA */
        mp_size_t n2 = 1 << depth2; /* for MFA */
        /* The first n2 rows need no truncation. The rest is truncated at
         * (trunc), which means that we take only trunc2 rows. */
        mp_size_t trunc2 = (trunc - 2 * n) / n1;
        
        /* Begin with inner steps (those which are intertwined with
         * convolution inside fft_mfa_truncate_sqrt2_inner), and finish
         * with outer steps */
        for (mp_size_t s = 0; s < trunc2; s++) {
            /* Truncation apparently appears only with bitrev semantics */
            mp_limb_t ** row = ptrs + 2*n + n_revbin(s, depth2) * n1;
            ifft_radix2(row, n1 / 2, fti->w * n2, tslot0, tslot1);
        }
        for (mp_size_t i = 0; i < n2; i++) {
            mp_limb_t ** row = ptrs + i * n1;
            ifft_radix2(row, n1 / 2, fti->w * n2, tslot0, tslot1);
        }

        /* outer. Does the division and normalization. */
        ifft_mfa_truncate_sqrt2_outer(ptrs, n, fti->w, tslot0, tslot1, &s1, n1, trunc);
    }
#ifdef DEBUG_FFT
    fft_debug_print_ft("/tmp/after_ift.m", y, fti);
#endif
#ifdef TIME_FFT
    fti->ift.t += seconds();
    fti->ift.n++;
#endif
}
/* }}} */

/* {{{ dft/ift frontends */
void fft_do_dft(void * y, mp_limb_t * x, mp_size_t nx, void * temp, struct fft_transform_info * fti)
{
    /* See mul_truncate_sqrt2 */
    mp_size_t n = 1 << fti->depth;
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** ptrs = (mp_limb_t **) y;

    mp_size_t j = fft_split_bits(y, x, nx, fti->bits, rsize0);
    for( ; j < 4 * n + 2; j++)
        mpn_zero(ptrs[j], rsize0 + 1);

    fft_do_dft_backend(y, temp, fti);                                          
}

/* This does the same as above, except that x is an array of nx
 * coefficients modulo p, each taking mpz_size(p) limbs
 */
void fft_do_dft_fppol(void * y, mp_limb_t * x, mp_size_t cx, void * temp, struct fft_transform_info * fti, mpz_srcptr p)
{
    fft_split_fppol(y, x, cx, fti, p);
    fft_do_dft_backend(y, temp, fti);
}

void fft_do_ift(mp_limb_t * x, mp_size_t nx, void * y, void * temp, struct fft_transform_info * fti)
{
    mp_size_t w = fti->w;
    mp_size_t n = 1 << fti->depth;
    mp_size_t rsize0 = n*w/FLINT_BITS;  /* need rsize0+1 words for x\in R */
    fft_do_ift_backend(y, temp, fti);
    mpn_zero(x, nx);
    if (!fti->minwrap) {
        fft_addcombine_bits(x, y, fti->trunc0, fti->bits, rsize0, nx);
        return;
    }

    /* it's slightly less trivial here. We perform the overwrap. (ok, in
     * typical middle product uses, we don't need it. But it's cheap).
     */
    /* we want at least (4*n-1)*bits+n*w bits in the output zone */
    mp_bitcnt_t need = (4*n-1)*fti->bits+n*w;
    assert(nx >= (mp_size_t) iceildiv(need, FLINT_BITS));
    fft_addcombine_bits(x, y, fti->trunc0, fti->bits, rsize0, nx);
    /* bits above 4*n*fti->bits need to wrap around */
    mp_bitcnt_t outneed = need - 4*n*fti->bits;
    mp_size_t outneedlimbs = iceildiv(outneed, FLINT_BITS);
    assert(outneedlimbs <= rsize0 + 1);
    mp_size_t toplimb = (4*n*fti->bits) / FLINT_BITS;
    unsigned int topoffset = (4*n*fti->bits) % FLINT_BITS;
    mp_size_t cy;
    mpn_zero(temp, rsize0 + 1);
    do {
        if (topoffset) {
            mpn_rshift(temp, x + toplimb, nx - toplimb, topoffset);
            x[toplimb] &= ((1UL<<topoffset)-1);
            if (nx - toplimb > 1)
                memset(x + toplimb + 1, 0, (nx - toplimb - 1) * sizeof(mp_limb_t));
        } else {
            memcpy(temp, x + toplimb, (nx - toplimb) * sizeof(mp_limb_t));
            memset(x + toplimb, 0, (nx - toplimb) * sizeof(mp_limb_t));
        }
        cy = mpn_add_n(x, x, temp, outneedlimbs);
        cy = mpn_add_1(x + outneedlimbs, x + outneedlimbs, toplimb - outneedlimbs, cy);
        mpn_add_1(x + toplimb, x + toplimb, nx - toplimb, cy);
    } while (cy);
}

/* Same, but store the result as a polynomial over GF(p). nx must be a
 * multiple of mpz_size(p)
 */
void fft_do_ift_fppol(mp_limb_t * x, mp_size_t cx, void * y, void * temp, struct fft_transform_info * fti, mpz_srcptr p)
{
    fft_do_ift_backend(y, temp, fti);
    fft_combine_fppol(x, cx, y, fti, p);
}

void fft_do_ift_fppol_mp(mp_limb_t * x, mp_size_t cx, void * y, void * temp, struct fft_transform_info * fti, mpz_srcptr p, mp_size_t shift)
{
    fft_do_ift_backend(y, temp, fti);
    mp_size_t nbigtemp = fft_get_mulmod_output_minlimbs(fti);
    mp_limb_t * bigtemp = malloc(nbigtemp * sizeof(mp_limb_t));
    mp_size_t rsize0 = fti_rsize0(fti);
    mpn_zero(bigtemp, nbigtemp);
    fft_addcombine_bits(bigtemp, y, fti->trunc0, fti->bits, rsize0, nbigtemp);

    mp_size_t np = mpz_size(p);
    mp_size_t nx = cx * np;

    mp_size_t ksspan = (fti->ks_coeff_bits / FLINT_BITS + 2);
    mp_limb_t * smalltemp = malloc((2*ksspan+1) * sizeof(mp_limb_t));
    mp_limb_t topmask = (1UL << (fti->ks_coeff_bits % FLINT_BITS)) - 1UL;

    mpn_zero(x, nx);

    for(mp_size_t j = 0 ; j * np < nx ; j++) {
        mp_bitcnt_t bit0 = (j+shift) * fti->ks_coeff_bits;
        mp_bitcnt_t bit1 = (j+shift+1) * fti->ks_coeff_bits;
        assert(j * np < nx);
        assert((j+1) * np <= nx);

        /* TODO: rather read these from ptrs directly */
        /* How much words does [bit0..bit1[ span ? */
        mp_size_t w0 = bit0 / FLINT_BITS;
        mp_size_t w1 = (bit1 + FLINT_BITS - 1) / FLINT_BITS;

        assert(w1 - w0 <= ksspan);

        mp_size_t nwritten = (fti->ks_coeff_bits + FLINT_BITS - 1) / FLINT_BITS;
        assert(nwritten == w1 - w0 || nwritten+1 == w1-w0);
        if (bit0 % FLINT_BITS) {
            mpn_rshift(smalltemp, bigtemp + w0, w1 - w0, bit0 % FLINT_BITS);
        } else {
            mpn_copyi(smalltemp, bigtemp + w0, w1 - w0);
        }
        if (topmask) {
            smalltemp[nwritten-1] &= topmask;
        }
        /* TODO: Barrett ! */
        mpn_tdiv_qr(smalltemp + ksspan, x + j * np, 0, smalltemp, nwritten, p->_mp_d, mpz_size(p));
    }

    free(smalltemp);
    free(bigtemp);
}

/* }}} */

void fft_mul(void * z, void * y0, void * y1, void * temp, struct fft_transform_info * fti)
{
#ifdef TIME_FFT
    fti->conv.t -= seconds();
#endif
    /* See mul_truncate_sqrt2 */
    mp_size_t nw = fti->w << fti->depth;
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** p0 = (mp_limb_t **) y0;
    mp_limb_t ** p1 = (mp_limb_t **) y1;
    mp_limb_t ** q = (mp_limb_t **) z;
    mp_size_t trunc = fti_trunc(fti);

    /* It is tempting to believe that MFA makes no difference here. In
     * fact it does, because of truncation, and the way rows are
     * organized after the transforms
     */

    if (fti->alg == 0) {
        for (mp_size_t j = 0; j < trunc; j++) {
            /* c is a bitmask. bit 0 is for the top bit of p1[j], bit 1 is
             * for the top bit of p1[j].  */
            mp_limb_t c = 2 * p0[j][rsize0] + p1[j][rsize0];
            q[j][rsize0] = mpn_mulmod_2expp1(q[j], p0[j], p1[j], c, nw, temp);
            assert(q[j][rsize0] <= 1);
        }
    } else {
        /* 4n coeffs split into 2n2 rows of n1 coeffs */
        /* depth1+depth2 = depth + 1 */
        mp_size_t n = 1 << fti->depth;
        mp_size_t depth1 = fti->depth / 2;
        mp_size_t depth2 = fti->depth + 1 - fti->depth / 2;
        mp_size_t n1 = 1 << depth1; /* for MFA */
        mp_size_t n2 = 1 << depth2; /* for MFA */
        /* The first n2 rows need no truncation. The rest is truncated at
         * (trunc), which means that we take only trunc2 rows. */
        mp_size_t trunc2 = (trunc - 2 * n) / n1;

        /* convolutions on relevant rows */
        for (mp_size_t s = 0; s < trunc2; s++) {
            mp_size_t t = 2*n + n_revbin(s, depth2) * n1;
            for (mp_size_t j = 0; j < n1; j++, t++) {
                mp_limb_t c = 2 * p0[t][rsize0] + p1[t][rsize0];
                q[t][rsize0] = mpn_mulmod_2expp1(q[t], p0[t], p1[t], c, nw, temp);
                assert(q[t][rsize0] <= 1);
            }
        }
        /* convolutions on rows */
        for (mp_size_t i = 0; i < n2; i++) {
            mp_size_t t = i * n1;
            for (mp_size_t j = 0; j < n1; j++, t++) {
                mp_limb_t c = 2 * p0[t][rsize0] + p1[t][rsize0];
                q[t][rsize0] = mpn_mulmod_2expp1(q[t], p0[t], p1[t], c, nw, temp);
                assert(q[t][rsize0] <= 1);
            }
        }
    }
#ifdef TIME_FFT
    fti->conv.t += seconds();
    fti->conv.n++;
#endif
}

static __inline__
void mpn_addmod_2expp1(mp_limb_t * z, mp_limb_t * x, mp_limb_t * y, mp_size_t limbs)
{
    /* This adds two normalized representatives modulo B^limbs + 1, with
     * B=2^FLINT_BITS. Result is normalized.
     *
     * TODO: we have to think about the normalization requirement. In the
     * context of accumulating several transforms, it may be wise to
     * relax this slightly (i.e. only one of the inputs reduced, and the
     * other not, nor the output).
     *
     * Possible cases:
     * c0 == 2:
     *    both x[0..limbs-1] and y[0..limbs-1] are zero. The result
     *    is then -2, i.e. 2^(n*w)-1 (all bits set).
     * c0 == 1:
     *    one of x[0..limbs-1] or y[0..limbs-1] is zero. Wlog, say x.
     *      - if y is non-zero, then subtracting 1 to y will
     *        give the desired result, and won't create a carry.
     *      - if y is really zero (without a carry), then the
     *        result is x.
     * c0 == 0:
     *    then mpn_add_n will yield a carry bit, to be subtracted
     *    *unless* the result is exactly 2^(n*w).
     *
     * The important thing is to optimize is the most likely case c0==0,
     * and c1 to be subtracted
     */
    assert(x[limbs] <= 1);
    assert(y[limbs] <= 1);

    mp_size_t c0 = x[limbs] + y[limbs];
    if (c0 == 0) {
        mp_size_t c1 = mpn_add_n(z, x, y, limbs);
        mp_size_t c2 = mpn_sub_1(z, z, limbs, c1);
        if (c2) mpn_zero(z, limbs);
        z[limbs] = c2;
    } else if (c0 == 1) {
        mp_limb_t * nz = x[limbs] ? y : x;
        mp_limb_t c2 = mpn_sub_1(z, nz, limbs, 1);
        if (c2) mpn_zero(z, limbs);
        z[limbs] = c2;
    } else {
        memset(z, ~0, limbs * sizeof(mp_limb_t));
        z[limbs] = 0;
    }
}

void fft_zero(void * z, struct fft_transform_info * fti)
{
    mp_limb_t ** ptrs = (mp_limb_t **) z;
    mp_size_t n = 1 << fti->depth;
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t * area = ptrs[0];
    mpn_zero(area, (4*n+2) * (rsize0 + 1));
}

void fft_add(void * z, void * y0, void * y1, struct fft_transform_info * fti)
{
    /* See fft_mul */
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** p0 = (mp_limb_t **) y0;
    mp_limb_t ** p1 = (mp_limb_t **) y1;
    mp_limb_t ** q = (mp_limb_t **) z;
    mp_size_t trunc = fti_trunc(fti);

    /* It is tempting to believe that MFA makes no difference here. In
     * fact it does, because of truncation, and the way rows are
     * organized after the transforms
     */

    if (fti->alg == 0) {
        for (mp_size_t j = 0; j < trunc; j++) {
            mpn_addmod_2expp1(q[j], p0[j], p1[j], rsize0);
        }
    } else {
        /* 4n coeffs split into 2n2 rows of n1 coeffs */
        /* depth1+depth2 = depth + 1 */
        mp_size_t n = 1 << fti->depth;
        mp_size_t depth1 = fti->depth / 2;
        mp_size_t depth2 = fti->depth + 1 - fti->depth / 2;
        mp_size_t n1 = 1 << depth1; /* for MFA */
        mp_size_t n2 = 1 << depth2; /* for MFA */
        /* The first n2 rows need no truncation. The rest is truncated at
         * (trunc), which means that we take only trunc2 rows. */
        mp_size_t trunc2 = (trunc - 2 * n) / n1;

        /* convolutions on relevant rows */
        for (mp_size_t s = 0; s < trunc2; s++) {
            mp_size_t t = 2*n + n_revbin(s, depth2) * n1;
            for (mp_size_t j = 0; j < n1; j++, t++) {
                mpn_addmod_2expp1(q[t], p0[t], p1[t], rsize0);
            }
        }
        /* convolutions on rows */
        for (mp_size_t i = 0; i < n2; i++) {
            mp_size_t t = i * n1;
            for (mp_size_t j = 0; j < n1; j++, t++) {
                mpn_addmod_2expp1(q[t], p0[t], p1[t], rsize0);
            }
        }
    }
}

void fft_addmul(void * z, void * y0, void * y1, void * temp, void * qtemp, struct fft_transform_info * fti)
{
#ifdef TIME_FFT
    fti->conv.t -= seconds();
#endif
    /* See mul_truncate_sqrt2 */
    mp_size_t nw = fti->w << fti->depth;
    mp_size_t rsize0 = fti_rsize0(fti);
    mp_limb_t ** p0 = (mp_limb_t **) y0;
    mp_limb_t ** p1 = (mp_limb_t **) y1;
    mp_limb_t ** q = (mp_limb_t **) z;
    mp_size_t trunc = fti_trunc(fti);
    mp_limb_t * qt = (mp_limb_t *) qtemp;

    /* It is tempting to believe that MFA makes no difference here. In
     * fact it does, because of truncation, and the way rows are
     * organized after the transforms
     */

    if (fti->alg == 0) {
        for (mp_size_t j = 0; j < trunc; j++) {
            /* c is a bitmask. bit 0 is for the top bit of p1[j], bit 1 is
             * for the top bit of p1[j].  */
            mp_limb_t c = 2 * p0[j][rsize0] + p1[j][rsize0];
            qt[rsize0] = mpn_mulmod_2expp1(qt, p0[j], p1[j], c, nw, temp);
            assert(qt[rsize0] <= 1);
            /* now q[j] += qt : */
            mpn_addmod_2expp1(q[j], q[j], qt, rsize0);
        }
    } else {
        /* 4n coeffs split into 2n2 rows of n1 coeffs */
        /* depth1+depth2 = depth + 1 */
        mp_size_t n = 1 << fti->depth;
        mp_size_t depth1 = fti->depth / 2;
        mp_size_t depth2 = fti->depth + 1 - fti->depth / 2;
        mp_size_t n1 = 1 << depth1; /* for MFA */
        mp_size_t n2 = 1 << depth2; /* for MFA */
        /* The first n2 rows need no truncation. The rest is truncated at
         * (trunc), which means that we take only trunc2 rows. */
        mp_size_t trunc2 = (trunc - 2 * n) / n1;

        /* convolutions on relevant rows */
        for (mp_size_t s = 0; s < trunc2; s++) {
            mp_size_t t = 2*n + n_revbin(s, depth2) * n1;
            for (mp_size_t j = 0; j < n1; j++, t++) {
                mp_limb_t c = 2 * p0[t][rsize0] + p1[t][rsize0];
                qt[rsize0] = mpn_mulmod_2expp1(qt, p0[t], p1[t], c, nw, temp);
                assert(qt[rsize0] <= 1);
                mpn_addmod_2expp1(q[t], q[t], qt, rsize0);
            }
        }
        /* convolutions on rows */
        for (mp_size_t i = 0; i < n2; i++) {
            mp_size_t t = i * n1;
            for (mp_size_t j = 0; j < n1; j++, t++) {
                mp_limb_t c = 2 * p0[t][rsize0] + p1[t][rsize0];
                qt[rsize0] = mpn_mulmod_2expp1(qt, p0[t], p1[t], c, nw, temp);
                assert(qt[rsize0] <= 1);
                mpn_addmod_2expp1(q[t], q[t], qt, rsize0);
            }
        }
    }
#ifdef TIME_FFT
    fti->conv.t += seconds();
    fti->conv.n++;
#endif
}



void get_ft_hash(mpz_t h, int bits_per_coeff, void * data, struct fft_transform_info * fti)
{
    mp_limb_t ** ptrs = data;
    mp_size_t trunc = fti_trunc(fti);
    mp_size_t rsize0 = fti_rsize0(fti);
    mpz_set_ui(h, 0);
    for(mp_size_t i = trunc ; i-- > 0 ; ) {
        mpz_mul_2exp(h, h, bits_per_coeff);
        mp_limb_t v = 0;
        for(mp_size_t j = 0 ; j < rsize0 + 1 ; j++) {
            v += (1+j)*ptrs[i][j];
        }
        v &= (1 << bits_per_coeff) - 1;
        mpz_add_ui(h, h, v);
    }
}
