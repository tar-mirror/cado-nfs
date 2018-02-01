#include "cado.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "bwc_config.h"
#include "matmul.h"
#include "matmul-common.h"
#include "mpfq_layer.h"
#include "portability.h"

#include "matmul_facade.h"

/* This extension is used to distinguish between several possible
 * implementations of the product */
#define MM_EXTENSION   "-basic"
#define MM_MAGIC_FAMILY        0xa001UL
#define MM_MAGIC_VERSION       0x1001UL
#define MM_MAGIC (MM_MAGIC_FAMILY << 16 | MM_MAGIC_VERSION)

/* This selects the default behaviour as to which is our best code
 * for multiplying. If this flag is 1, then a multiplication matrix times
 * vector (direction==1) performs best if the in-memory structure
 * reflects the non-transposed matrix. Similarly, a vector times matrix
 * multiplication (direction==0) performs best if the in-memory structure
 * reflects the transposed matrix. When the flag is 1, the converse
 * happens.
 * This flag depends on the implementation, and possibly even on the cpu
 * type under certain circumstances.
 */
#define MM_DIR0_PREFERS_TRANSP_MULT   1

struct matmul_basic_data_s {
    /* repeat the fields from the public interface */
    struct matmul_public_s public_[1];
    /* now our private fields */
    size_t datasize;
    abdst_field xab;
    uint32_t * q;
};

void MATMUL_NAME(clear)(matmul_ptr mm0)
{
    struct matmul_basic_data_s * mm = (struct matmul_basic_data_s *) mm0;
    matmul_common_clear(mm->public_);
    free(mm->q);
    free(mm);
}

matmul_ptr MATMUL_NAME(init)(void* xx, param_list pl, int optimized_direction)
{
    struct matmul_basic_data_s * mm;
    mm = malloc(sizeof(struct matmul_basic_data_s));
    memset(mm, 0, sizeof(struct matmul_basic_data_s));
    mm->xab = xx;

    int suggest = optimized_direction ^ MM_DIR0_PREFERS_TRANSP_MULT;
    mm->public_->store_transposed = suggest;
    if (pl) {
        param_list_parse_int(pl, "mm_store_transposed", 
                &mm->public_->store_transposed);
        if (mm->public_->store_transposed != suggest) {
            fprintf(stderr, "Warning, mm_store_transposed"
                    " overrides suggested matrix storage ordering\n");
        }           
    }   

    return (matmul_ptr) mm;
}

void MATMUL_NAME(build_cache)(matmul_ptr mm0, uint32_t * data)
{
    struct matmul_basic_data_s * mm = (struct matmul_basic_data_s *) mm0;
    ASSERT_ALWAYS(data);

    unsigned int nrows_t = mm->public_->dim[ mm->public_->store_transposed];
    
    uint32_t * ptr = data;
    unsigned int i = 0;

    /* count coefficients */
    for( ; i < nrows_t ; i++) {
        unsigned int weight = 0;
        weight += *ptr;
        mm->public_->ncoeffs += weight;

        /* Perform the data transformation, since it can be done in
         * place.
         */
        uint32_t last = 0;
        ptr++;
        for(unsigned int j = 0 ; j < weight ; j++) {
            uint32_t current = *ptr;
            *ptr++ -= last;
            last = current;
        }
    }

    mm->q = data;

    mm->datasize = nrows_t + mm->public_->ncoeffs;

    ASSERT_ALWAYS(ptr - data == (ptrdiff_t) mm->datasize);
}

int MATMUL_NAME(reload_cache)(matmul_ptr mm0)
{
    FILE * f;
    struct matmul_basic_data_s * mm = (struct matmul_basic_data_s *) mm0;
    f = matmul_common_reload_cache_fopen(sizeof(abelt), mm->public_, MM_MAGIC);
    if (!f) return 0;

    MATMUL_COMMON_READ_ONE32(mm->datasize, f);
    mm->q = malloc(mm->datasize * sizeof(uint32_t));
    MATMUL_COMMON_READ_MANY32(mm->q, mm->datasize, f);
    fclose(f);

    return 1;
}

void MATMUL_NAME(save_cache)(matmul_ptr mm0)
{
    FILE * f;

    struct matmul_basic_data_s * mm = (struct matmul_basic_data_s *) mm0;
    f = matmul_common_save_cache_fopen(sizeof(abelt), mm->public_, MM_MAGIC);
    if (!f) return;

    MATMUL_COMMON_WRITE_ONE32(mm->datasize, f);
    MATMUL_COMMON_WRITE_MANY32(mm->q, mm->datasize, f);

    fclose(f);
}

void MATMUL_NAME(mul)(matmul_ptr mm0, void * xdst, void const * xsrc, int d)
{
    struct matmul_basic_data_s * mm = (struct matmul_basic_data_s *) mm0;
    ASM_COMMENT("multiplication code");
    uint32_t * q = mm->q;
    abdst_field x = mm->xab;
    absrc_vec src = (absrc_vec) xsrc; // typical C const problem.
    abdst_vec dst = xdst;

    if (d == !mm->public_->store_transposed) {
        abvec_set_zero(x, dst, mm->public_->dim[!d]);
        ASM_COMMENT("critical loop");
        for(unsigned int i = 0 ; i < mm->public_->dim[!d] ; i++) {
            uint32_t len = *q++;
            unsigned int j = 0;
            for( ; len-- ; ) {
                j += *q++;
                ASSERT(j < mm->public_->dim[d]);
                abadd(x, dst[i], dst[i], src[j]);
            }
        }
        ASM_COMMENT("end of critical loop");
    } else {
        if (mm->public_->iteration[d] == 10) {
            fprintf(stderr, "Warning: Doing many iterations with transposed code (not a huge problem for impl=basic)\n");
        }
        abvec_set_zero(x, dst, mm->public_->dim[!d]);
        ASM_COMMENT("critical loop (transposed mult)");
        for(unsigned int i = 0 ; i < mm->public_->dim[d] ; i++) {
            uint32_t len = *q++;
            unsigned int j = 0;
            for( ; len-- ; ) {
                j += *q++;
                ASSERT(j < mm->public_->dim[!d]);
                abadd(x, dst[j], dst[j], src[i]);
            }
        }
        ASM_COMMENT("end of critical loop (transposed mult)");
    }
    ASM_COMMENT("end of multiplication code");
    mm->public_->iteration[d]++;
}

void MATMUL_NAME(report)(matmul_ptr mm0 MAYBE_UNUSED, double scale MAYBE_UNUSED) {
}

void MATMUL_NAME(auxv)(matmul_ptr mm0 MAYBE_UNUSED, int op MAYBE_UNUSED, va_list ap MAYBE_UNUSED)
{
}

void MATMUL_NAME(aux)(matmul_ptr mm0, int op, ...)
{
    va_list ap;
    va_start(ap, op);
    MATMUL_NAME(auxv) (mm0, op, ap);
    va_end(ap);
}

/* vim: set sw=4: */
