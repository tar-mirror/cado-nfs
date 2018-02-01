#ifndef MPFQ_U64K2_T_H_
#define MPFQ_U64K2_T_H_

/* MPFQ generated file -- do not edit */

#include "mpfq_u64k1.h"
#include "mpfq_u64k2.h"
#include "mpfq_u64k3.h"
#include "mpfq_u64k4.h"
/* Active handler: simd_u64k */
/* Automatically generated code  */
/* Active handler: Mpfq::defaults */
/* Active handler: Mpfq::defaults::vec */
/* Active handler: simd_dotprod */
/* Active handler: io */
/* Active handler: trivialities */
/* Active handler: simd_char2 */
/* Options used:{
   family=[ u64k1, u64k2, u64k3, u64k4, ],
   k=2,
   tag=u64k2,
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
     [ (?^:mpfq_u64k2_elt \*), void *, ],
     [ (?^:mpfq_u64k2_src_elt\b), const void *, ],
     [ (?^:mpfq_u64k2_elt\b), void *, ],
     [ (?^:mpfq_u64k2_dst_elt\b), void *, ],
     [ (?^:mpfq_u64k2_elt_ur \*), void *, ],
     [ (?^:mpfq_u64k2_src_elt_ur\b), const void *, ],
     [ (?^:mpfq_u64k2_elt_ur\b), void *, ],
     [ (?^:mpfq_u64k2_dst_elt_ur\b), void *, ],
     [ (?^:mpfq_u64k2_vec \*), void *, ],
     [ (?^:mpfq_u64k2_src_vec\b), const void *, ],
     [ (?^:mpfq_u64k2_vec\b), void *, ],
     [ (?^:mpfq_u64k2_dst_vec\b), void *, ],
     [ (?^:mpfq_u64k2_vec_ur \*), void *, ],
     [ (?^:mpfq_u64k2_src_vec_ur\b), const void *, ],
     [ (?^:mpfq_u64k2_vec_ur\b), void *, ],
     [ (?^:mpfq_u64k2_dst_vec_ur\b), void *, ],
     [ (?^:mpfq_u64k2_poly \*), void *, ],
     [ (?^:mpfq_u64k2_src_poly\b), const void *, ],
     [ (?^:mpfq_u64k2_poly\b), void *, ],
     [ (?^:mpfq_u64k2_dst_poly\b), void *, ],
     ],
    },
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
void mpfq_u64k2_u64k1_wrapper_dotprod(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k2_dst_vec, mpfq_u64k1_src_vec, mpfq_u64k2_src_vec, unsigned int);
void mpfq_u64k2_u64k1_dotprod(mpfq_u64k2_dst_field, mpfq_u64k1_dst_field, mpfq_u64k2_dst_vec, mpfq_u64k1_src_vec, mpfq_u64k2_src_vec, unsigned int);
void mpfq_u64k2_u64k2_wrapper_dotprod(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k2_dst_vec, mpfq_u64k2_src_vec, mpfq_u64k2_src_vec, unsigned int);
void mpfq_u64k2_u64k2_dotprod(mpfq_u64k2_dst_field, mpfq_u64k2_dst_field, mpfq_u64k2_dst_vec, mpfq_u64k2_src_vec, mpfq_u64k2_src_vec, unsigned int);
void mpfq_u64k2_u64k3_wrapper_dotprod(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k2_dst_vec, mpfq_u64k3_src_vec, mpfq_u64k2_src_vec, unsigned int);
void mpfq_u64k2_u64k3_dotprod(mpfq_u64k2_dst_field, mpfq_u64k3_dst_field, mpfq_u64k2_dst_vec, mpfq_u64k3_src_vec, mpfq_u64k2_src_vec, unsigned int);
void mpfq_u64k2_u64k4_wrapper_dotprod(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k2_dst_vec, mpfq_u64k4_src_vec, mpfq_u64k2_src_vec, unsigned int);
void mpfq_u64k2_u64k4_dotprod(mpfq_u64k2_dst_field, mpfq_u64k4_dst_field, mpfq_u64k2_dst_vec, mpfq_u64k4_src_vec, mpfq_u64k2_src_vec, unsigned int);
void mpfq_u64k2_u64k1_wrapper_addmul_tiny(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k1_dst_vec, mpfq_u64k2_src_vec, mpfq_u64k1_dst_vec, unsigned int);
void mpfq_u64k2_u64k1_addmul_tiny(mpfq_u64k2_dst_field, mpfq_u64k1_dst_field, mpfq_u64k1_dst_vec, mpfq_u64k2_src_vec, mpfq_u64k1_dst_vec, unsigned int);
void mpfq_u64k2_u64k2_wrapper_addmul_tiny(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k2_dst_vec, mpfq_u64k2_src_vec, mpfq_u64k2_dst_vec, unsigned int);
void mpfq_u64k2_u64k2_addmul_tiny(mpfq_u64k2_dst_field, mpfq_u64k2_dst_field, mpfq_u64k2_dst_vec, mpfq_u64k2_src_vec, mpfq_u64k2_dst_vec, unsigned int);
void mpfq_u64k2_u64k3_wrapper_addmul_tiny(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k3_dst_vec, mpfq_u64k2_src_vec, mpfq_u64k3_dst_vec, unsigned int);
void mpfq_u64k2_u64k3_addmul_tiny(mpfq_u64k2_dst_field, mpfq_u64k3_dst_field, mpfq_u64k3_dst_vec, mpfq_u64k2_src_vec, mpfq_u64k3_dst_vec, unsigned int);
void mpfq_u64k2_u64k4_wrapper_addmul_tiny(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k4_dst_vec, mpfq_u64k2_src_vec, mpfq_u64k4_dst_vec, unsigned int);
void mpfq_u64k2_u64k4_addmul_tiny(mpfq_u64k2_dst_field, mpfq_u64k4_dst_field, mpfq_u64k4_dst_vec, mpfq_u64k2_src_vec, mpfq_u64k4_dst_vec, unsigned int);
void mpfq_u64k2_u64k1_wrapper_transpose(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k2_dst_vec, mpfq_u64k1_src_vec);
void mpfq_u64k2_u64k1_transpose(mpfq_u64k2_dst_field, mpfq_u64k1_dst_field, mpfq_u64k2_dst_vec, mpfq_u64k1_src_vec);
void mpfq_u64k2_u64k2_wrapper_transpose(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k2_dst_vec, mpfq_u64k2_src_vec);
void mpfq_u64k2_u64k2_transpose(mpfq_u64k2_dst_field, mpfq_u64k2_dst_field, mpfq_u64k2_dst_vec, mpfq_u64k2_src_vec);
void mpfq_u64k2_u64k3_wrapper_transpose(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k2_dst_vec, mpfq_u64k3_src_vec);
void mpfq_u64k2_u64k3_transpose(mpfq_u64k2_dst_field, mpfq_u64k3_dst_field, mpfq_u64k2_dst_vec, mpfq_u64k3_src_vec);
void mpfq_u64k2_u64k4_wrapper_transpose(mpfq_vbase_ptr, mpfq_vbase_ptr, mpfq_u64k2_dst_vec, mpfq_u64k4_src_vec);
void mpfq_u64k2_u64k4_transpose(mpfq_u64k2_dst_field, mpfq_u64k4_dst_field, mpfq_u64k2_dst_vec, mpfq_u64k4_src_vec);

#endif  /* MPFQ_U64K2_T_H_ */

/* vim:set ft=cpp: */
