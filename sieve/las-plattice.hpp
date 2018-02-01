#ifndef LAS_PLATTICE_H
#define LAS_PLATTICE_H 1

/*******************************************************************/
/********        Walking in p-lattices                    **********/

/*  p-lattice stuff */


// Compute the root r describing the lattice inside the q-lattice
// corresponding to the factor base prime (p,R).
// Formula: r = - (a1-R*b1)/(a0-R*b0) mod p
// Assumes p < 2^32

/* General version of the lattice transform function. Allows projective
   roots in input and output, and handles prime powers.
   In input, if the root is projective, say s/t (mod p) and t is
   non-invertible (mod p), then we expect R = p + (t/s mod p).
   On output, if the root is projective, say u/v (mod p) and v is
   non-invertible (mod p), then return value r = p + (v/u mod p).
   So projective roots are stored as their reciprocal, and have p added
   to signal the fact that it's a reciprocal value.
*/


/*
 * Algorithm by Franke and Kleinjung for lattice sieving of largish
 * primes.
 */

// Proposition 1 of [FrKl05]:
// Compute a basis <(alpha, beta), (gamma, delta)> of the p-lattice
// inside the q-lattice, such that
//    beta, delta > 0
//    -I < alpha <= 0 <= gamma < I
//    gamma-alpha >= I
//
// Sizes:
//    p is less than 32 bits and I fits easily in 32 bits.
//    So, alpha and beta fit easily in 32 bits, since they are less than I
//    Now, gamma and delta are also bounded by p, so 32 bits is enough
//    However: a and c can be as large as p*I (not both ?).
//    We still store them in 32 bits, since if they are larger, it means
//    that as soon as they are added to the offset for S, the index will
//    be out of range for S and the loop stops. Hence, this is safe to
//    replace a and c by a large value within 32 bits, when they are
//    larger than 32 bits.
//    Except that the choice of this ``large value'' requires some
//    caution. We need a value which can be used either for a or c, or
//    both, so that adding a, or c, or both, to a value within [0,IJ[ is
//    guaranteed to exceed IJ, but can't wrap around. Up to I=15, it's
//    rather easy. With the rescaling of J, at worst we may have IJ
//    within the range [2^29,2^30[. Thus if a and c are set to 2^30-1,
//    a.k.a INT32_MAX/2, then adding either, or the sum of both, to a
//    valid value x is guaranteed to be at most 3*2^30, which fits within
//    32 bits.
//    For I=16, it's much, much harder. Unless somebody comes up with a
//    nice idea, I see no way to avoid 64-bit arithmetic (which has some
//    cost, sure, but not terribly expensive). For consistency, we make
//    all data types for x, a, and c 64-bit in this case, and choose the
//    overflow constants as UINT32_MAX.
//

// OBSOLETE: the large value is now UINT64MAX>>2. So, plattice_x_t must
// be 64 bits. We could deal until logI <= 31 when logJ = logI -1.


// Return value:
// * non-zero if everything worked ok
// * zero when the algorithm failed. This can happen when p is a prime power,
//   and g, gcd(p,r) >= I, since then the subtractive Euclidean algorithm will
//   yield (a0=g, b0=0) at some point --- or the converse --- and the loop
//   while (|a0| >= I) a0 += b0 will loop forever.
//
// Note that on a c166 example, this code alone accounts for almost 20%
// of the computation time.


typedef uint64_t plattice_x_t;

struct plattice_info_t;
static int reduce_plattice (plattice_info_t *, fbprime_t, fbroot_t, uint32_t);


/* Information on a p-lattice: lattice basis coordinates, and auxiliary
   values that can be derived directly from them, such as the constants
   used by the Franke-Kleinjung algorithm */
struct plattice_info_t {
#if MOD2_CLASSES_BS
  static const int shift = 1;
#else
  static const int shift = 0;
#endif

  int32_t a0;
  uint32_t a1;
  int32_t b0;
  uint32_t b1;

