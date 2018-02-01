#include "cado.h"

/* This is a very silly program which merely reads the matrix and then
 * exits. It must come before the prep program, and coalescing this one
 * and prep into one would be difficult and/or artificial, because they
 * handle different data widths.
 */
#include <stdint.h>     /* AIX wants it first (it's a bug) */
#include <stdio.h>
#include <string.h>
#include "bwc_config.h"
#include "parallelizing_info.h"
#include "matmul_top.h"
#include "select_mpi.h"
#include "params.h"
#include "portability.h"
#include "misc.h"
#include "bw-common.h"
#include "async.h"
#include "mpfq/mpfq.h"
#include "mpfq/mpfq_vbase.h"
#include "cheating_vec_init.h"

void * dispatch_prog(parallelizing_info_ptr pi, param_list pl, void * arg MAYBE_UNUSED)
{
    matmul_top_data mmt;

    int ys[2] = { bw->ys[0], bw->ys[1], };
    /*
     * Hmm. Interleaving doesn't make a lot of sense for this program,
     * right ? Furthermore, it gets in the way for the sanity checks. We
     * tend to always receive ys=0..64 as an argument.
    if (pi->interleaved) {
        ASSERT_ALWAYS((bw->ys[1]-bw->ys[0]) % 2 == 0);
        ys[0] = bw->ys[0] + pi->interleaved->idx * (bw->ys[1]-bw->ys[0])/2;
        ys[1] = ys[0] + (bw->ys[1]-bw->ys[0])/2;
    }
    */

    mpfq_vbase A;
    mpfq_vbase_oo_field_init_byfeatures(A, 
            MPFQ_PRIME_MPZ, bw->p,
            MPFQ_GROUPSIZE, ys[1]-ys[0],
            MPFQ_DONE);

    block_control_signals();

    /*****************************************
     *             Watch out !               *
     *****************************************/
     
    /* HERE is the place where something actually happens. The rest of
     * this function are just sanity checks. Matrix dispatch can, in
     * fact, be done from any program which does matmul_top_init. This
     * function calls mmt_finish_init, which calls
     * matmul_top_read_submatrix, which eventuallmy,a!ls
     * balancing_get_matrix_u32, which hooks into the balancing code.
     * This is UNLESS either of the two command-line arguments are set,
     * because these trigger special behaviour:
     * export_cachelist : see this file, in fact. This makes "dispatch" a
     * quick cache file listing tool, which helps the perl script a bit.
     * random_matrix_size : for creating test matrices, essentially for
     * krylov/mksol speed testing.
     */
    matmul_top_init(mmt, A, pi, pl, bw->dir);

    mmt_vec ymy[2];
    mmt_vec_ptr y = ymy[0];
    mmt_vec_ptr my = ymy[1];
    mmt_vec_init(mmt,0,0, y,  1, 0, mmt->n[1]);
    mmt_vec_init(mmt,0,0, my, 0, 0, mmt->n[0]);

    unsigned int unpadded = MAX(mmt->n0[0], mmt->n0[1]);

    const char * tmp = param_list_lookup_string(pl, "sanity_check_vector");
    int only_export = param_list_lookup_string(pl, "export_cachelist") != NULL;

    // in no situation shall we try to do our sanity check if we've just
    // been told to export our cache list. Note also that this sanity
    // check is currently only valid for GF(2).
    if (tmp != NULL && !only_export && mpz_cmp_ui(bw->p, 2) == 0) {
        /* We have computed a sanity check vector, which is H=M*K, with K
         * constant and easily given. Note that we have not computed K*M,
         * but really M*K. Thus independently of which side we prefer, we
         * are going to check the matrix product in a rigid direction.
         *
         * check 1: compute M*K again using the mmt structures, and
         * compare with H1. This is matmul_top_mul(mmt, 1).
         *
         * check 2: for some vector L, compute (L, H=M*K) and (L*M, K).
         * This means matmul_top_mul(mmt, 0).
         */
        const char * checkname;

        checkname = "1st check: consistency of M*arbitrary1 (Hx == H1)";

        mmt_full_vec_set_zero(y);
        ASSERT_ALWAYS(y->siblings);     /* shared vector undesired */
        for(unsigned int i = y->i0 ; i < y->i1 && i < unpadded ; i++) {
            void * dst = A->vec_subvec(A, y->v, i - y->i0);
            uint64_t value = DUMMY_VECTOR_COORD_VALUE(i);
            memcpy(dst, &value, sizeof(uint64_t));
        }
        mmt_vec_twist(mmt, y);
        matmul_top_mul(mmt, ymy, NULL);
        mmt_vec_untwist(mmt, y);

        mmt_vec_save(y, "Hx", unpadded);

        // compare if files are equal.
        if (pi->m->jrank == 0 && pi->m->trank == 0) {
            char cmd[1024];
            int rc = snprintf(cmd, 80, "diff -q %s Hx", tmp);
            ASSERT_ALWAYS(rc>=0);
            rc = system(cmd);
            if (rc) {
                printf("%s : failed\n", checkname);
#ifdef WEXITSTATUS
                fprintf(stderr, "%s returned %d\n", cmd, WEXITSTATUS(rc));
#else
                fprintf(stderr, "%s returned %d\n", cmd, rc);
#endif
                exit(EXIT_FAILURE);
            } else {
                printf("%s : ok\n", checkname);
            }
        }
        serialize(pi->m);

        checkname = "2nd check: (arbitrary2, M*arbitrary1) == (arbitrary2*M==Hy, arbitrary1)";

        mmt_full_vec_set_zero(my);
        ASSERT_ALWAYS(my->siblings);     /* shared vector undesired */
        for(unsigned int i = my->i0 ; i < my->i1 && i < unpadded ; i++) {
            void * dst = A->vec_subvec(A, my->v, i - my->i0);
            uint64_t value = DUMMY_VECTOR_COORD_VALUE2(i);
            memcpy(dst, &value, sizeof(uint64_t));
        }
        /* This is L. Now compute the dot product. */
        void * dp0;
        void * dp1;
        cheating_vec_init(A, &dp0, A->groupsize(A));
        cheating_vec_init(A, &dp1, A->groupsize(A));
        unsigned int how_many;
        unsigned int offset_c;
        unsigned int offset_v;
        how_many = intersect_two_intervals(&offset_c, &offset_v,
                my->i0, my->i1,
                y->i0, y->i1);
        A->dotprod(A, dp0,
                A->vec_subvec(A, my->v, offset_c),
                A->vec_subvec(A, y->v, offset_v),
                how_many);
        pi_allreduce(NULL, dp0, A->groupsize(A), mmt->pitype, BWC_PI_SUM, pi->m);

        /* now we can throw away Hx */

        /* we do a transposed multiplication, here. It's a bit of a
         * quirk, admittedly. We need to build a reversed vector list.
         */
        mmt_vec_twist(mmt, my);
        {
            mmt_vec myy[2];
            mmt_vec_init(mmt,0,0, myy[0],  0, 0, mmt->n[0]);
            mmt_vec_init(mmt,0,0, myy[1],  1, 0, mmt->n[1]);
            mmt_full_vec_set(myy[0], my);
            matmul_top_mul(mmt, myy, NULL);
            mmt_full_vec_set(my, myy[0]);
            mmt_vec_clear(mmt, myy[0]);
            mmt_vec_clear(mmt, myy[1]);
        }
        mmt_vec_untwist(mmt, my);
        mmt_vec_save(my, "Hy", unpadded);

        mmt_full_vec_set_zero(y);
        ASSERT_ALWAYS(y->siblings);     /* shared vector undesired */
        for(unsigned int i = y->i0 ; i < y->i1 && i < unpadded ; i++) {
            void * dst = A->vec_subvec(A, y->v, i - y->i0);
            uint64_t value = DUMMY_VECTOR_COORD_VALUE(i);
            memcpy(dst, &value, sizeof(uint64_t));
        }
        A->dotprod(A, dp1,
                A->vec_subvec(A, my->v, offset_c),
                A->vec_subvec(A, y->v, offset_v),
                how_many);
        pi_allreduce(NULL, dp1, A->groupsize(A), mmt->pitype, BWC_PI_SUM, pi->m);
        int diff = memcmp(dp0, dp1, A->vec_elt_stride(A, A->groupsize(A)));
        if (pi->m->jrank == 0 && pi->m->trank == 0) {
            if (diff) {
                printf("%s : failed\n", checkname);
                fprintf(stderr, "aborting on sanity check failure.\n");
                exit(1);
            }
            printf("%s : ok\n", checkname);
        }
        cheating_vec_clear(A, &dp0, A->groupsize(A));
        cheating_vec_clear(A, &dp1, A->groupsize(A));
    }

    mmt_vec_clear(mmt, y);
    mmt_vec_clear(mmt, my);
    matmul_top_clear(mmt);

    A->oo_field_clear(A);

    return NULL;
}


