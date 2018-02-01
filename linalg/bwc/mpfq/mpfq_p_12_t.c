#include "cado.h"
/* MPFQ generated file -- do not edit */

#include "mpfq_p_12_t.h"

/* Active handler: simd_gfp */
/* Automatically generated code  */
/* Active handler: Mpfq::defaults */
/* Active handler: Mpfq::defaults::vec */
/* Active handler: Mpfq::defaults::poly */
/* Active handler: Mpfq::gfp::field */
/* Active handler: Mpfq::gfp::elt */
/* Options used:{
   family=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_12, tag=p_12, }, ],
   fieldtype=prime,
   n=12,
   nn=25,
   opthw=,
   tag=p_12,
   type=plain,
   vbase_stuff={
    choose_byfeatures=<code>,
    families=[
     [ u64k1, u64k2, u64k3, u64k4, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_1, tag=p_1, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_10, tag=p_10, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_11, tag=p_11, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_12, tag=p_12, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_13, tag=p_13, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_14, tag=p_14, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_15, tag=p_15, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_2, tag=p_2, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_3, tag=p_3, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_4, tag=p_4, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_5, tag=p_5, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_6, tag=p_6, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_7, tag=p_7, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_8, tag=p_8, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_9, tag=p_9, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_pz, tag=pz, }, ],
     ],
    member_templates_restrict={
     p_1=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_1, tag=p_1, }, ],
     p_10=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_10, tag=p_10, }, ],
     p_11=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_11, tag=p_11, }, ],
     p_12=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_12, tag=p_12, }, ],
     p_13=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_13, tag=p_13, }, ],
     p_14=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_14, tag=p_14, }, ],
     p_15=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_15, tag=p_15, }, ],
     p_2=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_2, tag=p_2, }, ],
     p_3=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_3, tag=p_3, }, ],
     p_4=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_4, tag=p_4, }, ],
     p_5=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_5, tag=p_5, }, ],
     p_6=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_6, tag=p_6, }, ],
     p_7=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_7, tag=p_7, }, ],
     p_8=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_8, tag=p_8, }, ],
     p_9=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_p_9, tag=p_9, }, ],
     pz=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELD_pz, tag=pz, }, ],
     u64k1=[ u64k1, u64k2, u64k3, u64k4, ],
     u64k2=[ u64k1, u64k2, u64k3, u64k4, ],
     u64k3=[ u64k1, u64k2, u64k3, u64k4, ],
     u64k4=[ u64k1, u64k2, u64k3, u64k4, ],
     },
    vc:includes=[ <stdarg.h>, ],
    },
   virtual_base={
    filebase=mpfq_vbase,
    global_prefix=mpfq_,
    name=mpfq_vbase,
    substitutions=[
     [ (?^:mpfq_p_12_elt \*), void *, ],
     [ (?^:mpfq_p_12_src_elt\b), const void *, ],
     [ (?^:mpfq_p_12_elt\b), void *, ],
     [ (?^:mpfq_p_12_dst_elt\b), void *, ],
     [ (?^:mpfq_p_12_elt_ur \*), void *, ],
     [ (?^:mpfq_p_12_src_elt_ur\b), const void *, ],
     [ (?^:mpfq_p_12_elt_ur\b), void *, ],
     [ (?^:mpfq_p_12_dst_elt_ur\b), void *, ],
     [ (?^:mpfq_p_12_vec \*), void *, ],
     [ (?^:mpfq_p_12_src_vec\b), const void *, ],
     [ (?^:mpfq_p_12_vec\b), void *, ],
     [ (?^:mpfq_p_12_dst_vec\b), void *, ],
     [ (?^:mpfq_p_12_vec_ur \*), void *, ],
     [ (?^:mpfq_p_12_src_vec_ur\b), const void *, ],
     [ (?^:mpfq_p_12_vec_ur\b), void *, ],
     [ (?^:mpfq_p_12_dst_vec_ur\b), void *, ],
     [ (?^:mpfq_p_12_poly \*), void *, ],
     [ (?^:mpfq_p_12_src_poly\b), const void *, ],
     [ (?^:mpfq_p_12_poly\b), void *, ],
     [ (?^:mpfq_p_12_dst_poly\b), void *, ],
     ],
    },
   vtag=p_12,
   w=64,
   } */


