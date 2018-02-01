#include "cado.h"
#include "mpfq_layer.h"
#include <stdlib.h>
#include <gmp.h>
#include "portability.h"
#include "macros.h"
#include "lingen-matpoly.h"
#include "lingen-matpoly.h"
#ifdef HAVE_MPIR
#include "flint-fft/fft.h"
#endif

int matpoly_check_pre_init(matpoly_srcptr p)
{
    if (p->m && p->n && p->alloc)
        return 0;
    if (!p->m && !p->n && !p->alloc)
        return 1;
    abort();
    return 0;
}

/* with the exception of matpoly_realloc, all functions here are exactly
 * identical to those in lingen-polymat.c */
/* {{{ init/zero/clear interface for matpoly */
void matpoly_init(abdst_field ab, matpoly_ptr p, unsigned int m, unsigned int n, int len) {
    memset(p, 0, sizeof(matpoly));
    /* As a special case, we allow a pre-init state with m==n==len==0 */
    ASSERT(!m == !n);
    ASSERT(!m == !len);
    if (!m) return;
    p->m = m;
    p->n = n;
    p->alloc = len;
    p->size = 0;
    abvec_init(ab, &(p->x), m*n*p->alloc);
    abvec_set_zero(ab, p->x, m*n*p->alloc);
}
void matpoly_realloc(abdst_field ab, matpoly_ptr p, size_t newalloc) {
    matpoly_check_pre_init(p);
    /* zero out the newly added data */
    if (newalloc > p->alloc) {
        /* allocate new space, then deflate */
        abvec_reinit(ab, &(p->x), p->m*p->n*p->alloc, p->m*p->n*newalloc);
        abvec rhead = abvec_subvec(ab, p->x, p->m*p->n*p->alloc);
        abvec whead = abvec_subvec(ab, p->x, p->m*p->n*newalloc);
        for(unsigned int i = p->m ; i-- ; ) {
            for(unsigned int j = p->n ; j-- ; ) {
                whead = abvec_subvec(ab, whead, -newalloc);
                rhead = abvec_subvec(ab, rhead, -p->alloc);
                abvec_set(ab, whead, rhead, p->alloc);
                abvec_set_zero(ab,
                        abvec_subvec(ab, whead, p->alloc),
                        newalloc - p->alloc);
            }
        }
    } else {
        /* inflate, then free space */
        ASSERT_ALWAYS(p->size <= newalloc);
        abvec rhead = p->x;
        abvec whead = p->x;
        for(unsigned int i = 0 ; i < p->m ; i++) {
            for(unsigned int j = 0 ; j < p->n ; j++) {
                abvec_set(ab, whead, rhead, newalloc);
                whead = abvec_subvec(ab, whead, newalloc);
                rhead = abvec_subvec(ab, rhead, p->alloc);
            }
        }
        abvec_reinit(ab, &(p->x), p->m*p->n*p->alloc, p->m*p->n*newalloc);
    }
    p->alloc = newalloc;
}
void matpoly_zero(abdst_field ab, matpoly_ptr p) {
    p->size = 0;
    abvec_set_zero(ab, p->x, p->m*p->n*p->alloc);
}
void matpoly_clear(abdst_field ab, matpoly_ptr p) {
    abvec_clear(ab, &(p->x), p->m*p->n*p->alloc);
    memset(p, 0, sizeof(matpoly));
}
void matpoly_swap(matpoly_ptr a, matpoly_ptr b)
{
    matpoly x;
    memcpy(x, a, sizeof(matpoly));
    memcpy(a, b, sizeof(matpoly));
    memcpy(b, x, sizeof(matpoly));
}
/* }}} */

void matpoly_fill_random(abdst_field ab MAYBE_UNUSED, matpoly_ptr a, unsigned int size, gmp_randstate_t rstate)
{
    ASSERT_ALWAYS(size <= a->alloc);
    a->size = size;
    abvec_random(ab, a->x, a->m*a->n*size, rstate);
}

