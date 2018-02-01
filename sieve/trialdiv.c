#define VERBOSE 0

#include "cado.h"
#if VERBOSE
#include <stdio.h>
#endif
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include "ularith.h"
#include "modredc_ul.h"
#include "trialdiv.h"
#include "portability.h"


unsigned long
trialdiv_get_max_p()
{
  /* With if(TRIALDIV_MAXLEN == 1) here, gcc complains about a division of zero
     in ULONG_MAX / (TRIALDIV_MAXLEN - 1), so have to use preprocessor to get
     gcc to shut up */
#if TRIALDIV_MAXLEN == 1
  return ULONG_MAX;
#else
  double s = sqrt(ULONG_MAX / (TRIALDIV_MAXLEN - 1));
  ASSERT(s >= 1.);
  return MIN((unsigned long)s - 1, ULONG_MAX);
#endif
}

static void
trialdiv_init_divisor (trialdiv_divisor_t *d, const unsigned long p)
{
#if TRIALDIV_MAXLEN > 1
  int i;
#endif
  ASSERT (p % 2UL == 1UL);
  /* Test that p < sqrt (word_base / (TRIALDIV_MAXLEN - 1)) */
  ASSERT (p <= trialdiv_get_max_p());
  d->p = p;
#if TRIALDIV_MAXLEN > 1
  if (p == 1UL)
    d->w[0] = 0UL; /* DIV would cause quotient overflow */
  else
    ularith_div_2ul_ul_ul_r (&(d->w[0]), 0UL, 1UL, p);
  /* Warning: the array d->w[] has size TRIALDIV_MAXLEN-1, not
   * TRIALDIV_MAXLEN (see definition of trialdiv_divisor_t) */
  for (i = 1; i < TRIALDIV_MAXLEN-1; i++)
    ularith_div_2ul_ul_ul_r (&(d->w[i]), 0UL, d->w[i - 1], p);
#endif
  d->pinv = ularith_invmod (p);
  d->plim = ULONG_MAX / p;
}

#if TRIALDIV_MAXLEN >= 2
/* Trial division for integers with 2 unsigned long */
static inline int
trialdiv_div2 (const unsigned long *n, const trialdiv_divisor_t *d)
{
  unsigned long r0, r1, x0, x1;
#ifdef __GNUC__
  __asm__ ("# trialdiv_div2");
#endif
  ularith_mul_ul_ul_2ul (&x0, &x1, n[1], d->w[0]); /* n_1 * (w^1 % p) */
  r0 = n[0];
  r1 = x1;
  ularith_add_ul_2ul (&r0, &r1, x0);
  x0 = r1 * d->w[0]; /* TODO: optimal? */
  x0 += r0;
  if (x0 < r0)
    x0 += d->w[0];
  x0 *= d->pinv;
  return x0 <= d->plim;
}
#endif