  plattice_info_t(const fbprime_t p, const fbroot_t r, const bool proj, const int logI) {
    if (UNLIKELY(proj && r == 0)) {
      /* This lattice basis might work in principle, but it generates hits in
         all locations i=1, ..., I/2-1, j = 0, all of which are useless except
         i=1, j=0.  */
      a1 = p;
      a0 = -((int32_t)1 << logI) + 1;
      b1 = 0;
      b0 = 1;
    } else if (UNLIKELY(!proj && r == 0)) {
      a1 = ((int32_t)1 << logI) + 1;
      a0 = -p; /* Thus bound0 = p > I, inc_c is always added */
      b1 = 1;
      b0 = 0; /* Thus bound1 = I - b0 = I, inc_a is never added */
    } else {
      ASSERT_ALWAYS(!proj);
      int rc = reduce_plattice (this, p, r, 1U << logI);
      if (UNLIKELY(rc == 0)) {
        /* gcd(r, p) > I. We currently can't handle this case. Set everything
           to zero to signal to calling code that this is not a valid basis.
           Is there a better way? Exception, maybe? */
        a0 = a1 = b0 = b1 = 0;
      }
    }
  }

  /* Return the four coordinates, but multiplied by 2 if we use mod 2 classes */
  int32_t get_a0() const {return a0 << shift;}
  uint32_t get_a1() const {return a1 << shift;}
  int32_t get_b0() const {return b0 << shift;}
  uint32_t get_b1() const {return b1 << shift;}

  uint32_t get_bound0(const int logI MAYBE_UNUSED) const {
      return -get_a0();
  }

  uint32_t get_bound1(const int logI) const {
      return (1U << logI) - get_b0();
  }

  plattice_x_t get_inc_a(const int logI) const {
      return ((uint64_t)get_a1() << logI) + (int64_t)get_a0();
  }

  plattice_x_t get_inc_c(const int logI) const {
    return ((uint64_t)get_b1() << logI) + (int64_t)get_b0();
  }

  uint32_t det() const {return (-a0)*b1 + a1*b0;};
};

/* We have several versions of reduce_plattice. Which one is best is
 * reportedly CPU-dependent, which I can imagine.
 *
 * I've run some benchmarks on a skylake i5 in 2017/05 with gcc-6.3.0.
 * The C version below is almost on par with the unrolled version further
 * down, but the latter still has a tiny edge nevertheless. We seem to be
 * well within the margin of error at this point, so more tests would be
 * useful.
 *
 * (which unrolling choice for the C code get the best performance is not
 * measurable at this point).
 */
#if 0
// Original version of reduce_plattice, always with a division for each step.
// This version is pretty fast on new powerful processors, but slow on others.
// I keep it because in fews years, if the int32_t division is faster, it's
// possible this code might be the fastest.

NOPROFILE_INLINE int
reduce_plattice (plattice_info_t *pli, const fbprime_t p, const fbroot_t r, uint32_t I)
{
  int32_t a0 = -((int32_t) p), b0 = (int32_t) r, a1 = 0, b1 = 1, k;
#if MOD2_CLASSES_BS
  const int32_t hI = (int32_t) (I>>1);
#else
  const int32_t hI = (int32_t) I;
#endif
  const int32_t mhI = -hI;
  while (LIKELY(b0 >= hI)) {
    k = a0 / b0; a0 %= b0; a1 -= k * b1;
    if (UNLIKELY(a0 > mhI)) break;
    k = b0 / a0; b0 %= a0; b1 -= k * a1;
    /* We may conceivably unroll a bit more, or a bit less, here. Just
     * tuck in as many copies of the following block as you wish. */
#if 1
    if (UNLIKELY(b0 < hI )) break;
    k = a0 / b0; a0 %= b0; a1 -= k * b1;
    if (UNLIKELY(a0 > mhI)) break;
    k = b0 / a0; b0 %= a0; b1 -= k * a1;
#endif
  }
  k = b0 - hI - a0;
  if (b0 > -a0) {
    if (UNLIKELY(!a0)) return 0;
    k /= a0; b0 -= k * a0; b1 -= k * a1;
  } else {
    if (UNLIKELY(!b0)) return 0;
    k /= b0; a0 += k * b0; a1 += k * b1;
  }
  pli->a0 = (int32_t) a0; pli->a1 = (uint32_t) a1; pli->b0 = (int32_t) b0; pli->b1 = (uint32_t) b1;
  return 1;
}
#else
// The really fastest version of reduce_plattice on processors >= Intel
// Nehalem & AMD Opteron.
// The C version is for others architectures; it's almost (99%) faster
// than the asm version on X86 with a GOOD C compiler (tested with gcc
// 4.6.X, 4.7.X, 4.8.X).
// Avoid to modify this code...
// This version has been tuned during several weeks; more than 50
// versions of this routine has been tested ever and ever. A. Filbois.