int main(int argc, char * argv[])
{
    param_list pl;

    bw_common_init(bw, &argc, &argv);
    param_list_init(pl);
    parallelizing_info_init();

    bw_common_decl_usage(pl);
    parallelizing_info_decl_usage(pl);
    matmul_top_decl_usage(pl);
    /* declare local parameters and switches */

    bw_common_parse_cmdline(bw, pl, &argc, &argv);

    bw_common_interpret_parameters(bw, pl);
    parallelizing_info_lookup_parameters(pl);
    matmul_top_lookup_parameters(pl);
    /* interpret our parameters: none here (so far). */

    ASSERT_ALWAYS(param_list_lookup_string(pl, "ys"));
    ASSERT_ALWAYS(!param_list_lookup_string(pl, "solutions"));

    if (param_list_warn_unused(pl)) {
        param_list_print_usage(pl, bw->original_argv[0], stderr);
        exit(EXIT_FAILURE);
    }
    // param_list_clear(pl);

    if (bw->ys[0] < 0) { fprintf(stderr, "no ys value set\n"); exit(1); }

    /* Forcibly disable interleaving here */
    param_list_remove_key(pl, "interleaving");

    catch_control_signals();
    pi_go(dispatch_prog, pl, 0);

    parallelizing_info_finish();
    param_list_clear(pl);
    bw_common_clear(bw);

    return 0;
}