#if TRIALDIV_MAXLEN >= 3
/* Trial division for integers with 3 unsigned long */
static inline int
trialdiv_div3 (const unsigned long *n, const trialdiv_divisor_t *d)
{
  unsigned long r0, r1, x0, x1;
#ifdef __GNUC__
  __asm__ ("# trialdiv_div3");
#endif
#if VERBOSE
    printf ("p = %lu, n2:n1:n0 = %lu * w^2 + %lu * w + %lu", 
            d->p, n[2], n[1], n[0]);
#endif
  ularith_mul_ul_ul_2ul (&x0, &x1, n[1], d->w[0]); /* n_1 * (w^1 % p) */
  r0 = n[0];
  r1 = x1;
  ularith_add_ul_2ul (&r0, &r1, x0);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[2], d->w[1]); /* n_2 * (w^2 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
#if VERBOSE
    printf ("  r1:r0 = %lu * w + %lu", r1, r0);
#endif
  x0 = r1 * d->w[0];
  x0 += r0;
  if (x0 < r0)
    x0 += d->w[0];
#if VERBOSE
    printf ("  x0 = %lu", x0);
#endif
  x0 *= d->pinv;
  return x0 <= d->plim;
}
#endif

#if TRIALDIV_MAXLEN >= 4
/* Trial division for integers with 4 unsigned long */
static inline int
trialdiv_div4 (const unsigned long *n, const trialdiv_divisor_t *d)
{
  unsigned long r0, r1, x0, x1;
#ifdef __GNUC__
  __asm__ ("# trialdiv_div4");
#endif
  ularith_mul_ul_ul_2ul (&x0, &x1, n[1], d->w[0]); /* n_1 * (w^1 % p) */
  r0 = n[0];
  r1 = x1;
  ularith_add_ul_2ul (&r0, &r1, x0);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[2], d->w[1]); /* n_2 * (w^2 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[3], d->w[2]); /* n_3 * (w^3 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  x0 = r1 * d->w[0];
  x0 += r0;
  if (x0 < r0)
    x0 += d->w[0];
  x0 *= d->pinv;
  return x0 <= d->plim;
}
#endif

#if TRIALDIV_MAXLEN >= 5
/* Trial division for integers with 5 unsigned long */
static inline int
trialdiv_div5 (const unsigned long *n, const trialdiv_divisor_t *d)
{
  unsigned long r0, r1, x0, x1;
#ifdef __GNUC__
  __asm__ ("# trialdiv_div5");
#endif
  ularith_mul_ul_ul_2ul (&x0, &x1, n[1], d->w[0]); /* n_1 * (w^1 % p) */
  r0 = n[0];
  r1 = x1;
  ularith_add_ul_2ul (&r0, &r1, x0);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[2], d->w[1]); /* n_2 * (w^2 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[3], d->w[2]); /* n_3 * (w^3 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[4], d->w[3]); /* n_4 * (w^4 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  x0 = r1 * d->w[0];
  x0 += r0;
  if (x0 < r0)
    x0 += d->w[0];
  x0 *= d->pinv;
  return x0 <= d->plim;
}
#endif

#if TRIALDIV_MAXLEN >= 6
/* Trial division for integers with 6 unsigned long */
static inline int
trialdiv_div6 (const unsigned long *n, const trialdiv_divisor_t *d)
{
  unsigned long r0, r1, x0, x1;
#ifdef __GNUC__
  __asm__ ("# trialdiv_div6");
#endif
  ularith_mul_ul_ul_2ul (&x0, &x1, n[1], d->w[0]); /* n_1 * (w^1 % p) */
  r0 = n[0];
  r1 = x1;
  ularith_add_ul_2ul (&r0, &r1, x0);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[2], d->w[1]); /* n_2 * (w^2 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[3], d->w[2]); /* n_3 * (w^3 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[4], d->w[3]); /* n_4 * (w^4 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[5], d->w[4]); /* n_5 * (w^5 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  x0 = r1 * d->w[0];
  x0 += r0;
  if (x0 < r0)
    x0 += d->w[0];
  x0 *= d->pinv;
  return x0 <= d->plim;
}
#endif

#if TRIALDIV_MAXLEN >= 7
/* Trial division for integers with 7 unsigned long */
static inline int
trialdiv_div7 (const unsigned long *n, const trialdiv_divisor_t *d)
{
  unsigned long r0, r1, x0, x1;
#ifdef __GNUC__
  __asm__ ("# trialdiv_div7");
#endif
  ularith_mul_ul_ul_2ul (&x0, &x1, n[1], d->w[0]); /* n_1 * (w^1 % p) */
  r0 = n[0];
  r1 = x1;
  ularith_add_ul_2ul (&r0, &r1, x0);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[2], d->w[1]); /* n_2 * (w^2 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[3], d->w[2]); /* n_3 * (w^3 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[4], d->w[3]); /* n_4 * (w^4 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[5], d->w[4]); /* n_5 * (w^5 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[6], d->w[5]); /* n_6 * (w^6 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  x0 = r1 * d->w[0];
  x0 += r0;
  if (x0 < r0)
    x0 += d->w[0];
  x0 *= d->pinv;
  return x0 <= d->plim;
}
#endif

#if TRIALDIV_MAXLEN >= 8
/* Trial division for integers with 8 unsigned long */
static inline int
trialdiv_div8 (const unsigned long *n, const trialdiv_divisor_t *d)
{
  unsigned long r0, r1, x0, x1;
#ifdef __GNUC__
  __asm__ ("# trialdiv_div8");
#endif
  ularith_mul_ul_ul_2ul (&x0, &x1, n[1], d->w[0]); /* n_1 * (w^1 % p) */
  r0 = n[0];
  r1 = x1;
  ularith_add_ul_2ul (&r0, &r1, x0);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[2], d->w[1]); /* n_2 * (w^2 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[3], d->w[2]); /* n_3 * (w^3 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[4], d->w[3]); /* n_4 * (w^4 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[5], d->w[4]); /* n_5 * (w^5 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[6], d->w[5]); /* n_6 * (w^6 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  ularith_mul_ul_ul_2ul (&x0, &x1, n[7], d->w[6]); /* n_7 * (w^7 % p) */
  ularith_add_2ul_2ul (&r0, &r1, x0, x1);
  x0 = r1 * d->w[0];
  x0 += r0;
  if (x0 < r0)
    x0 += d->w[0];
  x0 *= d->pinv;
  return x0 <= d->plim;
}
#endif