int matpoly_cmp(abdst_field ab MAYBE_UNUSED, matpoly_srcptr a, matpoly_srcptr b)
{
    ASSERT_ALWAYS(a->n == b->n);
    ASSERT_ALWAYS(a->m == b->m);
    if (a->size != b->size) return (a->size > b->size) - (b->size > a->size);
    return abvec_cmp(ab, a->x, b->x, a->m*a->n*a->size);
}


/* polymat didn't have a proper add(), which is somewhat curious */
void matpoly_add(abdst_field ab,
        matpoly c,
        matpoly a,
        matpoly b)
{
    ASSERT_ALWAYS(c->size == a->size);
    ASSERT_ALWAYS(c->size == b->size);
    for(unsigned int i = 0 ; i < a->m ; i++) {
        for(unsigned int j = 0 ; j < a->n ; j++) {
            abvec_add(ab,
                    matpoly_part(ab, c, i, j, 0),
                    matpoly_part(ab, a, i, j, 0),
                    matpoly_part(ab, b, i, j, 0), c->size);
        }
    }
}

void matpoly_multiply_column_by_x(abdst_field ab, matpoly_ptr pi, unsigned int j, unsigned int size)/*{{{*/
{
    ASSERT_ALWAYS((size + 1) <= pi->alloc);
    for(unsigned int i = 0 ; i < pi->m ; i++) {
        memmove(matpoly_part(ab, pi, i, j, 1), matpoly_part(ab, pi, i, j, 0), 
                size * abvec_elt_stride(ab, 1));
        abset_ui(ab, matpoly_coeff(ab, pi, i, j, 0), 0);
    }
}/*}}}*/

void matpoly_truncate(abdst_field ab, matpoly_ptr dst, matpoly_srcptr src, unsigned int size)/*{{{*/
{
    ASSERT_ALWAYS(size <= src->alloc);
    if (dst == src) {
        /* Never used by the code, so far. We're leaving garbage coeffs
         * on top, could this be a problem ? */
        dst->size = size;
        return;
    }
    if (matpoly_check_pre_init(dst)) {
        matpoly_init(ab, dst, src->m, src->n, size);
    }
    ASSERT_ALWAYS(dst->m == src->m);
    ASSERT_ALWAYS(dst->n == src->n);
    ASSERT_ALWAYS(size <= dst->alloc);
    dst->size = size;
    /* XXX Much more cumbersome here than for polymat, of course */
    for(unsigned int i = 0 ; i < src->m ; i++) {
        for(unsigned int j = 0 ; j < src->n ; j++) {
            abvec_set(ab,
                    matpoly_part(ab, dst, i, j, 0),
                    matpoly_part_const(ab, src, i, j, 0),
                    size);
        }
    }
}/*}}}*/

/* XXX compared to polymat, our diffferent striding has a consequence,
 * clearly ! */
void matpoly_extract_column(abdst_field ab,/*{{{*/
        matpoly_ptr dst, unsigned int jdst, unsigned int kdst,
        matpoly_srcptr src, unsigned int jsrc, unsigned int ksrc)
{
    ASSERT_ALWAYS(dst->m == src->m);
    for(unsigned int i = 0 ; i < src->m ; i++)
        abset(ab,
            matpoly_coeff(ab, dst, i, jdst, kdst),
            matpoly_coeff_const(ab, src, i, jsrc, ksrc));
}/*}}}*/

void matpoly_transpose_dumb(abdst_field ab, matpoly_ptr dst, matpoly_srcptr src) /*{{{*/
{
    if (dst == src) {
        matpoly tmp;
        matpoly_init(ab, tmp, 0, 0, 0);  /* pre-init for now */
        matpoly_transpose_dumb(ab, tmp, src);
        matpoly_swap(dst, tmp);
        matpoly_clear(ab, tmp);
        return;
    }
    if (!matpoly_check_pre_init(dst))
        matpoly_clear(ab, dst);
    matpoly_init(ab, dst, src->n, src->m, src->size);
    dst->size = src->size;
    for(unsigned int i = 0 ; i < src->m ; i++) {
        for(unsigned int j = 0 ; j < src->n ; j++) {
            for(unsigned int k = 0 ; k < src->size ; k++) {
                abset(ab,
                        matpoly_coeff(ab, dst, j, i, k),
                        matpoly_coeff_const(ab, src, i, j, k));
            }
        }
    }
}/*}}}*/