// Main idea: to avoid a division & multiplication, I tried to test the
// sign of aO + b0 * 4 (resp. b0 + a0 * 4). If this sign is different
// than a0 (resp. b0) sign, the quotient of a0 / b0 is between 0 and 3,
// and needs (4-1)*2 conditional affectations.
// The best optimisation is the right choice of the number of the
// conditional affectations. It's not exactly the same between Intel
// Nehalem & AMD Opteron, but this code is the best deal for both.
//
// NB: Be careful if you tried to change the "4" value. You have to
// change the constants guards 0xe666667 = -0x7FFFFFFF/(4+1) & 0x19999999
// = 0x7fffffff / (4+1).
// These guards are needed because the conditional affectations use the
// sign change; this sign may changed at max 1 times, not 2 (with an
// overflow).  In fact, these guards are 99.99999% useless.

NOPROFILE_INLINE int
reduce_plattice (plattice_info_t *pli, const fbprime_t p, const fbroot_t r, const uint32_t I)
{
#if MOD2_CLASSES_BS
  const int32_t hI = (int32_t) (I >> 1);
#else
  const int32_t hI = (int32_t) I;
#endif
  int32_t a0 = - (int32_t) p, b0 = (int32_t) r, a1, b1;

  /* Mac OS X 10.8 embarks a version of llvm which crashes on the code
   * below (could be that the constraints are exerting too much of the
   * compiler's behaviour).
   *
   * See tracker #16540
   */
#ifdef HAVE_GCC_STYLE_AMD64_INLINE_ASM
#if defined(__APPLE_CC__) && defined(__llvm__)
#if __APPLE_CC__ == 5621
#define AVOID_ASM_REDUCE_PLATTICE
#endif
#endif
#else
#define AVOID_ASM_REDUCE_PLATTICE
#endif

#ifndef AVOID_ASM_REDUCE_PLATTICE
#define RPA(LABEL)      \
          "addl %2, %0\n"						\
          "leal (%0,%2,4), %%edx\n"					\
          "addl %3, %1\n"						\
          "testl %%edx, %%edx\n"					\
          "movl %0, %%eax\n"						\
          "jng " LABEL "\n"						\
          "addl %2, %%eax\n"						\
          "leal (%1,%3,1), %%edx\n"					\
          "cmovngl %%eax, %0\n"						\
          "cmovngl %%edx, %1\n"						\
          "addl %3, %%edx\n"						\
          "addl %2, %%eax\n"						\
          "cmovngl %%edx, %1\n"						\
          "cmovngl %%eax, %0\n"						\
          "addl %3, %%edx\n"						\
          "addl %2, %%eax\n"						\
          "cmovngl %%edx, %1\n"						\
          "cmovngl %%eax, %0\n"
#define RPB(LABEL)      \
          "addl %0, %2\n"						\
          "leal (%2,%0,4), %%edx\n"					\
          "addl %1, %3\n"						\
          "testl %%edx, %%edx\n"					\
          "movl %2, %%eax\n"						\
          "jns " LABEL "\n"						\
          "addl %0, %%eax\n"						\
          "leal (%1,%3,1), %%edx\n"					\
          "cmovnsl %%eax, %2\n"						\
          "cmovnsl %%edx, %3\n"						\
          "addl %1, %%edx\n"						\
          "addl %0, %%eax\n"						\
          "cmovnsl %%edx, %3\n"						\
          "cmovnsl %%eax, %2\n"						\
          "addl %1, %%edx\n"						\
          "addl %0, %%eax\n"						\
          "cmovnsl %%edx, %3\n"						\
          "cmovnsl %%eax, %2\n"
#define RPC     \
          "cltd\n"							\
          "idivl %2\n"							\
          "imull %3, %%eax\n"						\
          "movl %%edx, %0\n"						\
          "subl %%eax, %1\n"
#define RPD "cltd\n"    \
          "idivl %0\n"							\
          "imull %1, %%eax\n"						\
          "movl %%edx, %2\n"						\
          "subl %%eax, %3\n"

  int32_t mhI;
  __asm__ __volatile__ (
            "xorl %1, %1\n"
            "cmpl %2, %5\n"
            "movl $0x1, %3\n"
            "jg 9f\n"
            "movl %5, %%eax\n"
            "negl %%eax\n"
            "movl %%eax, %4\n"
            "movl %0, %%eax\n"
            "cltd\n"
            "idivl %2\n"
            "subl %%eax, %1\n"
            "cmpl $0xe6666667, %%edx\n"
            "movl %%edx, %0\n"
            "jl 0f\n"
            ".balign 8\n"
            "1:\n"
            "cmpl %0, %4\n"
            "jl 9f\n"
            RPB("3f")
            "2:\n"
            "cmpl %2, %5\n"
            "jg 9f\n"
            RPA("4f")
            "jmp 1b\n"
            ".balign 8\n"
            "3:\n"
            RPD
            "jmp 2b\n"
            ".balign 8\n"
            "4:\n"
            RPC
            "jmp 1b\n"
            ".balign 8\n"
            "0:\n"
            "movl %2, %%eax\n"
            "cltd\n"
            "idivl %0\n"
            "imull %1, %%eax\n"
            "subl %%eax, %3\n"
            "cmpl $0x19999999, %%edx\n"
            "movl %%edx, %2\n"
            "jle 2b\n"
            "movl %0, %%eax\n"
            "cltd\n"
            "idivl %2\n"
            "imull %3, %%eax\n"
            "subl %%eax, %1\n"
            "cmpl $0xe6666667, %%edx\n"
            "movl %%edx, %0\n"
            "jge 1b\n"
            "jmp 0b\n"
            "9:\n"
	   : "+&r"(a0), "=&r"(a1), "+&r"(b0), "=&r"(b1),
	     "=&rm"(mhI) : "rm"(hI) : "%rax", "%rdx", "cc");
#else
#define RPA do {							\
    a0 += b0; a1 += b1;							\
    if (LIKELY(a0 + b0 * 4 > 0)) {					\
      int32_t c0 = a0, c1 = a1;						\
      c0 += b0; c1 += b1; if (LIKELY(c0 <= 0)) { a0 = c0; a1 = c1; }	\
      c0 += b0; c1 += b1; if (LIKELY(c0 <= 0)) { a0 = c0; a1 = c1; }	\
      c0 += b0; c1 += b1; if (LIKELY(c0 <= 0)) { a0 = c0; a1 = c1; }	\
    } else								\
      RPC;								\
  } while (0)