/* Functions operating on the field structure */

/* Element allocation functions */

/* Elementary assignment functions */

/* Assignment of random values */

/* Arithmetic operations on elements */

/* Operations involving unreduced elements */

/* Comparison functions */

/* Input/output functions */

/* Vector functions */

/* Polynomial functions */

/* Functions related to SIMD operation */

/* Member templates related to SIMD operation */

/* Object-oriented interface */
/* Mpfq::engine::handler::create_code */
void mpfq_p_12_p_12_wrapper_dotprod(mpfq_vbase_ptr K0 MAYBE_UNUSED, mpfq_vbase_ptr K1 MAYBE_UNUSED, mpfq_p_12_dst_vec xw, mpfq_p_12_src_vec xu1, mpfq_p_12_src_vec xu0, unsigned int n)
{
    mpfq_p_12_p_12_dotprod(K0->obj, K1->obj, xw, xu1, xu0, n);
}

/* *simd_gfp::code_for_member_template_dotprod */
void mpfq_p_12_p_12_dotprod(mpfq_p_12_dst_field K0 MAYBE_UNUSED, mpfq_p_12_dst_field K1 MAYBE_UNUSED, mpfq_p_12_dst_vec xw, mpfq_p_12_src_vec xu1, mpfq_p_12_src_vec xu0, unsigned int n)
{
        mpfq_p_12_elt_ur s,t;
        mpfq_p_12_elt_ur_init(K0, &s);
        mpfq_p_12_elt_ur_init(K0, &t);
        mpfq_p_12_elt_ur_set_zero(K0, s);
        for(unsigned int i = 0 ; i < n ; i++) {
            mpfq_p_12_mul_ur(K0, t, xu0[i], xu1[i]);
            mpfq_p_12_elt_ur_add(K0, s, s, t);
        }
        mpfq_p_12_reduce(K0, xw[0], s);
        mpfq_p_12_elt_ur_clear(K0, &s);
        mpfq_p_12_elt_ur_clear(K0, &t);
}

/* Mpfq::engine::handler::create_code */
void mpfq_p_12_p_12_wrapper_addmul_tiny(mpfq_vbase_ptr K MAYBE_UNUSED, mpfq_vbase_ptr L MAYBE_UNUSED, mpfq_p_12_dst_vec w, mpfq_p_12_src_vec u, mpfq_p_12_dst_vec v, unsigned int n)
{
    mpfq_p_12_p_12_addmul_tiny(K->obj, L->obj, w, u, v, n);
}

/* *simd_gfp::code_for_member_template_addmul_tiny */
void mpfq_p_12_p_12_addmul_tiny(mpfq_p_12_dst_field K MAYBE_UNUSED, mpfq_p_12_dst_field L MAYBE_UNUSED, mpfq_p_12_dst_vec w, mpfq_p_12_src_vec u, mpfq_p_12_dst_vec v, unsigned int n)
{
        mpfq_p_12_elt s;
        mpfq_p_12_init(K, &s);
        for(unsigned int i = 0 ; i < n ; i++) {
            mpfq_p_12_mul(K, s, u[i], v[0]);
            mpfq_p_12_add(K, w[i], w[i], s);
        }
        mpfq_p_12_clear(K, &s);
}

/* Mpfq::engine::handler::create_code */
void mpfq_p_12_p_12_wrapper_transpose(mpfq_vbase_ptr K MAYBE_UNUSED, mpfq_vbase_ptr L MAYBE_UNUSED, mpfq_p_12_dst_vec w, mpfq_p_12_src_vec u)
{
    mpfq_p_12_p_12_transpose(K->obj, L->obj, w, u);
}

/* *simd_gfp::code_for_member_template_transpose */
void mpfq_p_12_p_12_transpose(mpfq_p_12_dst_field K MAYBE_UNUSED, mpfq_p_12_dst_field L MAYBE_UNUSED, mpfq_p_12_dst_vec w, mpfq_p_12_src_vec u)
{
    mpfq_p_12_set(K, w[0], u[0]);
}


/* vim:set ft=cpp: */