void matpoly_zero_column(abdst_field ab,/*{{{*/
        matpoly_ptr dst, unsigned int jdst, unsigned int kdst)
{
    for(unsigned int i = 0 ; i < dst->m ; i++)
        abset_zero(ab,
            matpoly_coeff(ab, dst, i, jdst, kdst));
}/*}}}*/

void matpoly_extract_row_fragment(abdst_field ab,/*{{{*/
        matpoly_ptr dst, unsigned int i1, unsigned int j1,
        matpoly_srcptr src, unsigned int i0, unsigned int j0,
        unsigned int n)
{
    ASSERT_ALWAYS(src->size <= dst->alloc);
    ASSERT_ALWAYS(dst->size == src->size);
    for(unsigned int k = 0 ; k < n ; k++)
        abvec_set(ab,
                matpoly_part(ab, dst, i1, j1 + k, 0),
                matpoly_part_const(ab, src, i0, j0 + k, 0), dst->size);
}/*}}}*/

void matpoly_rshift(abdst_field ab, matpoly_ptr dst, matpoly_srcptr src, unsigned int k)/*{{{*/
{
    ASSERT_ALWAYS(k <= src->size);
    unsigned int newsize = src->size - k;
    if (matpoly_check_pre_init(dst)) {
        matpoly_init(ab, dst, src->m, src->n, newsize);
    }
    ASSERT_ALWAYS(dst->m == src->m);
    ASSERT_ALWAYS(dst->n == src->n);
    ASSERT_ALWAYS(newsize <= dst->alloc);
    dst->size = newsize;
    for(unsigned int i = 0 ; i < src->m ; i++) {
        for(unsigned int j = 0 ; j < src->n ; j++) {
            abvec_set(ab,
                    matpoly_part(ab, dst, i, j, 0),
                    matpoly_part_const(ab, src, i, j, k),
                    newsize);
        }
    }
    if (dst == src)
        matpoly_realloc(ab, dst, newsize);
}/*}}}*/


void matpoly_addmul(abdst_field ab, matpoly c, matpoly a, matpoly b)/*{{{*/
{
    ASSERT_ALWAYS(a->n == b->m);
    if (matpoly_check_pre_init(c)) {
        matpoly_init(ab, c, a->m, b->n, a->size + b->size - 1);
    }
    ASSERT_ALWAYS(c->m == a->m);
    ASSERT_ALWAYS(c->n == b->n);
    ASSERT_ALWAYS(c->alloc >= a->size + b->size - 1);
    c->size = a->size + b->size - 1;
    abvec_ur tmp[2];
    abvec_ur_init(ab, &tmp[0], c->size);
    abvec_ur_init(ab, &tmp[1], c->size);
    for(unsigned int i = 0 ; i < a->m ; i++) {
        for(unsigned int j = 0 ; j < b->n ; j++) {
            abvec_ur_set_vec(ab, tmp[1], matpoly_part(ab, c, i, j, 0), c->size);
            for(unsigned int k = 0 ; k < a->n ; k++) {
                abvec_conv_ur(ab, tmp[0],
                        matpoly_part(ab, a, i, k, 0), a->size,
                        matpoly_part(ab, b, k, j, 0), b->size);
                abvec_ur_add(ab, tmp[1], tmp[1], tmp[0], c->size);
            }
            abvec_reduce(ab, matpoly_part(ab, c, i, j, 0), tmp[1], c->size);
        }
    }
    abvec_ur_clear(ab, &tmp[0], c->size);
    abvec_ur_clear(ab, &tmp[1], c->size);
}/*}}}*/