#define RPB do {							\
    b0 += a0; b1 += a1;							\
    if (LIKELY(b0 + a0 * 4 < 0)) {					\
      int32_t c0 = b0, c1 = b1;						\
      c0 += a0; c1 += a1; if (LIKELY(c0 >= 0)) { b0 = c0; b1 = c1; }	\
      c0 += a0; c1 += a1; if (LIKELY(c0 >= 0)) { b0 = c0; b1 = c1; }	\
      c0 += a0; c1 += a1; if (LIKELY(c0 >= 0)) { b0 = c0; b1 = c1; }	\
    } else								\
      RPD;								\
    } while (0)
#define RPC do {					\
    int32_t k = a0 / b0; a0 %= b0; a1 -= k * b1;	\
  } while (0)
#define RPD do {					\
    int32_t k = b0 / a0; b0 %= a0; b1 -= k * a1;	\
  } while (0)

  /* This code seems odd (looks after the a0 <= mhI loop),
     but gcc generates the fastest asm with it... */
  a1 = 0; b1 = 1;
  if (LIKELY(b0 >= hI)) {
    const int32_t mhI = -hI;
    RPC;
    while (UNLIKELY(a0 < -0X7FFFFFFF / 5)) {
      RPD;
      if (UNLIKELY(b0 < 0X7FFFFFFF / 5)) goto p15;
      RPC;
    }
    if (LIKELY(a0 <= mhI))
      do {
	RPB;
      p15:
	if (UNLIKELY(b0 < hI)) break;
	RPA;
      } while (LIKELY(a0 <= mhI));
  }
#endif /* HAVE_GCC_STYLE_AMD64_INLINE_ASM */

  int32_t k = b0 - hI - a0;
  if (b0 > -a0) {
    if (UNLIKELY(!a0)) return 0;
    k /= a0; b0 -= k * a0; b1 -= k * a1;
  } else {
    if (UNLIKELY(!b0)) return 0;
    k /= b0; a0 += k * b0; a1 += k * b1;
  }
  pli->a0 = (int32_t) a0; pli->a1 = (uint32_t) a1; pli->b0 = (int32_t) b0; pli->b1 = (uint32_t) b1;
  return 1;
}
#endif