/* Divide exactly N = {n, 2} in place by p, where pinv = 1/p mod W,
   using Hensel division. The algorithm is the following for N of k limbs:

   r = 0 # borrow
   for (i = 0; i < k; i++)
      t = n[i] - r           # computed mod W
      q[i] = n[i] * pinv     # i-th digit of Hensel's quotient (computed mod W)
      y = q[i] * p + r - n[i]
      r = y / W              # exact division

   If at the end, r is zero, then the division is exact.

   The values r and y satisfy 0 <= r < W and 0 <= y < W^2 by induction:
   (i) q[i] <= W-1 thus q[i]*p + r <= (W-1)*p+W-1 = (W-1)*(p+1) < W since p < W
   (ii) we have q[i]*p = n[i] mod W, thus q[i]*p = n[i] + k*W with k >= 0,
        this ensures that q[i]*p - n[i] >= 0.
*/
static inline void
trialdiv2_divexact (mpz_t N, mp_limb_t p, mp_limb_t pinv)
{
  mp_limb_t x0, r0, r1;
  mp_ptr n = N->_mp_d;

  x0 = n[0] * pinv; /* N/p mod W = x0 */
  ularith_mul_ul_ul_2ul (&r0, &r1, x0, p); /* x0 * p = r1*W+r0 */
  ASSERT(r0 == n[0]);
  r1 = n[1] - r1;
  n[0] = x0;
  n[1] = r1 * pinv;
  N->_mp_size -= (r1 == 0);
}

/* Divides primes in d out of N and stores them (with multiplicity) in f.
   Never stores more than max_div primes in f. Returns the number of
   primes found, or max_div+1 if the number of primes exceeds max_div. 
   Primes not stored in f (because max_div was exceeded) are not divided out.
   This way, trial division of highly composite values can be processed
   by repeated calls of trialdiv(). */

size_t 
trialdiv (unsigned long *f, mpz_t N, const trialdiv_divisor_t *d,
	  const size_t max_div)
{
  size_t n = 0;

#if TRIALDIV_MAXLEN > 8
#error trialdiv not implemented for input sizes of more than 8 words
#endif

  while (mpz_cmp_ui (N, 1UL) > 0)
    {
      size_t s = mpz_size(N);
      mp_limb_t u = 0;
#if VERBOSE
      gmp_printf ("s = %d, N = %Zd, ", s, N);
#endif
      ASSERT_ALWAYS (s <= TRIALDIV_MAXLEN);
      if (s == 1)
        {
	  mp_limb_t t = mpz_getlimbn (N, 0);
	  while ((u = t * d->pinv) > d->plim)
	    d++;
        }
#if TRIALDIV_MAXLEN >= 2
      else if (s == 2)
        {
          while (!trialdiv_div2 (N[0]._mp_d, d))
            d++;
        }
#endif
#if TRIALDIV_MAXLEN >= 3
      else if (s == 3)
        {
          while (!trialdiv_div3 (N[0]._mp_d, d))
            d++;
        }
#endif
#if TRIALDIV_MAXLEN >= 4
      else if (s == 4)
        {
          while (!trialdiv_div4 (N[0]._mp_d, d))
            d++;
        }
#endif
#if TRIALDIV_MAXLEN >= 5
      else if (s == 5)
        {
          while (!trialdiv_div5 (N[0]._mp_d, d))
            d++;
        }
#endif
#if TRIALDIV_MAXLEN >= 6
      else if (s == 6)
        {
          while (!trialdiv_div6 (N[0]._mp_d, d))
            d++;
        }
#endif
#if TRIALDIV_MAXLEN >= 7
      else if (s == 7)
        {
          while (!trialdiv_div7 (N[0]._mp_d, d))
            d++;
        }
#endif

#if TRIALDIV_MAXLEN >= 8
      else if (s == 8)
        {
          while (!trialdiv_div8 (N[0]._mp_d, d))
            d++;
        }
#endif
#if VERBOSE 
      printf ("\n");
#endif

      if (d->p == 1UL)
        break;

      ASSERT (mpz_divisible_ui_p (N, d->p));

      if (n == max_div)
	return max_div + 1;

      f[n++] = d->p;
      if (s == 1)
        N->_mp_d[0] = u;
      else if (s == 2)
        trialdiv2_divexact (N, d->p, d->pinv);
      else
        mpz_divexact_ui (N, N, d->p);
    }
  
  return n;
}


/* Initialise a trialdiv_divisor_t array with the nr primes stored in *f.
   This function allocates memory for the array, inits each entry, and puts
   a sentinel at the end. */
trialdiv_divisor_t *
trialdiv_init (const unsigned long *f, const unsigned int nr)
{
  trialdiv_divisor_t *d;
  unsigned int i;
  
  d = (trialdiv_divisor_t *) malloc ((nr + 1) * sizeof (trialdiv_divisor_t));
  ASSERT (d != NULL);
  for (i = 0; i < nr; i++)
    trialdiv_init_divisor (&(d[i]), f[i]);
  trialdiv_init_divisor (&(d[nr]), 1UL);

  return d;
}

void
trialdiv_clear (trialdiv_divisor_t *d)
{
  free (d);
}
