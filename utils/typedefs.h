#ifndef	CADO_TYPEDEFS_H_
#define	CADO_TYPEDEFS_H_

#include <inttypes.h>
#include "ularith.h" /* NEEDED for LONG_BIT (32 or 64) */

/* data type to store the (p,r) values */
#ifndef __SIZEOF_P_R_VALUES__
#define __SIZEOF_P_R_VALUES__ 4
#elif __SIZEOF_P_R_VALUES__ != 4 && __SIZEOF_P_R_VALUES__ != 8
#error "Defined constant __SIZEOF_P_R_VALUES__ should be 4 or 8"
#endif

/* data type to store the renumber table */
#ifndef __SIZEOF_INDEX__
#define __SIZEOF_INDEX__ 4
#elif __SIZEOF_INDEX__ < 4 || 8 < __SIZEOF_INDEX__
#error "Defined constant __SIZEOF_INDEX__ should be in [4..8]"
#endif

#if __SIZEOF_INDEX__ > __SIZEOF_P_R_VALUES__
#error "__SIZEOF_INDEX__ should be smaller or equal to __SIZEOF_P_R_VALUES__"
#endif

#if (__SIZEOF_P_R_VALUES__ * 8) > LONG_BIT
#error "__SIZEOF_P_R_VALUES__ cannot be greater than LONG_BIT / 8"
#endif

#if __SIZEOF_P_R_VALUES__ == 4
#define p_r_values_t uint32_t
#define PRpr PRIx32
#define SCNpr SCNx32
#else /* __SIZEOF_P_R_VALUES__ == 8 */
#define p_r_values_t uint64_t
#define PRpr PRIx64
#define SCNpr SCNx64
#endif

#if __SIZEOF_INDEX__ == 4
#define index_t uint32_t
#define index_signed_t int32_t
#define PRid PRIx32
#define SCNid SCNx32
#else /* __SIZEOF_INDEX__ == 8 */
#define index_t uint64_t
#define index_signed_t int64_t
#define PRid PRIx64
#define SCNid SCNx64
#endif 

/* The weight of ideals saturates at 255 */
/* For relations, we hope that there will never be more */
/* than 255 ideals per relation */
#define weight_t uint8_t
#define exponent_t int8_t
#define REL_MAX_SIZE 255

typedef struct {
  index_t h;
  p_r_values_t p;
  exponent_t e;
  uint8_t side;
} prime_t;

typedef struct {
  index_t id;
  int32_t e;
} ideal_merge_t;

typedef struct {
  uint64_t nrows;
  uint64_t ncols;
  double W; /* weight of the active part of the matrix */
} info_mat_t;

#endif	/* CADO_TYPEDEFS_H_ */