/* Like plattice_info_t, but remembers the offset of the factor base entry
   that generated each lattice basis. This offset becomes the "prime hint"
   in the bucket updates. */
struct plattice_sieve_entry : public plattice_info_t {
  slice_offset_t hint;
  plattice_sieve_entry(const fbprime_t p, const fbroot_t r, const bool proj, const int logI, const slice_offset_t hint)
     : plattice_info_t(p, r, proj, logI), hint(hint) {};
};

template <int LEVEL>
class plattice_enumerate_area {
public:
    static plattice_x_t value;
};

/* see bug #21405 */
template <> plattice_x_t plattice_enumerate_area<1>::value;
template <> plattice_x_t plattice_enumerate_area<2>::value;
template <> plattice_x_t plattice_enumerate_area<3>::value;

template <int LEVEL>
bool
plattice_enumerate_finished(plattice_x_t x)
{ return x >= plattice_enumerate_area<LEVEL>::value; }


/* Class for enumerating lattice points with the Franke-Kleinjung algorithm */
// Note: we would have liked to template this class by LEVEL.
// However, since it is used by fb.h in a place that convert slices, and
// slices does not know its level, we do the ugly workaround that puts
// area outside this class.
// TODO: can we do better in terms of structure of the code?
class plattice_enumerate_t {
protected:
    // Maybe at some point, the plattice_x_t type could be templated in
    // order to have it 32 bits for non-top levels.
    plattice_x_t inc_a, inc_c;
    uint32_t bound0, bound1;
    slice_offset_t hint;
    plattice_x_t x;

    static uint32_t maskI;
    static plattice_x_t even_mask;
public:
    static void set_masks(const int _logI) {
        maskI = (1U << _logI) - 1U;
        even_mask = (plattice_x_t(1) << _logI) | plattice_x_t(1);
    }

    plattice_enumerate_t(const plattice_info_t &basis,
            const slice_offset_t hint, const int logI)
        : hint(hint)
    {
        inc_a = basis.get_inc_a(logI);
        inc_c = basis.get_inc_c(logI);
        bound0 = basis.get_bound0(logI);
        bound1 = basis.get_bound1(logI);
        x = plattice_x_t(1) << (logI-1);
    }

    plattice_enumerate_t(const plattice_enumerate_t& src)
        : hint(src.hint)
    {
        inc_a = src.inc_a;
        inc_c = src.inc_c;
        bound0 = src.bound0;
        bound1 = src.bound1;
        x = src.x;
    }

    void next() {
      uint32_t i = x & maskI;
      if (i >= bound1)
        x += inc_a;
      if (i < bound0)
        x += inc_c;
    }

    /* Currently merely checks that not both are even */
    bool probably_coprime() const {return (x & even_mask) != 0;}

    void advance_to_next_area(int level);
    plattice_x_t get_x() const {return x;}
    void set_x(plattice_x_t xx) {x = xx;}
    plattice_x_t get_bound1() const {return bound1;}
    plattice_x_t get_inc_c() const {return inc_c;}
    slice_offset_t get_hint() const {return hint;}
};

/* Also enumerates lattice points, but probably_coprime() does a full gcd()
   to ensure that points are really coprime. Very slow. Not used, just there
   for experimenting.*/
class plattice_enumerate_coprime_t : public plattice_enumerate_t {
  unsigned long u, v;
public:
  plattice_enumerate_coprime_t(const plattice_info_t &basis,
          const slice_offset_t hint, const int logI)
    : plattice_enumerate_t(basis, hint, logI), u(0), v(0) {}
  void next() {
    uint32_t i = x & maskI;
    if (i >= bound1) {
      x += inc_a; u++;
    }
    if (i < bound0) {
      x += inc_c;
      v++;
    }
  }
  bool probably_coprime() const {
    return (x & even_mask) != 0 && gcd_ul(u, v) == 1;
  }
};

class plattices_vector_t:
        public std::vector<plattice_enumerate_t>, private NonCopyable {
    slice_index_t index;
public:
    plattices_vector_t(const slice_index_t index) : index(index) {}
    slice_index_t get_index() const {return index;};
};