void matpoly_mul(abdst_field ab, matpoly c, matpoly a, matpoly b)/*{{{*/
{
    ASSERT_ALWAYS(a->n == b->m);
    if (matpoly_check_pre_init(c)) {
        matpoly_init(ab, c, a->m, b->n, a->size + b->size - 1);
    }
    ASSERT_ALWAYS(c->m == a->m);
    ASSERT_ALWAYS(c->n == b->n);
    ASSERT_ALWAYS(c->alloc >= a->size + b->size - 1);
    c->size = a->size + b->size - 1;
    abvec_set_zero(ab, c->x, c->m * c->n * c->size);
    matpoly_addmul(ab, c, a, b);
}/*}}}*/

void matpoly_addmp(abdst_field ab, matpoly b, matpoly a, matpoly c)/*{{{*/
{
    unsigned int nb = MAX(a->size, c->size) - MIN(a->size, c->size) + 1;
    ASSERT_ALWAYS(a->n == c->m);
    if (matpoly_check_pre_init(b)) {
        matpoly_init(ab, b, a->m, c->n, nb);
    }
    ASSERT_ALWAYS(b->m == a->m);
    ASSERT_ALWAYS(b->n == c->n);
    ASSERT_ALWAYS(b->alloc >= nb);
    b->size = nb;

    /* We are going to make it completely stupid for a beginning. */
    /* XXX XXX XXX FIXME !!! */
    abvec_ur tmp[2];
    abvec_ur_init(ab, &tmp[0], a->size + c->size - 1);
    abvec_ur_init(ab, &tmp[1], b->size);
    for(unsigned int i = 0 ; i < a->m ; i++) {
        for(unsigned int j = 0 ; j < c->n ; j++) {
            abvec_ur_set_vec(ab, tmp[1], matpoly_part(ab, b, i, j, 0), b->size);
            for(unsigned int k = 0 ; k < a->n ; k++) {
                abvec_conv_ur(ab, tmp[0],
                        matpoly_part(ab, a, i, k, 0), a->size,
                        matpoly_part(ab, c, k, j, 0), c->size);
                abvec_ur_add(ab, tmp[1], tmp[1],
                        abvec_ur_subvec(ab, tmp[0], MIN(a->size, c->size) - 1),
                        nb);
            }
            abvec_reduce(ab, matpoly_part(ab, b, i, j, 0), tmp[1], b->size);
        }
    }
    abvec_ur_clear(ab, &tmp[0], a->size + c->size - 1);
    abvec_ur_clear(ab, &tmp[1], b->size);
}/*}}}*/

void matpoly_mp(abdst_field ab, matpoly b, matpoly a, matpoly c)/*{{{*/
{
    unsigned int nb = MAX(a->size, c->size) - MIN(a->size, c->size) + 1;
    ASSERT_ALWAYS(a->n == c->m);
    if (matpoly_check_pre_init(b)) {
        matpoly_init(ab, b, a->m, c->n, nb);
    }
    ASSERT_ALWAYS(b->m == a->m);
    ASSERT_ALWAYS(b->n == c->n);
    ASSERT_ALWAYS(b->alloc >= nb);
    b->size = nb;
    abvec_set_zero(ab, b->x, b->m * b->n * b->size);
    matpoly_addmp(ab, b, a, c);
}/*}}}*/


void matpoly_set_polymat(abdst_field ab MAYBE_UNUSED, matpoly_ptr dst, polymat_srcptr src)
{
    if (!matpoly_check_pre_init(dst))
        matpoly_clear(ab, dst);
    matpoly_init(ab, dst, src->m, src->n, src->size);
    dst->size = src->size;

    for(unsigned int i = 0 ; i < src->m ; i++) {
        for(unsigned int j = 0 ; j < src->n ; j++) {
            for(unsigned int k = 0 ; k < src->size ; k++) {
                abset(ab,
                        matpoly_coeff(ab, dst, i, j, k),
                        polymat_coeff_const(ab, src, i, j, k));
            }
        }
    }
}