#if 0
/* MOD2_CLASSES_BS was an attempt, at some point, to support bucket
 * sieving in several passes for congruence classes mod 2. It was never
 * finished, and has never been tested. The last traces of that code may
 * be found in the git history perhaps. To date, the only remaining bit
 * that can still be considered interesting is the comment fragment below
 * in plattice_starting_vector
 */

/* This is for working with congruence classes only */
NOPROFILE_INLINE
plattice_x_t plattice_starting_vector(const plattice_info_t * pli, sieve_info_srcptr si, unsigned int par )
{
    /* With MOD2_CLASSES_BS set up, we have computed by the function
     * above an adapted basis for the band of total width I/2 (thus from
     * -I/4 to I/4). This adapted basis is in the coefficients a0 a1 b0
     *  b1 of the pli data structure.
     *
     * Now as per Proposition 1 of FrKl05 applied to I/2, any vector
     * whose i-coordinates are within ]-I/2,I/2[ (<ugly>We would like a
     * closed interval on the left. Read further on for that case</ugly>)
     * can actually be written as a combination with positive integer
     * coefficients of these basis vectors a and b.
     *
     * We also know that the basis (a,b) has determinant p, thus odd. The
     * congruence class mod 2 that we want to reach is thus accessible.
     * It is even possible to find coefficients (k,l) in {0,1} such that
     * ka+lb is within this congruence class. This means that we're going
     * to consider either a,b,or a+b as a starting vector. The
     * i-coordinates of these, as per the properties of Proposition 1, are
     * within ]-I/2,I/2[. Now all other vectors with i-coordinates in
     * ]-I/2,I/2[ which also belong to the same congruence class, can be
     * written as (2k'+k)a+(2l'+l)b, with k' and l' necessarily
     * nonnegative.
     *
     * The last ingredient is that (2a, 2b) forms an adapted basis for
     * the band of width I with respect to the lattice 2p. It's just an
     * homothety.
     *
     * To find (k,l), we proceed like this. First look at the (a,b)
     * matrix mod 2:
     *                 a0&1    a1&1
     *                 b0&1    b1&1
     * Its determinant is odd, thus the inverse mod 2 is:
     *                 b1&1    a1&1
     *                 b0&1    a0&1
     * Now the congruence class is given by the parity argument. The
     * vector is:
     *                par&1,   par>>1
     * Multiplying this vector by the inverse matrix above, we obtain the
     * coordinates k,l, which are:
     *            k = (b1&par&1)^(b0&(par>>1));
     *            l = (a1&par&1)^(a0&(par>>1));
     * Now our starting vector is ka+lb. Instead of multiplying by k and
     * l with values in {0,1}, we mask with -k and -l, which both are
     * either all zeroes or all ones in binary
     *
     */
    /* Now for the extra nightmare. Above, we do not have the guarantee
     * that a vector whose i-coordinate is precisely -I/2 has positive
     * coefficients in our favorite basis. It's annoying, because it may
     * well be that if such a vector also has positive j-coordinate, then
     * it is in fact the first vector we will meet. An example is given
     * by the following data:
     *
            f:=Polynomial(StringToIntegerSequence("
                -1286837891385482936880099433527136908899552
                55685111236629140623629786639929578
                13214494134209131997428776417
                -319664171270205889372
                -17633182261156
                40500"));

            q:=165017009; rho:=112690811;
            a0:=52326198; b0:=-1; a1:=60364613; b1:=2;
            lI:=13; I:=8192; J:=5088;
            p:=75583; r0:=54375;
            > M;
            [-2241    19]
            [ 1855    18]
            > M[1]-M[2];
            (-4096     1)

    * Clearly, above, for the congruence class (0,1), we must start with
    * this vector, not with the sum.
    */
    int32_t a0 = pli->a0;
    int32_t a1 = pli->a1;
    int32_t b0 = pli->b0;
    int32_t b1 = pli->b1;

    int k = -((b1&par&1)^(b0&(par>>1)));
    int l = -((a1&par&1)^(a0&(par>>1)));
    int32_t v[2]= { (a0&k)+(b0&l), (a1&k)+(b1&l)};

    /* handle exceptional case as described above */
    if (k && l && a0-b0 == -(1 << (si->conf->logI-1)) && a1 > b1) {
        v[0] = a0-b0;
        v[1] = a1-b1;
    }
    return (v[1] << si->conf->logI) | (v[0] + (1 << (si->conf->logI-1)));
}
#endif


#endif
