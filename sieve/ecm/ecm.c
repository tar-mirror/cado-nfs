#include "cado.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "portability.h"
#include "facul_ecm.h"
#include "ularith.h"
#include "getprime.h"
#include "addchain_bc.h"

/* Do we want backtracking when processing factors of 2 in E? */
#ifndef ECM_BACKTRACKING
/* Default is "yes." Set to 0 for "no." */
#define ECM_BACKTRACKING 1
#endif

/* Define to 1 to make ellM_add() test if the two points are identical,
   and call ellM_double() if they are */
#ifndef ELLM_SAFE_ADD
#define ELLM_SAFE_ADD 0
#endif

#ifndef MAYBE_UNUSED
#if defined(__GNUC__)
#define MAYBE_UNUSED __attribute__ ((unused))
#else
#define MAYBE_UNUSED
#endif
#endif

#define COUNT_ELLM_OPS 0
#if COUNT_ELLM_OPS
static unsigned long ellM_add_count, ellM_double_count;
#endif

#define COUNT_ELLE_OPS 0
#if COUNT_ELLE_OPS
static unsigned long ellE_add_count, ellE_double_count;
#endif


/* Projective point on Montgomery curve (x : z) onlyx */
typedef struct {residue_t x, z;} __ellM_point_t;
typedef __ellM_point_t ellM_point_t[1];

/* Homogeneous projective Edwards coordinates 
   (x : y : z) ---> (x/z, y/z)  z != 0 */
typedef struct {residue_t x, y, z;} __ellE_point_t;
typedef __ellE_point_t ellE_point_t[1];

/* Extended projective Edwards coordinates 
   [Hisil et al. 2008] */
typedef struct {residue_t x, y, t, z;} __ellEe_point_t;
typedef __ellEe_point_t ellEe_point_t[1];

/* Affine point on Weierstrass curve */
typedef struct {residue_t x, y;} __ellW_point_t;
typedef __ellW_point_t ellW_point_t[1];


/* -------------------------------------------------------------------------- */
/* Functions for curves in Montgomery form */
/* -------------------------------------------------------------------------- */
static inline void
ellM_init (ellM_point_t P, const modulus_t m)
{
  mod_init (P->x, m);
  mod_init (P->z, m);
}

static inline void
ellM_clear (ellM_point_t P, const modulus_t m)
{
  mod_clear (P->x, m);
  mod_clear (P->z, m);
}

static inline void
ellM_set (ellM_point_t Q, const ellM_point_t P, const modulus_t m)
{
  mod_set (Q->x, P->x, m);
  mod_set (Q->z, P->z, m);
}

static inline void
ellM_swap (ellM_point_t Q, ellM_point_t P, const modulus_t m)
{
  mod_swap (Q->x, P->x, m);
  mod_swap (Q->z, P->z, m);
}

/* computes Q=2P, with 5 muls (3 muls and 2 squares) and 4 add/sub.
     - m : number to factor
     - b : (a+2)/4 mod n
  It is permissible to let P and Q use the same memory. */
static void
ellM_double (ellM_point_t Q, const ellM_point_t P, const modulus_t m, 
             const residue_t b)
{
  residue_t u, v, w;

#if ELLM_SAFE_ADD
  if (mod_is0 (P->z, m))
    {
      ASSERT (mod_is0 (P->x, m));
    }
#endif
  
#if COUNT_ELLM_OPS
  ellM_double_count++;
#endif

  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);
  mod_init_noset0 (w, m);

  mod_add (u, P->x, P->z, m);
  mod_sqr (u, u, m);          /* u = (x + z)^2 */
  mod_sub (v, P->x, P->z, m);
  mod_sqr (v, v, m);          /* v = (x - z)^2 */
  mod_mul (Q->x, u, v, m);    /* x2 = (x^2 - z^2)^2 */
  mod_sub (w, u, v, m);       /* w = 4 * x * z */
  mod_mul (u, w, b, m);       /* u = x * z * (A + 2) */
  mod_add (u, u, v, m);       /* u = x^2 + x * z * A + z^2 */
  mod_mul (Q->z, w, u, m);    /* Q_z = (4xz) * (x^2 + xzA + z^2) */

#if ELLM_SAFE_ADD
  if (mod_is0 (Q->z, m))
    mod_set0 (Q->x, m);
#endif

  mod_clear (w, m);
  mod_clear (v, m);
  mod_clear (u, m);
}


/* adds P and Q and puts the result in R,
     using 6 muls (4 muls and 2 squares), and 6 add/sub.
   One assumes that Q-R=D or R-Q=D.
   This function assumes that P !~= Q, i.e. that there is 
   no t!=0 so that P->x = t*Q->x and P->z = t*Q->z, for otherwise the result 
   is (0:0) although it shouldn't be (which actually is good for factoring!).

   R may be identical to P, Q and/or D. */
static void
ellM_add (ellM_point_t R, const ellM_point_t P, const ellM_point_t Q, 
          const ellM_point_t D, MAYBE_UNUSED const residue_t b, 
          const modulus_t m)
{
  residue_t u, v, w;

#if ELLM_SAFE_ADD
  /* Handle case where at least one input point is point at infinity */
  if (mod_is0 (P->z, m))
    {
      ASSERT (mod_is0 (P->x, m));
      ellM_set (R, Q, m);
      return;
    }
  if (mod_is0 (Q->z, m))
    {
      ASSERT (mod_is0 (Q->x, m));
      ellM_set (R, P, m);
      return;
    }
#endif

#if COUNT_ELLM_OPS
  ellM_add_count++;
#endif

  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);
  mod_init_noset0 (w, m);

  mod_sub (u, P->x, P->z, m);
  mod_add (v, Q->x, Q->z, m);
  mod_mul (u, u, v, m);      /* u = (Px-Pz)*(Qx+Qz) */
  mod_add (w, P->x, P->z, m);
  mod_sub (v, Q->x, Q->z, m);
  mod_mul (v, w, v, m);      /* v = (Px+Pz)*(Qx-Qz) */
  mod_add (w, u, v, m);      /* w = 2*(Qx*Px - Qz*Pz)*/
  mod_sub (v, u, v, m);      /* v = 2*(Qz*Px - Qx*Pz) */
#if ELLM_SAFE_ADD
  /* Check if v == 0, which happens if P=Q or P=-Q. 
     If P=-Q, set result to point at infinity.
     If P=Q, use ellM_double() instead.
     This test only works if P=Q on the pseudo-curve modulo N, i.e.,
     if N has several prime factors p, q, ... and P=Q or P=-Q on E_p but 
     not on E_q, this test won't notice it. */
  if (mod_is0 (v, m))
    {
      mod_clear (w, m);
      mod_clear (v, m);
      mod_clear (u, m);
      /* Test if difference is point at infinity */
      if (mod_is0 (D->z, m))
        {
          ASSERT (mod_is0 (D->x, m));
          ellM_double (R, P, m, b); /* Yes, points are identical, use doubling */
        }
      else
        { 
          mod_set0 (R->x, m); /* No, are each other's negatives. */
          mod_set0 (R->z, m); /* Set result to point at infinity */
        }
      return;
    }
#endif
  mod_sqr (w, w, m);          /* w = 4*(Qx*Px - Qz*Pz)^2 */
  mod_sqr (v, v, m);          /* v = 4*(Qz*Px - Qx*Pz)^2 */
  mod_set (u, D->x, m);       /* save D->x */
  mod_mul (R->x, w, D->z, m); /* may overwrite D->x */
  mod_mul (R->z, u, v, m);

  mod_clear (w, m);
  mod_clear (v, m);
  mod_clear (u, m);
}


/* (x:z) <- e*(x:z) (mod p) */
static void
ellM_mul_ul (ellM_point_t R, const ellM_point_t P, unsigned long e, 
             const modulus_t m, const residue_t b)
{
  unsigned long l, n;
  ellM_point_t t1, t2;

  if (e == 0UL)
    {
      mod_set0 (R[0].x, m);
      mod_set0 (R[0].z, m);
      return;
    }

  if (e == 1UL)
    {
      ellM_set (R, P, m);
      return;
    }
  
  if (e == 2UL)
    {
      ellM_double (R, P, m, b);
      return;
    }

  if (e == 4UL)
    {
      ellM_double (R, P, m, b);
      ellM_double (R, R, m, b);
      return;
    }

  ellM_init (t1, m);

  if (e == 3UL)
    {
      ellM_double (t1, P, m, b);
      ellM_add (R, t1, P, P, b, m);
      ellM_clear (t1, m);
      return;
    }

  ellM_init (t2, m);
  e --;

  /* compute number of steps needed: we start from (1,2) and go from
     (i,i+1) to (2i,2i+1) or (2i+1,2i+2) */
  for (l = e, n = 0; l > 1; n ++, l /= 2) ;

  /* start from P1=P, P2=2P */
  ellM_set (t1, P, m);
  ellM_double (t2, t1, m, b);

  while (n--)
    {
      if ((e >> n) & 1) /* (i,i+1) -> (2i+1,2i+2) */
        {
          /* printf ("(i,i+1) -> (2i+1,2i+2)\n"); */
          ellM_add (t1, t1, t2, P, b, m);
          ellM_double (t2, t2, m, b);
        }
      else /* (i,i+1) -> (2i,2i+1) */
        {
          /* printf ("(i,i+1) -> (2i,2i+1)\n"); */
          ellM_add (t2, t1, t2, P, b, m);
          ellM_double (t1, t1, m, b);
        }
    }
  
  ellM_set (R, t2, m);

  ellM_clear (t1, m);
  ellM_clear (t2, m);
}

/* -------------------------------------------------------------------------- */
/* Functions for curves in twisted Edwards form */
/* -------------------------------------------------------------------------- */

static inline void
ellE_init (ellE_point_t P, const modulus_t m)
{
  mod_init (P->x, m);
  mod_init (P->y, m);
  mod_init (P->z, m);
}

static inline void
ellEe_init (ellEe_point_t P, const modulus_t m)
{
  mod_init (P->x, m);
  mod_init (P->y, m);
  mod_init (P->t, m);
  mod_init (P->z, m);
}

static inline void
ellE_clear (ellE_point_t P, const modulus_t m)
{
  mod_clear (P->x, m);
  mod_clear (P->y, m);
  mod_clear (P->z, m);
}

static inline void
ellEe_clear (ellEe_point_t P, const modulus_t m)
{
  mod_clear (P->x, m);
  mod_clear (P->y, m);
  mod_clear (P->t, m);
  mod_clear (P->z, m);
}

static inline void
ellE_set (ellE_point_t Q, const ellE_point_t P, const modulus_t m)
{
  mod_set (Q->x, P->x, m);
  mod_set (Q->y, P->y, m);
  mod_set (Q->z, P->z, m);
}

static inline void
ellEe_set (ellEe_point_t Q, const ellEe_point_t P, const modulus_t m)
{
  mod_set (Q->x, P->x, m);
  mod_set (Q->y, P->y, m);
  mod_set (Q->t, P->t, m);
  mod_set (Q->z, P->z, m);
}

/* (x : y : t : z) to (x : y : z) 
   free: simply ignore t coordinate */
static inline void
ellE_set_from_Ee (ellE_point_t Q, const ellEe_point_t P, const modulus_t m)
{
  mod_set (Q->x, P->x, m);
  mod_set (Q->y, P->y, m);
  mod_set (Q->z, P->z, m);
}

/* (x : y : z) to (x : y : t : z)
   cost: 3m+1s by computing (xz, yz, xy, z^2) */
static inline void
ellEe_set_from_E (ellEe_point_t Q, const ellE_point_t P, const modulus_t m)
{
  mod_mul (Q->x, P->x, P->z, m);
  mod_mul (Q->y, P->y, P->z, m);
  mod_mul (Q->t, P->x, P->y, m);
  mod_sqr (Q->z, P->z, m);
}

static inline void
ellE_swap (ellE_point_t Q, ellE_point_t P, const modulus_t m)
{
  mod_swap (Q->x, P->x, m);
  mod_swap (Q->y, P->y, m);
  mod_swap (Q->z, P->z, m);
}

static inline void
ellEe_swap (ellEe_point_t Q, ellEe_point_t P, const modulus_t m)
{
  mod_swap (Q->x, P->x, m);
  mod_swap (Q->y, P->y, m);
  mod_swap (Q->t, P->t, m);
  mod_swap (Q->z, P->z, m);
}

static inline void
ellEe_neg (ellEe_point_t P, const modulus_t m)
{
  mod_neg (P->x, P->x, m);
  mod_neg (P->t, P->t, m);
}

/* 
   Computes Q=2P in E using dedicated doubling in Projective twisted Edwards
   coordinates from [Bernstein et al. 2008] 

   Formulae only depends on the curve constant a (curve constant d is not
   necessary).  

   Cost: 3m + 4s + 1d (where d stands for a multiplication by a constant)

   @InProceedings{BerBirJoyLanPet08,
   author = 	 {D. J. Bernstein and P. Birkner M. Joye and T. Lange and C. Peters},
   title = 	 {Twisted {E}dwards Curves },
   booktitle = {Progress in Cryptology -- {AFRICACRYPT 2008}},
   pages = 	 {389--405},
   year = 	 2008,
   volume = 	 5023,
   series = 	 LNCS,
   publisher = {Springer}
   }
   
*/
static void
ellE_double (ellE_point_t Q, const ellE_point_t P, const modulus_t m, const residue_t a)
{
  /* FIXME: optimize registers */
  residue_t B, C, D, E, F, H, J;

#if COUNT_ELLE_OPS
  ellE_double_count++;
#endif

  mod_init_noset0 (B, m);
  mod_init_noset0 (C, m);
  mod_init_noset0 (D, m);
  mod_init_noset0 (E, m);
  mod_init_noset0 (F, m);
  mod_init_noset0 (H, m);
  mod_init_noset0 (J, m);

  mod_add (B, P->x, P->y, m);
  mod_sqr (B, B, m);                  /* B := (X1 + Y1)^2 */
  mod_sqr (C, P->x, m);               /* C := X1^2 */
  mod_sqr (D, P->y, m);               /* D := Y1^2 */
  mod_mul (E, C, a, m);               /* E := aC */
  mod_add (F, E, D, m);               /* F := E + D */
  mod_sqr (H, P->z, m);               /* H := Z1^2 */
  mod_add (H, H, H, m);               /* H := 2H (Any better way to multiply by 2?) */
  mod_sub (J, F, H, m);               /* J := F - 2H */
  mod_sub (H, B, C, m);
  mod_sub (H, H, D, m);
  mod_mul (Q->x, H, J, m);            /* X3 := (B - C - D) * J */
  mod_sub (H, E, D, m);
  mod_mul (Q->y, F, H, m);            /* Y3 := F * (E - D) */
  mod_mul (Q->z, F, J, m);            /* Z3 := F * J */

  mod_clear (B, m);
  mod_clear (C, m);
  mod_clear (D, m);
  mod_clear (E, m);
  mod_clear (F, m);
  mod_clear (H, m);
  mod_clear (J, m);  
}

/* 
   Computes Q=2P in Ee using dedicated doubling in extended twisted Edwards
   coordinates from [Hisil et al. 2008] 

   Formulae only depends on the curve constant a (curve constant d is not
   necessary).  

   Cost: 4m + 4s + 1d (where d stands for a multiplication by a constant)
  
   @InProceedings{HisWonCarDaw08:twed_revisited,
   author = 	 {H. Hisil and Wong, K. K.-H. and G. Carter and E. Dawson},
   title = 	 {Twisted {Edwards} Curves Revisited},
   booktitle = 	 {Advances in Cryptology, {ASIACRYPT 2008}},
   pages = 	 {326--343},
   year = 	 2008,
   volume = 	 5350,
   series = 	 LNCS,
   publisher =    {Springer}
   }
*/
static void
ellEe_double (ellEe_point_t Q, const ellEe_point_t P, const modulus_t m, 
             const residue_t a)
{
  /* FIXME: optimize registers */
  residue_t A, B, C, D, E, F, G, H;

#if COUNT_ELLE_OPS
  ellE_double_count++;
#endif

  mod_init_noset0 (A, m);
  mod_init_noset0 (B, m);
  mod_init_noset0 (C, m);
  mod_init_noset0 (D, m);
  mod_init_noset0 (E, m);
  mod_init_noset0 (F, m);
  mod_init_noset0 (G, m);
  mod_init_noset0 (H, m);

  mod_sqr(A, P->x, m);            /* A = X1^2 */
  mod_sqr(B, P->y, m);            /* B = Y1^2 */
  mod_sqr(C, P->z, m);
  mod_add(C, C, C, m);            /* C = 2 * Z1^2 */
  mod_mul(D, A, a, m);            /* D = a * A */
  mod_add(E, P->x, P->y, m);
  mod_sqr(E, E, m);
  mod_sub(E, E, A, m);
  mod_sub(E, E, B, m);            /* E = (X1+Y1)^2 - A - B */
  mod_add(G, D, B, m);            /* G = D + B */
  mod_sub(F, G, C, m);            /* F = G - C */
  mod_sub(H, D, B, m);            /* H = D - B */
  mod_mul(Q->x, E, F, m);         /* X3 = E * F */
  mod_mul(Q->y, G, H, m);         /* Y3 = G * H */
  mod_mul(Q->t, E, H, m);         /* T3 = E * H */
  mod_mul(Q->z, F, G, m);         /* Z3 = F * G */
 
  mod_clear (A, m);
  mod_clear (B, m);
  mod_clear (C, m);
  mod_clear (D, m);
  mod_clear (E, m);
  mod_clear (F, m);
  mod_clear (G, m);
  mod_clear (H, m);
}


/* 
   Adds P and Q and puts the result in R, using the dedicated addition formula
   in extended twisted Edwards coordinates from [Hisil et al. 2008]

   Formulae only depends on the curve constant a (curve constant d is not necessary).

   Cost: 9m + 1d    (where d stands for a multiplication by a constant)
   
   [Hisil et al. 2008] H. Hisil, K. K.-H. Wong, G. Carter and E. Dawson. Twisted
   Edwards Curves Revisited. ASIACRYPT 2008, vol 5350 of LNCS, pp. 326--343,
   Springer.
*/
static void
ellEe_add (ellEe_point_t R, const ellEe_point_t P, const ellEe_point_t Q,
	   const modulus_t m, const residue_t a)
{
  /* FIXME: optimize registers */
  residue_t A, B, C, D, E, F, G, H;

  /* FIXME: check if necessary in twisted Edwards form */
/* #if ELLM_SAFE_ADD */
/*   /\* Handle case where at least one input point is point at infinity *\/ */
/*   if (mod_is0 (P->z, m)) */
/*     { */
/*       ASSERT (mod_is0 (P->x, m)); */
/*       ellM_set (R, Q, m); */
/*       return; */
/*     } */
/*   if (mod_is0 (Q->z, m)) */
/*     { */
/*       ASSERT (mod_is0 (Q->x, m)); */
/*       ellM_set (R, P, m); */
/*       return; */
/*     } */
/* #endif */

#if COUNT_ELLE_OPS
  ellE_add_count++;
#endif

  mod_init_noset0 (A, m);
  mod_init_noset0 (B, m);
  mod_init_noset0 (C, m);
  mod_init_noset0 (D, m);
  mod_init_noset0 (E, m);
  mod_init_noset0 (F, m);
  mod_init_noset0 (G, m);
  mod_init_noset0 (H, m);

  mod_mul(A, P->x, Q->x, m);          /* A = X1 * X2 */
  mod_mul(B, P->y, Q->y, m);          /* B = Y1 * Y2 */
  mod_mul(C, P->z, Q->t, m);          /* C = Z1 * T2 */
  mod_mul(D, P->t, Q->z, m);          /* D = T1 * Z2 */
  mod_add(E, D, C, m);                /* E = D + C */
  mod_sub(H, D, C, m);                /* H = D - C */
  
  mod_sub(C, P->x, P->y, m);          /* C = (X1 - Y1) */
  mod_add(D, Q->x, Q->y, m);          /* D = (X2 + Y2) */
  mod_mul(F, C, D, m);
  mod_add(F, F, B, m);
  mod_sub(F, F, A, m);                /* F = (X1 - Y1) * (X2 + Y2) + B - A */
  
  mod_mul(G, A, a, m);
  mod_add(G, G, B, m);                /* G = B + a * A */
  
  mod_mul(R->x, E, F, m);             /* X3 = E * F */
  mod_mul(R->y, G, H, m);             /* Y3 = G * H */
  mod_mul(R->z, F, G, m);             /* Z3 = F * G */
  mod_mul(R->t, E, H, m);             /* T3 = E * H */
  

/* #if ELLM_SAFE_ADD */
/*   /\* Check if v == 0, which happens if P=Q or P=-Q.  */
/*      If P=-Q, set result to point at infinity. */
/*      If P=Q, use ellM_double() instead. */
/*      This test only works if P=Q on the pseudo-curve modulo N, i.e., */
/*      if N has several prime factors p, q, ... and P=Q or P=-Q on E_p but  */
/*      not on E_q, this test won't notice it. *\/ */
/*   if (mod_is0 (v, m)) */
/*     { */
/*       mod_clear (w, m); */
/*       mod_clear (v, m); */
/*       mod_clear (u, m); */
/*       /\* Test if difference is point at infinity *\/ */
/*       if (mod_is0 (D->z, m)) */
/*         { */
/*           ASSERT (mod_is0 (D->x, m)); */
/*           ellM_double (R, P, m, b); /\* Yes, points are identical, use doubling *\/ */
/*         } */
/*       else */
/*         {  */
/*           mod_set0 (R->x, m); /\* No, are each other's negatives. *\/ */
/*           mod_set0 (R->z, m); /\* Set result to point at infinity *\/ */
/*         } */
/*       return; */
/*     } */
/* #endif */

  mod_clear (A, m);
  mod_clear (B, m);
  mod_clear (C, m);
  mod_clear (D, m);
  mod_clear (E, m);
  mod_clear (F, m);
  mod_clear (G, m);
  mod_clear (H, m);
}

/* Computes Q = 2P + R in projective coordinates,   */
/* where P is given in projective coord. and R in extended coord. */
MAYBE_UNUSED
static void
ellE_double_add (ellE_point_t Q, const ellE_point_t P, const ellEe_point_t R, const modulus_t m, const residue_t a)
{
  ellEe_point_t Qe;
  ellEe_init (Qe, m);
  /* TODO: optimize cost */
  ellE_double (Q, P, m, a);
  ellEe_set_from_E (Qe, Q, m);
  ellEe_add (Qe, Qe, R, m, a);
  ellE_set_from_Ee (Q, Qe, m);

  ellEe_clear (Qe, m);
}


/* Computes R = [e]P (mod m) in homogeneous Edwards coordinates

   Twisted Edwards curve (a,d):
   ax^2 + y^2 = 1 + d * x^2 * y^2, with a * d * (a-d) != 0

   TODO: add conversions between E and Ee coordinates for consecutive doublings
   Only useful for precomputed addition chains
 */
MAYBE_UNUSED 
static void
ellE_mul_ul (ellE_point_t R, const ellE_point_t P, const unsigned long e, 
             const modulus_t m, const residue_t a)
{
  unsigned long j;
  long k;
  ellEe_point_t T, Pe;
  
  if (e == 0UL)
    {
      mod_set0 (R->x, m);
      mod_set1 (R->y, m);
      mod_set1 (R->z, m);
      return;
    }
  
  if (e == 1UL)
    {
      ellE_set (R, P, m);
      return;
    }
  
  if (e == 2UL)
    {
      ellE_double (R, P, m, a);
      return;
    }

  if (e == 4UL)
    {
      ellE_double (R, P, m, a);
      ellE_double (R, R, m, a);
      return;
    }

  ellEe_init (T, m);
  ellEe_init (Pe, m);
  ellEe_set_from_E (Pe, P, m);
  
  /* basic double-and-add */
  
  /* fprintf (stdout, "Running ellE_mul_ul for e = %lu\n", e); */
  
  mod_set0 (T->x, m);
  mod_set0 (T->t, m);
  mod_set1 (T->y, m);
  mod_set1 (T->z, m);
  
  k = CHAR_BIT * sizeof(e) - 1;
  j = (1UL << k);
  
  while(k-- >= 0)
    {
      ellEe_double (T, T, m, a);
      if (j & e)
	ellEe_add (T, T, Pe, m, a);
      j >>= 1;
    }

  ellE_set_from_Ee (R, T, m);
   
  ellEe_clear (T, m);
  ellEe_clear (Pe, m);
}



/* -------------------------------------------------------------------------- */
/* Functions for curves in Weierstrass form */
/* -------------------------------------------------------------------------- */
static inline void
ellW_init (ellW_point_t P, const modulus_t m)
{
  mod_init (P->x, m);
  mod_init (P->y, m);
}

static inline void
ellW_clear (ellW_point_t P, const modulus_t m)
{
  mod_clear (P->x, m);
  mod_clear (P->y, m);
}

static inline void
ellW_set (ellW_point_t Q, const ellW_point_t P, const modulus_t m)
{
  mod_set (Q->x, P->x, m);
  mod_set (Q->y, P->y, m);
}

static inline void
ellW_swap (ellW_point_t Q, ellW_point_t P, const modulus_t m)
{
  mod_swap (Q->x, P->x, m);
  mod_swap (Q->y, P->y, m);
}

/* R <- 2 * P for the curve y^2 = x^3 + a*x + b.

   For Weierstrass coordinates. Returns 1 if doubling worked normally, 
   0 if the result is point at infinity.
*/

static int
ellW_double (ellW_point_t R, const ellW_point_t P, const residue_t a, 
	     const modulus_t m)
{
  residue_t lambda, u, v;

  mod_init_noset0 (lambda, m);
  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);

  mod_sqr (u, P->x, m);
  mod_add (v, u, u, m);
  mod_add (v, v, u, m);
  mod_add (v, v, a, m); /* 3x^2 + a */
  mod_add (u, P->y, P->y, m);
  if (mod_inv (u, u, m) == 0)    /* 1/(2*y) */
  {
      mod_clear (v, m);
      mod_clear (u, m);
      mod_clear (lambda, m);
      return 0; /* y was 0  =>  result is point at infinity */
  }
  mod_mul (lambda, u, v, m);
  mod_sqr (u, lambda, m);
  mod_sub (u, u, P->x, m);
  mod_sub (u, u, P->x, m);    /* x3 = u = lambda^2 - 2*x */
  mod_sub (v, P->x, u, m);
  mod_mul (v, v, lambda, m);
  mod_sub (R->y, v, P->y, m);
  mod_set (R->x, u, m);
  
  mod_clear (v, m);
  mod_clear (u, m);
  mod_clear (lambda, m);
  return 1;
}


/* Adds two points P and Q on the curve y^2 = x^3 + a*x + b
   in Weierstrass coordinates and puts result in R. 
   Returns 1 if the addition worked (i.e. the modular inverse existed) 
   and 0 otherwise (resulting point is point at infinity) */
static int
ellW_add (ellW_point_t R, const ellW_point_t P, const ellW_point_t Q, 
          const residue_t a, const modulus_t m)
{
  residue_t lambda, u, v;
  int r;

  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);
  mod_init_noset0 (lambda, m);

  mod_sub (u, Q->y, P->y, m);
  mod_sub (v, Q->x, P->x, m);
  if (mod_inv (v, v, m) == 0)
  {
      /* Maybe we were trying to add two identical points? If so,
         use the ellW_double() function instead */
      if (mod_equal (P->x, Q->x, m) && mod_equal (P->y, Q->y, m))
	  r = ellW_double (R, P, a, m);
      else 
	{
	  /* Or maybe the points are negatives of each other? */
	  mod_neg (u, P->y, m);
	  if (mod_equal (P->x, Q->x, m) && mod_equal (u, Q->y, m))
	    r = 0; /* Signal point at infinity */
	  else
	    {
	      /* Neither identical, nor negatives (mod m). Looks like we
		 found a proper factor. FIXME: What do we do with it? */
	      r = 0;
	    }
	}
  }
  else
  {
      mod_mul (lambda, u, v, m);
      mod_sqr (u, lambda, m);
      mod_sub (u, u, P->x, m);
      mod_sub (u, u, Q->x, m);    /* x3 = u = lambda^2 - P->x - Q->x */
      mod_sub (v, P->x, u, m);
      mod_mul (v, v, lambda, m);
      mod_sub (R->y, v, P->y, m);
      mod_set (R->x, u, m);
      r = 1;
  }

  mod_clear (lambda, m);
  mod_clear (v, m);
  mod_clear (u, m);
  return r;
}


/* (x,y) <- e * (x,y) on the curve y^2 = x^3 + a*x + b (mod m) */
static int
ellW_mul_ui (ellW_point_t P, const unsigned long e, residue_t a, 
	     const modulus_t m)
{
  unsigned long i;
  ellW_point_t T;
  int tfinite; /* Nonzero iff T is NOT point at infinity */

  if (e == 0)
    return 0; /* signal point at infinity */

  ellW_init (T, m);

  i = ~(0UL);
  i -= i/2;   /* Now the most significant bit of i is set */
  while ((i & e) == 0)
    i >>= 1;

  ellW_set (T, P, m);
  tfinite = 1;
  i >>= 1;

  while (i > 0)
  {
      if (tfinite)
	tfinite = ellW_double (T, T, a, m);
      if (e & i)
      {
	  if (tfinite)
	      tfinite = ellW_add (T, T, P, a, m);
	  else
	  {
	      ellW_set (T, P, m);
	      tfinite = 1;
	  }
      }
      i >>= 1;
  }

  if (tfinite)
    ellW_set (P, T, m);

  ellW_clear (T, m);

  return tfinite;
}


/* Interpret the bytecode located at "code" and do the 
   corresponding elliptic curve operations on (x::z) */
/* static */ void
ellM_interpret_bytecode (ellM_point_t P, const char *bc, unsigned int bc_len,
                         const modulus_t m, const residue_t b)
{
  ellM_point_t A, B, C, t, t2;
  
  ellM_init (A, m);
  ellM_init (B, m);
  ellM_init (C, m);
  ellM_init (t, m);
  ellM_init (t2, m);

  ellM_set (A, P, m);

  for (unsigned i = 0; i < bc_len; i++)
  {
    switch (bc[i])
    {
      case 's': /* Swap A, B */
        ellM_swap (A, B, m);
        break;
      case 'i': /* Start of a subchain */
        ellM_set (B, A, m);
        ellM_set (C, A, m);
        ellM_double (A, A, m, b);
        break;
      case 'f': /* End of a subchain */
        ellM_add (A, A, B, C, b, m);
        break;
      case 1:
        ellM_add (t, A, B, C, b, m);
        ellM_add (t2, t, A, B, b, m);
        ellM_add (B, B, t, A, b, m);
        ellM_set (A, t2, m);
        break;
      case 2:
        ellM_add (B, A, B, C, b, m);
        ellM_double (A, A, m, b);
        break;
      case 3:
        ellM_add (C, B, A, C, b, m);
        ellM_swap (B, C, m);
        break;
      case 4:
        ellM_add (B, B, A, C, b, m);
        ellM_double (A, A, m, b);
        break;
      case 5:
        ellM_add (C, C, A, B, b, m);
        ellM_double (A, A, m, b);
        break;
      case 6:
        ellM_double (t, A, m, b);
        ellM_add (t2, A, B, C, b, m);
        ellM_add (A, t, A, A, b, m);
        ellM_add (C, t, t2, C, b, m);
        ellM_swap (B, C, m);
        break;
      case 7:
        ellM_add (t, A, B, C, b, m);
        ellM_add (B, t, A, B, b, m);
        ellM_double (t, A, m, b);
        ellM_add (A, A, t, A, b, m);
        break;
      case 8:
        ellM_add (t, A, B, C, b, m);
        ellM_add (C, C, A, B, b, m);
        ellM_swap (B, t, m);
        ellM_double (t, A, m, b);
        ellM_add (A, A, t, A, b, m);
        break;
      case 9:
        ellM_add (C, C, B, A, b, m);
        ellM_double (B, B, m, b);
        break;
      case 10:
        /* Combined final add of old subchain and init of new subchain [=fi] */
        ellM_add (B, A, B, C, b, m);
        ellM_set (C, B, m);
        ellM_double (A, B, m, b);
        break;
      case 11:
        /* Combined rule 3 and rule 0 [=\x3s] */
        ellM_add (C, B, A, C, b, m);
        /* (B,C,A) := (A,B,C)  */
        ellM_swap (B, C, m);
        ellM_swap (A, B, m);
        break;
      case 12:
        /* Combined rule 3, then subchain end/start [=\x3fi] */
        ellM_add (t, B, A, C, b, m);
        ellM_add (C, A, t, B, b, m);
        ellM_set (B, C, m);
        ellM_double (A, C, m, b);
        break;
      case 13:
        /* Combined rule 3, swap, rule 3 and swap, merged a bit [=\x3s\x3s] */
        ellM_set (t, B, m);
        ellM_add (B, B, A, C, b, m);
        ellM_set (C, A, m);
        ellM_add (A, A, B, t, b, m);
        break;
      default:
        printf ("#Unknown bytecode %u\n", (unsigned int) bc[i]);
        abort ();
    }
  }

  ellM_set (P, A, m);

  ellM_clear (A, m);
  ellM_clear (B, m);
  ellM_clear (C, m);
  ellM_clear (t, m);
  ellM_clear (t2, m);
}


/* Interpret the addition chain written in bc */
/* Computes Q = sP, where */
/* - s is the primorial exponent depending only on B1 */
/* - P is the initial point given in extended coord */
/* - Q is returned in projective coord */
static void
ellE_interpret_bytecode (ellE_point_t P, const char *bc, const unsigned int bc_len,
			 const modulus_t m, const residue_t a)
{
  unsigned char q;

  q = bc[0];       /* 'q':  permet de remplacer les couilles en coquilles [PG, Oct. 2016] */
  ASSERT (q & 1);  /* q is odd */

  unsigned char rP_size = (q+1)/2;
  ASSERT (rP_size <= 127);

  /* Precomputation phase */
  
  /* _2Pe = [2]P in extended coord */
  ellEe_point_t _2Pe;
  ellEe_init (_2Pe, m);

  ellEe_set_from_E (_2Pe, P, m);
  ellEe_double (_2Pe, _2Pe, m, a);

  /* _rP[i] = [2*i+1]P in extended coord */
  ellEe_point_t *_rP;
  _rP = (ellEe_point_t *) malloc (rP_size * sizeof (ellEe_point_t));

  ASSERT (_rP != NULL);

  for (int i=0 ; i < rP_size ; i++)
    ellEe_init (_rP[i], m);

  ellEe_set_from_E (_rP[0], P, m);

  for (int i = 1 ; i < rP_size ; i++)
    ellEe_add (_rP[i], _rP[i-1], _2Pe, m, a);

  /* Addition chain */
  
  /* Starting point (depends on bc[1] and bc[2]) */
  ellE_point_t Q;
  ellEe_point_t Te;
  ellE_init (Q, m);
  ellEe_init (Te, m);

  unsigned int i = bc[1] & 0x7F;
  if (i == 0x7F)
    ellEe_set (Te, _2Pe, m);
  else
    ellEe_set (Te, _rP[i], m);
  
  if (bc[1] & 0x80)
    {
      i = bc[2] & 0x7F;
      ellEe_add (Te, Te, _rP[i], m, a);

    }
  ellE_set_from_Ee (Q, Te, m);

  /* scan bc[i] for i >= 3 */
  for (unsigned int i = 3; i < bc_len; i++)
  {
    char b = bc[i];
    switch (b)
    {
      case ADDCHAIN_2DBL:
        ellE_double (Q, Q, m, a);
        no_break();
      case ADDCHAIN_DBL:
        ellE_double (Q, Q, m, a);
        break;
      default:
        ellEe_set (Te, _rP[b & 0x7f], m);
	      if (b & 0x80)
	        ellEe_neg (Te, m);
	      ellE_double_add (Q, Q, Te, m, a);
        break;
    }
  }
  
  ellE_set (P, Q, m);

  ellE_clear (Q, m);
  ellEe_clear (_2Pe, m);
  ellEe_clear (Te, m);
  for (i=0 ; i < rP_size ; i++)
    ellEe_clear (_rP[i], m);
  free (_rP);
}


/* Produces curve in Montgomery form from sigma value.
   Return 1 if it worked, 0 if a modular inverse failed.
   If modular inverse failed, return non-invertible value in x. */
static int
Brent12_curve_from_sigma (residue_t A, residue_t x, const residue_t sigma, 
			  const modulus_t m)
{
  residue_t u, v, t, b, z;
  int r;

  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);
  mod_init_noset0 (t, m);
  mod_init_noset0 (b, m);
  mod_init_noset0 (z, m);

  /* compute b, x */
  mod_add (v, sigma, sigma, m);
  mod_add (v, v, v, m);         /* v = 4*sigma */
  mod_sqr (u, sigma, m);
  mod_set1 (b, m);
  mod_add (t, b, b, m);
  mod_add (t, t, t, m);
  mod_add (t, t, b, m);         /* t = 5 */
  mod_sub (u, u, t, m);         /* u = sigma^2 - 5 */
  mod_sqr (t, u, m);
  mod_mul (x, t, u, m);         /* x = u^3 */
  mod_sqr (t, v, m);
  mod_mul (z, t, v, m);         /* z = v^3 */
  mod_mul (t, x, v, m);         /* t = x*v = u^3*v */
  mod_add (b, t, t, m);
  mod_add (b, b, b, m);         /* b = 4*u^3*v */
  mod_add (t, u, u, m);
  mod_add (t, t, u, m);         /* t = 3*u */
  mod_sub (u, v, u, m);         /* t2 = v-u  (stored in u) */
  mod_add (v, t, v, m);         /* t3 = 3*u + v (stored in v) */
  mod_sqr (t, u, m);
  mod_mul (u, t, u, m);         /* t4 = (u-v)^3 (stored in u) */
  mod_mul (A, u, v, m);         /* A = (u-v)^3 * (3*u + v) */
  mod_mul (v, b, z, m);         /* t5 = b*z (stored in v) */

  r = mod_inv (u, v, m);        /* t6 = 1/(b*z) (stored in u) */
  if (r == 0) /* non-trivial gcd */
    {
      mod_set (x, v, m);
    }
  else
    {
      mod_mul (v, u, b, m);     /* t7 = 1/z (stored in v) */
      mod_mul (x, x, v, m);     /* x := x/z */
      mod_mul (v, u, z, m);     /* t8 = 1/b (stored in v) */
      mod_mul (t, A, v, m);     /* t = A/b = (u-v)^3 * (3*u + v) / (4*u^3*v) */
      mod_set1 (u, m);
      mod_add (u, u, u, m);
      mod_sub (A, t, u, m);     /* A = (u-v)^3 * (3*u + v) / (4*u^3*v) - 2 */
    }

  mod_clear (z, m);
  mod_clear (b, m);
  mod_clear (t, m);
  mod_clear (v, m);
  mod_clear (u, m);

  return r;
}

/* Produces curve in Montgomery parameterization from k value, using
   parameters for a torsion 12 curve as in Montgomery's thesis (6.1).
   Return 1 if it worked, 0 if a modular inverse failed. 
   If a modular inverse failed, the non-invertible value is stored in x.

   The elliptic curve is B y^2 = x^3 + A x^2 + x
   
   with A = (-3*a^4-6*a^2+1)/(4*a^3) = (1/a - 3*a*(a^2 + 2))/(2*a)^2
   and B = (a^2-1)^2/(4*a^3).

   and initial point x = (3*a^2+1)/(4*a).

   A and x are obtained from u and v such that (u,v) = k*P on the curve
   v^2 = u^3 - 12*u, where P = (-2, 4).

   In Sage notation:
   E=EllipticCurve([-12,0])
   P=E(-2,4)
   k=2
   kP=k*P; u=kP[0]; v=kP[1]
   t2 = (u^2-12)/(4*u)
   a = (u^2 - 4*u - 12)/(u^2 + 12*u - 12)
   A = (-3*a^4-6*a^2+1)/(4*a^3)
   B = (a^2-1)^2/(4*a^3)
   x = (3*a^2+1)/(4*a)

   We want t^2 = (u^2-12)/4u, and a=(t^2-1)/(t^2+3), thus
   a = (u^2 - 4*u - 12)/(u^2 + 12*u - 12). 
   We need both a and 1/a, so we can compute the inverses of both
   u^2 - 4*u - 12 and u^2 + 12*u - 12 with a single batch inversion.

   For k=2, we get u=4, v=-4, t=-1/2, a=-3/13, 
     A=-4798/351, B=-6400/351 and x=-49/39.

   For k=3, we get u=-2/9, v=-44/27, t=11/3, a=28/37, 
     A=-6409583/3248896, B=342225/3248896, and x=3721/4144.
*/
static int
Montgomery12_curve_from_k (residue_t A, residue_t x, const unsigned long k, 
		           const modulus_t m)
{
  residue_t u, v, a, t2, one;
  int r = 0;
  
  /* We want a multiple of the point (-2,4) on the curve Y^2=X^3-12*X.
     The curve has 2-torsion with torsion point (0,0), but adding it 
     does not seem to change the ECM curve we get out in the end. */
  mod_init_noset0 (a, m);
  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);
  mod_init_noset0 (one, m);
  mod_init_noset0 (t2, m);
  
  mod_set1 (one, m);

  if (k == 2)
    {
      /* For k=2, we need A=-4798/351 = -13 - 1/13 - 16/27 
         and x=-49/39 = 1/13 - 1/3 - 1. */
      mod_div13 (u, one, m);    /* u = 1/13 */
      mod_div3 (v, one, m);     /* v = 1/3 */
      mod_sub (x, u, v, m);     /* x = 1/13 - 1/3 = -10/39 */
      mod_sub (x, x, one, m);   /* x = -10/39 - 1 = -49/39 */
      mod_sub (A, one, v, m);   /* A = 1 - 1/3 = 2/3 */
      mod_div3 (A, A, m);       /* A = 2/9 */
      mod_add1 (A, A, m);       /* A = 11/9 */
      mod_div3 (A, A, m);       /* A = 11/27 */
      mod_sub (A, A, one, m);   /* A = -16/27 */
      mod_sub (A, A, u, m);     /* A = -16/27 - 1/13 = -235/351 */
      mod_add (u, one, one, m); /* u = 2 */
      mod_add (u, u, one, m);   /* u = 3 */
      mod_add (u, u, u, m);     /* u = 6 */
      mod_add (u, u, u, m);     /* u = 12 */
      mod_add1 (u, u, m);       /* u = 13 */
      mod_sub (A, A, u, m);     /* A = -235/351 - 13 = -4798/351 */

      mod_clear (one, m);
      mod_clear (t2, m);
      mod_clear (v, m);
      mod_clear (u, m);
      mod_clear (a, m);
      return 1;
    }
  else if (k == 3)
    {
#if 0
      mod_set_ul (v, 37UL, m);
      mod_div2 (v, v, m);
      mod_div2 (v, v, m);
      mod_div7 (v, v, m);      /* v = 37/28 = 1/a */
#else
      mod_div2 (v, one, m);	/* v = 1/2 */
      mod_div2 (v, v, m);	/* v = 1/4 */
      mod_add1 (v, v, m);       /* v = 5/4 */
      mod_add1 (v, v, m);       /* v = 9/4 */
      mod_div7 (v, v, m);       /* v = 9/28 */
      mod_add1 (v, v, m);       /* v = 37/28 = 1/a */
#endif
      /* TODO: Write a mod_div37()? Or a general mod_div_n() for small n? */
      if (mod_inv (a, v, m) == 0)  /* a = 28/37 */
        {
	  mod_set (x, v, m);
	  goto clear_and_exit;
        }
    }
  else
    {
      mod_add (v, one, one, m);
      mod_neg (u, v, m);    /* u = -2 */
      mod_add (v, v, v, m); /* v = 4 */
      mod_add (a, v, v, m);
      mod_add (a, a, v, m);
      mod_neg (a, a, m);    /* a = -12 */
      {
        ellW_point_t T;
        ellW_init (T, m);
        mod_set (T[0].x, u, m);
        mod_set (T[0].y, v, m);
        ellW_mul_ui (T, k, a, m);
        mod_set (u, T[0].x, m);
        mod_set (v, T[0].y, m);
        ellW_clear (T, m);
      }

      /* Now we have an $u$ such that $v^2 = u^3-12u$ is a square */
      /* printf ("Montgomery12_curve_from_k: u = %lu\n", mod_get_ul (u)); */
      
      /* We want a = (u^2 - 4*u - 12)/(u^2 + 12*u - 12). 
         We need both $a$ and $1/a$, so we can compute the inverses of both
         u^2 - 4*u - 12 and u^2 + 12*u - 12 with a single batch inversion. */

      mod_sqr (t2, u, m);     /* t2 = u^2 */
      mod_sub (u, u, one, m);
      mod_add (u, u, u, m);
      mod_add (u, u, u, m);   /* u' = 4u - 4 */
      mod_sub (v, t2, u, m);  /* v = u^2 - 4u + 4 */
      mod_add (t2, t2, u, m);
      mod_add (t2, t2, u, m);
      mod_add (u, t2, u, m);  /* u'' = u^2 + 12u - 12 */
      mod_add (t2, one, one, m);
      mod_add (t2, t2, t2, m);
      mod_add (t2, t2, t2, m);
      mod_add (t2, t2, t2, m); /* t2 = 16 */
      mod_sub (v, v, t2, m);   /* v = u^2 - 4u - 12 */
      
      mod_mul (t2, u, v, m);
      if (mod_inv (t2, t2, m) == 0)
	{
	  mod_set (x, t2, m);
	  goto clear_and_exit;
	}

      /* Now 
         u'' = u^2 + 12u - 12
         v  = u^2 - 4u - 12
         t2 = 1 / ( (u^2 + 12u - 12) * (u^2 - 4u - 12) ).
         We want 
         a   = (u^2 - 4u - 12)/(u^2 + 12u - 12) and
         1/a = (u^2 + 12u - 12)/(u^2 - 4u - 12) */
      mod_sqr (a, v, m);
      mod_mul (a, a, t2, m);
      mod_sqr (v, u, m);
      mod_mul (v, v, t2, m);
    }

  /* Here we have $a$ in a, $1/a$ in v */
  mod_sqr (u, a, m);      /* a^2 */
  mod_add (A, u, one, m);
  mod_add (A, A, one, m); /* a^2 + 2 */
  mod_add (t2, A, A, m);
  mod_add (A, A, t2, m);  /* 3*(a^2 + 2) */
  mod_mul (t2, A, a, m);
  mod_set (A, v, m);
  mod_sub (A, A, t2, m);  /* 1/a - 3 a (a^2 + 2) */
  mod_div2 (v, v, m);     /* v = 1/(2a) */
  mod_sqr (t2, v, m);     /* t2 = 1/(2a)^2 */
  mod_mul (A, A, t2, m);  /* A = [1/a - 3 a (a^2 + 2)]/(2a)^2 */

  mod_add (x, u, u, m);   /* 2a^2 */
  mod_add (x, x, u, m);   /* 3*a^2 */
  mod_add (x, x, one, m); /* 3*a^2 + 1 */
  mod_div2 (v, v, m);     /* v = 1/(4a) */
  mod_mul (x, x, v, m);   /* x = (3*a^2 + 1)/(4a) */
  r = 1;

clear_and_exit:  
  mod_clear (one, m);
  mod_clear (t2, m);
  mod_clear (v, m);
  mod_clear (u, m);
  mod_clear (a, m);
  return r;
}


/* Produces curve in Montgomery parameterization from k value, using
   parameters for a torsion 16 curve as in Montgomery's thesis.
   Return 1 if it worked, 0 if a modular inverse failed.
   Currently can produce only one, hard-coded curve that is cheap 
   to initialise */
static int
Montgomery16_curve_from_k (residue_t b, residue_t x, const unsigned long k, 
		           const modulus_t m)
{
#if 0
  if (k == 1UL)
    {
      /* ERROR: this curve has rank 0! That explains why it only ever
         finds trivial factorizations... */ 
      /* Make curve corresponding to (a,b,c) = (4, -3, 5) in 
         Montgomery's thesis, equation (6.2.2). Then A = 337/144, 
         b = (A+2)/4 = (25/24)^2. x = -3/2, see table 6.2.1. 
         This curve is cheap to initialise, three div2, one div3, two add,
	 one mul */
      residue_t t, t2, one;
      
      mod_init (t, m);
      mod_init (t2, m);
      mod_init (one, m);
      
      mod_set1 (one, m);
      mod_div2 (t, one, m);     /* t = 1/2 */
      mod_div2 (t2, t, m);      /* t2 = 1/4 */
      mod_add (t, t, one, m);   /* t = 3/2 */
      mod_neg (x, t, m);        /* x = -3/2 */
      mod_div2 (t2, t2, m);     /* t2 = 1/8 */
      mod_div3 (t2, t2, m);     /* t2 = 1/24 */
      mod_add (t2, t2, one, m); /* t2 = 25/24 */
      mod_sqr (b, t2, m);       /* b = 625/576 */

#ifdef WANT_ASSERT_EXPENSIVE
      mod_add (t, x, x, m);
      mod_add (t, t, one, m);
      mod_add (t, t, one, m);
      mod_add (t, t, one, m);
      ASSERT (mod_is0 (t, m));
      mod_set_ul (t, 576UL, m);
      mod_mul (t2, b, t, m);
      mod_set_ul (t, 625UL, m);
      ASSERT (mod_equal (t, t2, m));
#endif
      
      mod_clear (t, m);
      mod_clear (t2, m);
      mod_clear (one, m);
    }
#endif
  if (k == 1UL)
    {
      /* Make curve corresponding to (a,b,c) = (8, 15, 17) in 
         Montgomery's thesis, equation (6.2.2). Then A = 54721/14400, 
         b = (A+2)/4 = (289/240)^2. x = 8/15, see table 6.2.1. 
         This curve is cheap to initialise: four div2, one div3, two div5,
         three add, one mul */
      residue_t t, t2, one;
      
      mod_init (t, m);
      mod_init (t2, m);
      mod_init (one, m);
      
      mod_set1 (one, m);
      mod_div3 (t, one, m);
      mod_div5 (t2, one, m);
      mod_add (x, t, t2, m);  /* x = 1/3 + 1/5 = 8/15 */
      mod_div2(t, t, m);
      mod_div2(t, t, m);
      mod_div2(t, t, m);
      mod_div2(t, t, m);      /* t = 1/48 */
      mod_add (t, t, one, m); /* t = 49/48 */
      mod_div5 (t, t, m);     /* t = 49/240 */
      mod_add (t, t, one, m); /* t = 289/240 */

      mod_sqr (b, t, m);      /* b = 83521/57600 */
      
#ifdef WANT_ASSERT_EXPENSIVE
      mod_set_ul (t, 15UL, m);
      mod_mul (t2, x, t, m);
      mod_set_ul (t, 8UL, m);
      ASSERT (mod_equal (t, t2, m));
      mod_set_ul (t, 57600UL, m);
      mod_mul (t2, b, t, m);
      mod_set_ul (t, 83521UL, m);
      ASSERT (mod_equal (t, t2, m));
#endif
      mod_clear (t, m);
      mod_clear (t2, m);
      mod_clear (one, m);
    }
  else
    {
      abort();
    }

  return 1;
}



static int 
Twisted_Edwards16_curve_from_sigma (residue_t d, ellE_point_t P,
				    MAYBE_UNUSED const unsigned long sigma, 
				    const modulus_t m)
{
  residue_t u, f;
  const Edwards_curve_t *E = &Ecurve14;

  residue_t xn, xd, yn, yd, dd;
  
  mod_init (u, m);
  mod_init (f, m);
  
  mod_set_ul (xn, E->x_numer, m);
  mod_set_ul (xd, E->x_denom, m);
  
  /* (xn, yn) = P (mod m) */
  if ( mod_inv (u, xd, m) == 0)
    {
      mod_gcd (f, xd, m);
      mod_clear (u, m);
      return 0;
    }
  mod_mul (P->x, xn, u, m);
  
  mod_set_ul (yn, E->y_numer, m);
  mod_set_ul (yd, E->y_denom, m);
  if (mod_inv (u, yd, m) == 0)
    {
      mod_gcd (f, yd, m);
      mod_clear (u, m);
      ellE_clear (P, m);
      return 0;
    }
  mod_mul (P->y, yn, u, m);
  
  mod_set1 (P->z, m);
  
  /* Reduce d = dn/dd mod m */
  mod_set_ul (d, E->d_numer, m);
  mod_set_ul (dd, E->d_denom, m);
  if (mod_inv (u, dd, m) == 0)
    {
      mod_gcd (f, dd, m);
      mod_clear (u, m);
      ellE_clear (P, m);
      return 0;
    }
  mod_mul (d, d, u, m);
  
  mod_clear (u, m);
  mod_clear (f, m);

  return 1;
}


/* Make a curve of the form y^2 = x^3 + a*x^2 + b with a valid point
   (x, y) from a curve Y^2 = X^3 + A*X^2 + X. The value of b will not
   be computed. 
   x and X may be the same variable. */
static int
curveW_from_Montgomery (residue_t a, ellW_point_t P, const residue_t X, 
                        const residue_t A, const modulus_t m)
{
  residue_t g, one;
  int r;

  mod_init_noset0 (g, m);
  mod_init_noset0 (one, m);
  mod_set1 (one, m);

  mod_add (g, X, A, m);
  mod_mul (g, g, X, m);
  mod_add (g, g, one, m);
  mod_mul (g, g, X, m); /* G = X^3 + A*X^2 + X */
  /* printf ("curveW_from_Montgomery: Y^2 = %lu\n", g[0]); */

  /* Now (x,1) is on the curve G*Y^2 = X^3 + A*X^2 + X. */
  r = mod_inv (g, g, m);
  if (r != 0)
  {
      mod_set (P[0].y, g, m);       /* y = 1/G */
      mod_div3 (a, A, m);
      mod_add (P[0].x, X, a, m);
      mod_mul (P[0].x, P[0].x, g, m); /* x = (X + A/3)/G */
      mod_mul (a, a, A, m);
      mod_sub (a, one, a, m);
      mod_mul (a, a, g, m);
      mod_mul (a, a, g, m); /* a = (1 - (A^2)/3)/G^2 */
  }
  else
    fprintf (stderr, "curveW_from_Montgomery: r = 0\n");

  mod_clear (one, m);
  mod_clear (g, m);

  return r;
}


/* Multiplies x[1] by z[2]*z[3]*z[4]...*z[n],
   x[2] by z[1]*z[3]*z[4]...*z[n] etc., generally
   x[i] by \prod_{1\leq j \leq n, j\neq i} z[j]
   Requires n > 1. Uses 4n-6 multiplications. */

MAYBE_UNUSED
static void ATTRIBUTE((__noinline__))
common_z (const int n1, residue_t *x1, residue_t *z1, 
	  const int n2, residue_t *x2, residue_t *z2,
	  const modulus_t m)
{
  const int n = n1 + n2;
  int i, j;
  residue_t *t, p;
  const int verbose = 0;
  
  if (verbose)
    printf ("common_z: n1 = %d, n2 = %d, sum = %d, nr muls=%d\n", 
            n1, n2, n1 + n2, 4*(n1 + n2) - 6);
  
  if (n < 2)
    return;
  
  t = (residue_t *) malloc (n * sizeof (residue_t));
  for (i = 0; i < n; i++)
    mod_init (t[i], m);
  
  /* Set t[i] = z_0 * z_1 * ... * z_i, where the z_i are taken
     from the two lists z1 and z2 */
  i = j = 0;
  if (n1 == 0)
    mod_set (t[0], z2[j++], m);
  else
    mod_set (t[0], z1[i++], m);
  
  for ( ; i < n1; i++)
    mod_mul (t[i], t[i - 1], z1[i], m);
  
  for ( ; j < n2; j++)
    mod_mul (t[j + n1], t[j + n1 - 1], z2[j], m);
  
  /* Now t[i] contains z_0 * ... * z_i */
  
#ifdef ECM15_UL
  if (verbose) {
    printf ("t[] = {");
    for (int verbose_i = 0; verbose_i < n; verbose_i++)
      printf ("{%lu,%lu}%s", t[verbose_i][0], t[verbose_i][1], (verbose_i < n-1) ? ", " : "");
    printf ("}\n");
  }
#endif  
  mod_init (p, m);
  
  i = n - 1;
  if (i < n1) /* <==>  n2 == 0 */
    mod_mul (x1[i], x1[i], t[n - 2], m);
  else
    mod_mul (x2[i - n1], x2[i - n1], t[n - 2], m);
  
  /* Init the accumulator p, either to the last element of z2 if z2 is 
     non-empty, or to the last element of z1 if z2 is empty */
  if (n2 > 0)
    mod_set (p, z2[n2 - 1], m);
  else
    mod_set (p, z1[n1 - 1], m);
  
  for (i = n2 - 2; i > -n1 && i >= 0; i--)
    {
      /* Here p = z_{i+1} * ... * z_{n-1} */
      mod_mul (x2[i], x2[i], p, m);
      mod_mul (x2[i], x2[i], t[i + n1 - 1], m);
      mod_mul (p, p, z2[i], m);
    }
  
  /* n1 = 0  =>  i = 0 */
  /* n1 > 0  =>  i = -1 or -2 */
  
  for (i = i + n1 ; i > 0; i--)
    {
      /* Here p = z_{i+1} * ... * z_{n-1} */
      mod_mul (x1[i], x1[i], p, m);
      mod_mul (x1[i], x1[i], t[i-1], m);
      mod_mul (p, p, z1[i], m);
    }
  
  if (n1 > 0)
    mod_mul (x1[0], x1[0], p, m);
  else
    mod_mul (x2[0], x2[0], p, m);
  
  mod_clear (p, m);
  
  for (i = 0; i < n; i++)
    mod_clear (t[i], m);
  free (t);
  t = NULL;
}


static int ATTRIBUTE((__noinline__))
ecm_stage2 (residue_t r, const ellM_point_t P, const stage2_plan_t *plan, 
	    const residue_t b, const modulus_t m)
{
  ellM_point_t Pd, Pt; /* d*P, i*d*P, (i+1)*d*P and a temp */
  residue_t *Pid_x, *Pid_z, *Pj_x, *Pj_z; /* saved i*d*P, i0 <= i < i1, 
              and jP, j in S_1, x and z coordinate stored separately */
  residue_t a, a_bk, t;
  unsigned int i, k, l;
  int bt = 0;
  const int verbose = 0;
  
  ellM_init (Pt, m);
  mod_init_noset0 (t, m);
  mod_init_noset0 (a, m);
  Pj_x = (residue_t *) malloc (plan->s1 * sizeof(residue_t));
  ASSERT (Pj_x != NULL);
  Pj_z = (residue_t *) malloc (plan->s1 * sizeof(residue_t));
  ASSERT (Pj_z != NULL);
  ASSERT(plan->i0 < plan->i1);
  Pid_x = (residue_t *) malloc ((plan->i1 - plan->i0) * sizeof(residue_t));
  ASSERT (Pid_x != NULL);
  Pid_z = (residue_t *) malloc ((plan->i1 - plan->i0) * sizeof(residue_t));
  ASSERT (Pid_z != NULL);
  for (i = 0; i < plan->s1; i++)
    {
      mod_init_noset0 (Pj_x[i], m);
      mod_init_noset0 (Pj_z[i], m);
    }
  for (i = 0; i < plan->i1 - plan->i0; i++)
    {
      mod_init_noset0 (Pid_x[i], m);
      mod_init_noset0 (Pid_z[i], m);
    }

#if COUNT_ELLM_OPS
  ellM_add_count = ellM_double_count = 0;
#endif

  if (verbose)
    printf ("Stage 2: P = (%lu::%lu)\n", 
            mod_get_ul (P[0].x, m), mod_get_ul (P[0].z, m));
  
  /* Compute jP for j in S_1. Compute all the j, 1 <= j < d/2, gcd(j,d)=1 
     with two arithmetic progressions 1+6k and 5+6k (this assumes 6|d).
     We need two values of each progression (1, 7 and 5, 11) and the 
     common difference 6. These can be computed with the Lucas chain
     1, 2, 3, 5, 6, 7, 11 at the cost of 6 additions and 1 doubling. 
     For d=210, generating all 24 desired values 1 <= j < 210/2, gcd(j,d)=1, 
     takes 6+16+15=37 point additions. If d=30, we could use 
     1,2,3,4,6,7,11,13 which has 5 additions and 2 doublings */
  
  ASSERT (plan->d % 6 == 0);
  {
    ellM_point_t ap1_0, ap1_1, ap5_0, ap5_1, P2, P6;
    int i1, i5;
    ellM_init (ap1_0, m);
    ellM_init (ap1_1, m);
    ellM_init (ap5_0, m);
    ellM_init (ap5_1, m);
    ellM_init (P6, m);
    ellM_init (P2, m);

    /* Init ap1_0 = 1P, ap1_1 = 7P, ap5_0 = 5P, ap5_1 = 11P
       and P6 = 6P */
    ellM_set (ap1_0, P, m);            /* ap1_0 = 1*P */
    ellM_double (P2, P, m, b);         /* P2 = 2*P */
    ellM_add (P6, P2, P, P, b, m);     /* P6 = 3*P (for now) */
    ellM_add (ap5_0, P6, P2, P, b, m); /* 5*P = 3*P + 2*P */
    ellM_double (P6, P6, m, b);        /* P6 = 6*P = 2*(3*P) */
    ellM_add (ap1_1, P6, P, ap5_0, b, m); /* 7*P = 6*P + P */
    ellM_add (ap5_1, P6, ap5_0, P, b, m); /* 11*P = 6*P + 5*P */
    
    /* Now we generate all the j*P for j in S_1 */
    /* We treat the first two manually because those might correspond 
       to ap1_0 = 1*P and ap5_0 = 5*P */
    k = 0;
    if (plan->s1 > k && plan->S1[k] == 1)
      {
        mod_set (Pj_x[k], ap1_0[0].x, m);
        mod_set (Pj_z[k], ap1_0[0].z, m);
        k++;
      }
    if (plan->s1 > k && plan->S1[k] == 5)
      {
        mod_set (Pj_x[k], ap5_0[0].x, m);
        mod_set (Pj_z[k], ap5_0[0].z, m);
        k++;
      }
    
    i1 = 7;
    i5 = 11;
    while (k < plan->s1)
      {
        if (plan->S1[k] == i1)
          {
            mod_set (Pj_x[k], ap1_1[0].x, m);
            mod_set (Pj_z[k], ap1_1[0].z, m);
	    k++;
	    continue;
          }
        if (plan->S1[k] == i5)
          {
            mod_set (Pj_x[k], ap5_1[0].x, m);
            mod_set (Pj_z[k], ap5_1[0].z, m);
	    k++;
	    continue;
          }
	
        ellM_add (Pt, ap1_1, P6, ap1_0, b, m);
        ellM_set (ap1_0, ap1_1, m);
        ellM_set (ap1_1, Pt, m);
        i1 += 6;
	
        ellM_add (Pt, ap5_1, P6, ap5_0, b, m);
        ellM_set (ap5_0, ap5_1, m);
        ellM_set (ap5_1, Pt, m);
        i5 += 6;
      }

    if ((unsigned) (i1 + i5) < plan->d)
      {
        if (i1 < i5)
          {
            ellM_add (Pt, ap1_1, P6, ap1_0, b, m);
            ellM_set (ap1_0, ap1_1, m);
            ellM_set (ap1_1, Pt, m);
            i1 += 6;
          }
        else
          {
            ellM_add (Pt, ap5_1, P6, ap5_0, b, m);
            ellM_set (ap5_0, ap5_1, m);
            ellM_set (ap5_1, Pt, m);
            i5 += 6;
          }
      }
    
    ellM_init (Pd, m);
#if 0
    /* Also compute Pd = d*P while we've got 6*P */
    ellM_mul_ul (Pd, P6, plan->d / 6, m, b); /* slow! */
#else
    ASSERT ((unsigned) (i1 + i5) == plan->d);
    if (i1 + 4 == i5)
      {
        ellM_double (P2, P2, m, b); /* We need 4P for difference */
        ellM_add (Pd, ap1_1, ap5_1, P2, b, m);
      }
    else if (i5 + 2 == i1)
      {
        ellM_add (Pd, ap1_1, ap5_1, P2, b, m);
      }
    else
      abort ();
#endif

    ellM_clear (ap1_0, m);
    ellM_clear (ap1_1, m);
    ellM_clear (ap5_0, m);
    ellM_clear (ap5_1, m);
    ellM_clear (P6, m);
    ellM_clear (P2, m);

  }

  if (verbose)
    {
      printf ("Pj = [");
      for (i = 0; i < plan->s1; i++)
        printf ("%s(%lu::%lu)", (i>0) ? ", " : "", 
                mod_get_ul (Pj_x[i], m), mod_get_ul (Pj_z[i], m));
      printf ("]\nPd = (%lu::%lu)\n",
                mod_get_ul (Pd[0].x, m), mod_get_ul (Pd[0].z, m));
    }
  
  /* Compute idP for i0 <= i < i1 */
  {
    ellM_point_t Pid, Pid1;
    
    ellM_init (Pid, m);
    ellM_init (Pid1, m);
    k = 0; i = plan->i0;

    /* If i0 == 0, we simply leave the first point at (0::0) which is the
       point at infinity */
    if (plan->i0 == 0)
      {
	mod_set0 (Pid_x[k], m);
	mod_set0 (Pid_z[k], m);
	k++;
	i++;
      }

    /* Todo: do both Pid and Pid1 with one addition chain */
    ellM_mul_ul (Pid, Pd, i, m, b); /* Pid = i_0 d * P */
    mod_set (Pid_x[k], Pid[0].x, m);
    mod_set (Pid_z[k], Pid[0].z, m);
    k++; i++;
    if (i < plan->i1)
      {
        ellM_mul_ul (Pid1, Pd, i, m, b); /* Pid = (i_0 + 1) d * P */
        mod_set (Pid_x[k], Pid1[0].x, m);
        mod_set (Pid_z[k], Pid1[0].z, m);
        k++; i++;
      }
    while (i < plan->i1)
      {
        ellM_add (Pt, Pid1, Pd, Pid, b, m);
        ellM_set (Pid, Pid1, m);
        ellM_set (Pid1, Pt, m);
        mod_set (Pid_x[k], Pt[0].x, m);
        mod_set (Pid_z[k], Pt[0].z, m);
        k++; i++;
      }

    ellM_clear (Pd, m);
    ellM_clear (Pid, m);
    ellM_clear (Pid1, m);
  }

  if (verbose)
    {
      printf ("Pid = [");
      for (i = 0; i < plan->i1 - plan->i0; i++)
        printf ("%s(%lu:%lu)", (i>0) ? ", " : "", 
	        mod_get_ul (Pid_x[i], m), mod_get_ul (Pid_z[i], m));
      printf ("]\n");
    }

  /* Now we've computed all the points we need, so multiply each by
     the Z-coordinates of all the others, using Zimmermann's 
     two product-lists trick. 
     If i0 == 0, then Pid[0] is the point at infinity (0::0),
     so we skip that one */
  {
    int skip = (plan->i0 == 0) ? 1 : 0;
#if defined(ECM15_UL)
    if (verbose)
      {
        residue_t *x1 = Pj_x, *z1 = Pj_z, *x2 = Pid_x + skip, *z2 = Pid_z + skip;
        int n1 = plan->s1, n2 = plan->i1 - plan->i0 - skip;
        
        printf ("Before common_z():\n");
        printf ("x1 = {");
        for (int verbose_i = 0; verbose_i < n1; verbose_i++)
          printf ("{%luUL,%luUL}%s", x1[verbose_i][0], x1[verbose_i][1], (verbose_i < n1-1) ? ", " : "}\n");
        printf ("z1 = {");
        for (int verbose_i = 0; verbose_i < n1; verbose_i++)
          printf ("{%luUL,%luUL}%s", z1[verbose_i][0], z1[verbose_i][1], (verbose_i < n1-1) ? ", " : "}\n");
        printf ("x2 = {");
        for (int verbose_i = 0; verbose_i < n2; verbose_i++)
          printf ("{%luUL,%luUL}%s", x2[verbose_i][0], x2[verbose_i][1], (verbose_i < n2-1) ? ", " : "}\n");
        printf ("z2 = {");
        for (int verbose_i = 0; verbose_i < n2; verbose_i++)
          printf ("{%luUL,%luUL}%s", z2[verbose_i][0], z2[verbose_i][1], (verbose_i < n2-1) ? ", " : "}\n");
      }
#endif
    common_z (plan->s1, Pj_x, Pj_z, plan->i1 - plan->i0 - skip, 
	      Pid_x + skip, Pid_z + skip, m);
#if defined(ECM15_UL)
    if (verbose)
      {
        residue_t *x1 = Pj_x, *z1 = Pj_z, *x2 = Pid_x + skip, *z2 = Pid_z + skip;
        int n1 = plan->s1, n2 = plan->i1 - plan->i0 - skip;
        
        printf ("After common_z():\n");
        printf ("x1 = {");
        for (int verbose_i = 0; verbose_i < n1; verbose_i++)
          printf ("{%luUL,%luUL}%s", x1[verbose_i][0], x1[verbose_i][1], (verbose_i < n1-1) ? ", " : "}\n");
        printf ("z1 = {");
        for (int verbose_i = 0; verbose_i < n1; verbose_i++)
          printf ("{%luUL,%luUL}%s", z1[verbose_i][0], z1[verbose_i][1], (verbose_i < n1-1) ? ", " : "}\n");
        printf ("x2 = {");
        for (int verbose_i = 0; verbose_i < n2; verbose_i++)
          printf ("{%luUL,%luUL}%s", x2[verbose_i][0], x2[verbose_i][1], (verbose_i < n2-1) ? ", " : "}\n");
        printf ("z2 = {");
        for (int verbose_i = 0; verbose_i < n2; verbose_i++)
          printf ("{%luUL,%luUL}%s", z2[verbose_i][0], z2[verbose_i][1], (verbose_i < n2-1) ? ", " : "}\n");
      }
#endif
  }

  if (verbose)
    {
      printf ("After canonicalizing:\nPj = [");
      for (i = 0; i < plan->s1; i++)
        printf ("%s(%lu:%lu)", (i>0) ? ", " : "", 
                mod_get_ul (Pj_x[i], m), mod_get_ul (Pj_z[i], m));
      printf ("]\n");
      
      printf ("Pid = [");
      for (i = 0; i < plan->i1 - plan->i0; i++)
        printf ("%s(%lu:%lu)", (i>0) ? ", " : "", 
                mod_get_ul (Pid_x[i], m), mod_get_ul (Pid_z[i], m));
      printf ("]\n");
      fflush(stdout);
    }

  /* Now process all the primes p = id - j, B1 < p <= B2 and multiply
     (id*P)_x - (j*P)_x to the accumulator */

  /* Init the accumulator to Pj[0], which contains the product of
     the Z-coordinates of all the precomputed points, except Pj_z[0]
     which is equal to P, and we know that one is coprime to the modulus. 
     Maybe one of the others was zero (mod p) for some prime factor p. */

  mod_set (a, Pj_x[0], m);
  mod_init_noset0 (a_bk, m); /* Backup value of a, in case we get a == 0 */
  mod_set (a_bk, a, m);

  i = 0;
  l = 0;
  unsigned char j = plan->pairs[0];
  while (j != NEXT_PASS) 
    {
      __asm__ volatile ("# ECM stage 2 loop here");
      while (j < NEXT_D && j < NEXT_PASS)
	{
	  mod_sub (t, Pid_x[i], Pj_x[j], m);
	  j = plan->pairs[++l];
	  mod_mul (a, a, t, m);
	}
      
#if ECM_BACKTRACKING
      /* See if we got a == 0. If yes, restore previous a value and
	 end stage 2. Let's hope not all factors were found since
	 the last d increase. */
      if (mod_is0 (a, m))
	{
	  mod_set (a, a_bk, m);
	  bt = 1;
	  break;
	}
      mod_set (a_bk, a, m); /* Save new a value */
#endif
      
      if (j == NEXT_D)
	{
	  i++;
	  j = plan->pairs[++l];
	  ASSERT (i < plan->i1 - plan->i0);
	}
    }

  if (verbose)
    printf ("Accumulator = %lu\n", mod_get_ul (a, m));

#if COUNT_ELLM_OPS
  printf("Stage 2 used %lu point additions and %lu point doublings\n",
         ellM_add_count, ellM_double_count);
#endif
  
  mod_set (r, a, m);
  
  /* Clear everything */
  
  for (i = 0; i < plan->s1; i++)
    {
      mod_clear (Pj_x[i], m);
      mod_clear (Pj_z[i], m);
    }
  free (Pj_x);
  Pj_x = NULL;
  free (Pj_z);
  Pj_z = NULL;

  for (i = 0; i < plan->i1 - plan->i0; i++)
    {
      mod_clear (Pid_x[i], m);
      mod_clear (Pid_z[i], m);
    }
  free (Pid_x);
  Pid_x = NULL;
  free (Pid_z);
  Pid_z = NULL;
  
  ellM_clear (Pt, m);
  mod_clear (t, m);
  mod_clear (a, m);
  mod_clear (a_bk, m);
  return bt;
}

/* Stores any factor found in f_out (1 if no factor found).
   If back-tracking was used, returns 1, otherwise returns 0. */

int 
ecm (modint_t f, const modulus_t m, const ecm_plan_t *plan)
{
  residue_t u, b, d, a;
  ellM_point_t P, Pt;

  ellE_point_t Q;

  /* P is initialized here because the coordinates of P may be used as temporary
     variables when constructing curves from sigma! (mouaif) */
  ellM_init (P, m); 

  unsigned int i;
  int bt = 0;

  mod_init (u, m);
  mod_init (b, m);

  mod_intset_ul (f, 1UL);

  if (plan->parameterization == BRENT12)
  {
    residue_t s, A;
    mod_init_noset0 (s, m);
    mod_init_noset0 (A, m);
    mod_set_ul (s, plan->sigma, m);
    if (Brent12_curve_from_sigma (A, P->x, s, m) == 0)
      {
	mod_gcd (f, P->x, m);
	mod_clear (u, m);
	mod_clear (A, m);
	mod_clear (b, m);
	ellM_clear (P, m);
	mod_clear (s, m);
	return 0;
      }
    mod_clear (s, m);
    mod_set1 (P->z, m);
    
    mod_div2 (A, A, m);
    mod_set1 (b, m);
    mod_add (b, b, A, m);
    mod_div2 (b, b, m); /* b = (A + 2)/4 */
    mod_clear (A, m);
  }
  else if (plan->parameterization == MONTY12)
  {
    residue_t A;
    mod_init_noset0 (A, m);
    if (Montgomery12_curve_from_k (A, P->x, plan->sigma, m) == 0)
      {
	mod_gcd (f, P->x, m);
	mod_clear (u, m);
	mod_clear (A, m);
	mod_clear (b, m);
	ellM_clear (P, m);
	return 0;
      }
    mod_set1 (P->z, m);
    
    mod_div2 (A, A, m);
    mod_set1 (b, m);
    mod_add (b, b, A, m);
    mod_div2 (b, b, m); /* b = (A + 2)/4 */
    mod_clear (A, m);
  }
  else if (plan->parameterization == MONTY16)
    {
      if (Montgomery16_curve_from_k (b, P->x, plan->sigma, m) == 0)
	{
	  mod_gcd (f, P->x, m);
	  mod_clear (u, m);
	  mod_clear (b, m);
	  ellM_clear (P, m);
	  return 0;
	}
      mod_set1 (P->z, m);
  }
  else if (plan->parameterization == TWED16)
  {
    if (Twisted_Edwards16_curve_from_sigma (d, Q, plan->sigma, m) == 0)
      {
	// TODO: check if factor found!
	return 0;
      }
  }
  else
  {
    fprintf (stderr, "ecm: Unknown parameterization\n");
    abort();
  }


#ifdef TRACE
  if (plan->parameterization & FULLMONTY)
    {
      printf ("starting point: (%lu::%lu) on curve y^2 = x^3 + (%lu*4-2)*x^2 + x\n", 
	      mod_get_ul (P->x, m), mod_get_ul (P->z, m), mod_get_ul(b, m));
    }
  else if (plan->parameterization == TWED16)
    {
      printf ("starting point: (%lu:%lu:%lu) on twisted Edwards curve\
               -x^2 + y^2 = 1 + %lu * x^2 * y^2\n", 
	      mod_get_ul (Q->x, m), mod_get_ul (Q->y, m), mod_get_ul (Q->z, m), 
	      mod_get_ul(d, m));
    }
#endif

  /* now start ecm */

  /* Do stage 1 */
  if (plan->parameterization & FULLMONTY)
  {
    ellM_interpret_bytecode (P, plan->bc, plan->bc_len, m, b);
    
      /* Add prime 2 in the desired power. If a zero residue for the 
	 Z-coordinate is encountered, we backtrack to previous point and stop.
	 NOTE: This is not as effective as I hoped. It prevents trivial 
	 factorizations only if after processing the odd part of the stage 1 
	 multiplier, the resulting point has power-of-2 order on E_p for all p|N.
	 If that were to happen, the point probably had that (presumably small 
	 on most E_p) power-of-2 order during the last couple of primes processed 
	 in the precomputed Lucas chain, and then quite likely the Lucas chain 
	 incorrectly used an addition of identical points, causing the 
	 Z-coordinate to become zero, leading to 0 (mod N) before we even 
	 get here. 
	 For example, using 10^6 composites from an RSA155 sieving experiment,
	 without backtracking we get N as the factor 456 times, with backtracking
	 still 360 times. 
	 TODO: this could probably be fixed by treating 3 separately, too, 
	 instead of putting it in the precomputed Lucas chain. Then the 
	 probability that a point of very small order on all E_p is encountered
	 during the Lucas chain is reduced, and so the probability of using
	 curve addition erroneously. */
    ellM_init (Pt, m);
    ellM_set (Pt, P, m);
    for (i = 0; i < plan->exp2; i++)
      {
	ellM_double (P, P, m, b);
#if ECM_BACKTRACKING
	if (mod_is0 (P[0].z, m))
	  {
	    ellM_set (P, Pt, m);
	    bt = 1;
	    break;
	  }
	ellM_set (Pt, P, m);
#endif
      }
    mod_gcd (f, P[0].z, m);
  }
  else if (plan->parameterization == TWED16)
    {
      
      /* a = -1 */
      mod_init (a, m);
      mod_set1 (a, m);
      mod_neg (a, a, m);
      
      ellE_interpret_bytecode (Q, plan->bc, plan->bc_len, m, a);
      
      ellE_point_t Qt;
      ellE_init (Qt, m);
      ellE_set (Qt, Q, m);
      for (i = 0; i < plan->exp2; i++)
	{
	  ellE_double (Q, Q, m, b);
#if ECM_BACKTRACKING
	  if (mod_is0 (Q[0].x, m))
	    {
	      ellE_set (Q, Qt, m);
	      bt = 1;
	      break;
	    }
	  ellE_set (Qt, Q, m);
#endif
	}
      mod_gcd (f, Q[0].x, m);

      ellE_clear (Qt, m);
    }
  
#if 0
  printf ("After stage 1, P = (%lu: :%lu), bt = %d, i = %d, exp2 = %d\n", 
	  mod_get_ul (P->x, m), mod_get_ul (P->z, m), bt, i, plan->exp2);
#endif
  
  
  if (bt == 0 && mod_intcmp_ul(f, 1UL) == 0 && plan->B1 < plan->stage2.B2)
    {
      bt = ecm_stage2 (u, P, &(plan->stage2), b, m);
      mod_gcd (f, u, m);
    }
  
  mod_clear (u, m);
  mod_clear (b, m);
  ellM_clear (P, m);
  ellM_clear (Pt, m);
  
  if (plan->parameterization & FULLTWED)
  {
    mod_clear (a, m);
    ellE_clear (Q, m);
  }

  return bt;
}


/* -------------------------------------------------------------------------- */

/* Determine order of a point P on a curve, both defined by the sigma value
   as in ECM. 
   If the group order is known to be == r (mod m), this can be supplied in 
   the variables "known_r" and" known_m".
   Looks for i in Hasse interval so that i*P = O, has complexity O(m^(1/4)). */

unsigned long
ell_pointorder (const residue_t sigma, const int parameterization, 
		const unsigned long known_m, const unsigned long known_r,
		const modulus_t m, const int verbose)
{
  ellW_point_t P, Pi, Pg;
  ellE_point_t Q;
  residue_t A, x, a, d;
  unsigned long min, max, i, j, order, cof, p;
  unsigned long giant_step, giant_min, baby_len;
  modint_t tm;
  ellW_point_t *baby;

  ASSERT (known_r < known_m);

  mod_intinit (tm);
  mod_getmod_int (tm, m);
  mod_init (A, m);
  mod_init (x, m);
  mod_init (a, m);
  mod_init (d, m);
  ellW_init (P, m);
  ellW_init (Pi, m);
  ellW_init (Pg, m);

  if (parameterization == BRENT12)
    {
      if (Brent12_curve_from_sigma (A, x, sigma, m) == 0)
	return 0;
    }
  else if (parameterization == MONTY12)
  {
    if (Montgomery12_curve_from_k (A, x, mod_get_ul (sigma, m), m) == 0)
      return 0;
  }
  else if (parameterization == MONTY16)
  {
    if (Montgomery16_curve_from_k (A, x, mod_get_ul (sigma, m), m) == 0)
      return 0;
    //    printf ("Montg16 curve built\n");
  }
  else if (parameterization == TWED16)
    {
      if (Twisted_Edwards16_curve_from_sigma (d, Q, mod_get_ul(sigma, m), m) == 0)
	return 0;

      //      printf ("Twed16 curve built\n");

      // Montgomery16_curve_from_Edwards16...
      // ax^2 + y^2 = 1 + dx^2y^2 ---> By^2 = x^3 + Ax^2 + x
      // A = 2(a+d)/(a-d), B = 4/(a-d)
      // u = (1+y)/(1-y)
      // v = (1+y)/(1-y)x
      // source: http://math.stackexchange.com/questions/1391732/birational-equvalence-of-twisted-edwards-and-montgomery-curves?noredirect=1&lq=1

      //      A = 2(a+d)/(a-d) = 2(-1+d)/(-1-d)

      mod_set1 (A, m);        // A = 1
      mod_neg (A, A, m);         // A = -1
      mod_set (x, A, m);      // x = -1
      mod_add (x, x, d, m);   // x = (-1+d)
      mod_add (x, x, x, m);   // x = 2(-1+d)
      mod_sub (A, A, d, m);   // A = -1-d
      mod_inv (A, A, m);      // A = 1/(-1-d)
      mod_mul (A, A, x, m);   // A = 2(-1+d)/(-1-d)

      //      x = (1+Q->y)/(1-Q->y)
      //      we don't need y

      mod_set1 (a, m);           // a = 1
      mod_add (a, a, Q->y, m);   // a = (1+Q->y)
      mod_set1 (x, m);           // x = 1
      mod_sub (x, x, Q->y, m);   // x = (1-Q->y)
      mod_inv (x, x, m);         // x = 1/(1-Q->y)
      mod_mul (x, x, a, m);      // x = (1+Q->y)/(1-Q->y)

      //      printf ("Twed16 curve and point converted to Montg16\n");
      
    }
  else
  {
    fprintf (stderr, "ecm: Unknown parameterization\n");
    abort();
  }
  
  if (verbose >= 2)
    {
      modint_t tA, tx;
      mod_intinit (tA);
      mod_intinit (tx);
      mod_get_int (tA, A, m);
      mod_get_int (tx, x, m);
      /* FIXME need multiple precision print */
      printf ("Curve parameters: A = %lu, x = %ld (mod %ld)\n", 
              mod_intget_ul(tA), mod_intget_ul(tx), mod_intget_ul(tm)); 
      mod_intclear (tA);
      mod_intclear (tx);
    }

  if (curveW_from_Montgomery (a, P, x, A, m) == 0)
    return 0UL;
  
  //  printf ("Montg16 curve and point converted to Weierstrass\n");

  if (verbose >= 2)
    {
      modint_t tx1, ty1, ta;
      mod_intinit (tx1);
      mod_intinit (ty1);
      mod_intinit (ta);
      mod_get_int (tx1, P[0].x, m);
      mod_get_int (ty1, P[0].y, m);
      mod_get_int (ta, a, m);
      /* FIXME need multiple precision print */
      printf ("Finding order of point (%ld, %ld) on curve "
              "y^2 = x^3 + %ld * x + b (mod %ld)\n", 
              mod_intget_ul(tx1), mod_intget_ul(ty1), mod_intget_ul(ta), 
              mod_intget_ul(tm));
      mod_intclear (tx1);
      mod_intclear (ty1);
      mod_intclear (ta);
    }
  
  /* FIXME deal with multiple precision modulus */
  i = (unsigned long) (2. * sqrt((double) mod_intget_ul(tm)));
  min = mod_intget_ul(tm) - i;
  max = mod_intget_ul(tm) + i;

  /* Giant steps visit values == r (mod m), baby steps values == 0 (mod m) */
  giant_step = ceil(sqrt(2.*(double)i / (double) known_m));
  /* Round up to multiple of m */
  giant_step = ((giant_step - 1) / known_m + 1) * known_m;
  
  /* We test Pi +- Pj, where Pi = (giant_min + i*giant_step), i >= 0,
     and Pj = j*P, 0 <= j <= giant_step / 2. 
     To ensure we can find all values >= min, ensure 
     giant_min <= min + giant_step / 2. 
     We also want giant_min == r (mod m) */
  giant_min = ((min + giant_step / 2) / known_m) * known_m + known_r;
  if (giant_min > min + giant_step / 2)
    giant_min -= known_m;
  if (verbose >= 2)
    printf ("known_m = %lu, known_r = %lu, giant_step = %lu, "
            "giant_min = %lu\n", known_m, known_r, giant_step, giant_min);
  
  baby_len = giant_step / known_m / 2 + 1;
  baby = (ellW_point_t *) malloc (baby_len * sizeof (ellW_point_t));
  for (i = 0; i < baby_len; i++)
    ellW_init (baby[i], m);
  
  ellW_set (Pg, P, m);
  i = known_m;
  if (ellW_mul_ui (Pg, i, a, m) == 0) /* Pg = m*P for now */
    goto found_inf;
  
  if (1 < baby_len)
    ellW_set (baby[1], Pg, m);
  
  if (2 < baby_len)
    {
      if (ellW_double (Pi, Pg, a, m) == 0)
        {
          i = 2 * known_m;
          goto found_inf;
        }
      ellW_set (baby[2], Pi, m);
    }

  for (i = 3; i < baby_len; i++)
    {
      if (ellW_add (Pi, Pi, Pg, a, m) == 0)
        {
          i *= known_m;
          goto found_inf;
        }
      ellW_set (baby[i], Pi, m);
    }

  /* Now compute the giant steps in [giant_min, giant_max] */
  i = giant_step;
  ellW_set (Pg, P, m);
  if (ellW_mul_ui (Pg, i, a, m) == 0)
    goto found_inf;

  i = giant_min;
  ellW_set (Pi, P, m);
  if (ellW_mul_ui (Pi, i, a, m) == 0)
    goto found_inf;
  
  while (i <= max + giant_step - 1)
    {
      /* Compare x-coordinate with stored baby steps. This makes it
         O(sqrt(p)) complexity, strictly speaking. */
      for (j = 1; j < baby_len; j++)
        if (mod_equal (Pi[0].x, baby[j]->x, m))
          {
            if (mod_equal (Pi[0].y, baby[j]->y, m))
              i -= j * known_m; /* Equal, so iP = jP and (i-j)P = 0 */
            else 
              {
                mod_neg (Pi[0].y, Pi[0].y, m);
                if (mod_equal (Pi[0].y, baby[j]->y, m))
                  i += j * known_m; /* Negatives, so iP = -jP and (i+j)P = 0 */
                else
                  {
                    fprintf (stderr, "Matching x-coordinates, but y neither "
                             "equal nor negatives\n");
                    abort();
                  }
              }
            goto found_inf;
          }

      i += giant_step;
      if (!ellW_add (Pi, Pi, Pg, a, m))
        goto found_inf;
    }
  
  if (i > max)
  {
      fprintf (stderr, "ell_order: Error, no match found for p = %lu, "
               "min = %lu, max = %lu, giant_step = %lu, giant_min = %lu\n", 
               mod_intget_ul(tm), min, max, giant_step, giant_min);
      abort ();
  }

found_inf:
  /* Check that i is a multiple of the order */
  ellW_set (Pi, P, m);
  if (ellW_mul_ui (Pi, i, a, m) != 0)
    {
      modint_t tx1, ty1;
      mod_intinit (tx1); 
      mod_intinit (ty1); 
      mod_get_int (tx1, P[0].x, m);
      mod_get_int (ty1, P[0].y, m);
#ifndef MODMPZ_MAXBITS
      fprintf (stderr, "ell_order: Error, %ld*(%ld, %ld) (mod %ld) is "
               "not the point at infinity\n", 
               i, tx1[0], ty1[0], tm[0]);
#endif
      mod_intclear (tx1); 
      mod_intclear (ty1); 
      return 0UL;
    }
  
  /* Ok, now we have some i so that ord(P) | i. Find ord(P).
     We know that ord(P) > 1 since P is not at infinity */

  /* For each prime factor of the order, reduce the exponent of 
     that prime factor as far as possible */

  cof = order = i;
  for (p = 2; p * p <= cof; p += 1 + p%2)
    if (cof % p == 0)
      {
        ASSERT (order % p == 0);
        /* Remove all factors of p */
        for (order /= p, cof /= p; order % p == 0; order /= p)
          {
            ASSERT(cof % p == 0);
            cof /= p;
          }
        ASSERT (cof % p != 0);

        /* Add factors of p again one by one, stopping when we hit 
           point at infinity */
        ellW_set (Pi, P, m);
        if (ellW_mul_ui (Pi, order, a, m) != 0)
          {
            order *= p;
            while (ellW_mul_ui (Pi, p, a, m) != 0)
              order *= p;
          }
      }
  /* Now cof is 1 or a prime */
  if (cof > 1)
    {
      ellW_set (Pi, P, m);
      ASSERT (order % cof == 0);
      if (ellW_mul_ui (Pi, order / cof, a, m) == 0)
        order /= cof;
    }


  /* One last check that order divides real order */
  ellW_set (Pi, P, m);
  if (ellW_mul_ui (Pi, order, a, m) != 0)
    {
      modint_t tx1, ty1;
      mod_intinit (tx1); 
      mod_intinit (ty1); 
      mod_get_int (tx1, P[0].x, m);
      mod_get_int (ty1, P[0].y, m);
      fprintf (stderr, "ell_order: Error, final order %ld is wrong\n", 
               order);
      mod_intclear (tx1); 
      mod_intclear (ty1); 
      abort ();
    }
  
  for (i = 0; i < giant_step; i++)
    ellW_clear (baby[i], m);
  free (baby);
  baby = NULL;
  mod_clear (A, m);
  mod_clear (x, m);
  mod_clear (a, m);
  mod_clear (d, m);
  mod_intclear (tm);
  ellW_clear (P, m);
  ellW_clear (Pi, m);
  ellW_clear (Pg, m);
  ellE_clear (Q, m);

  return order;
}


/* Count points on curve using the Jacobi symbol. This has complexity O(m). */

unsigned long 
ellM_curveorder_jacobi (residue_t A, residue_t x, modulus_t m)
{
  residue_t t, one;
  unsigned long order, i;
  modint_t tm;
  int bchar;

  mod_init_noset0 (t, m);
  mod_init_noset0 (one, m);
  mod_set1 (one, m);

  /* Compute x^3 + A*x^2 + x and see if it is a square */
  mod_set (t, x, m);
  mod_add (t, t, A, m);
  mod_mul (t, t, x, m);
  mod_add (t, t, one, m);
  mod_mul (t, t, x, m);
  bchar = mod_jacobi (t, m);
  ASSERT (bchar != 0);

  order = 2; /* One for (0, 0, 1), one for the point at infinity */
  /* FIXME deal with multi-word modulus */
  mod_getmod_int (tm, m);
  for (i = 1; mod_intcmp_ul (tm, i) > 0; i++)
    {
      mod_set_ul (x, i, m);
      mod_set (t, x, m);
      mod_add (t, t, A, m);
      mod_mul (t, t, x, m);
      mod_add (t, t, one, m);
      mod_mul (t, t, x, m);
      if (bchar == 1) 
	order = order + (unsigned long) (1L + (long) mod_jacobi (t, m));
      else
	order = order + (unsigned long) (1L - (long) mod_jacobi (t, m));
	/* Brackets put like this to avoid signedness warning */
    }

  mod_clear (one, m);
  mod_clear (t, m);
  
  return order;
}

unsigned long 
ell_curveorder (const unsigned long sigma_par, int parameterization, 
		const unsigned long m_par)
{
  residue_t sigma, A, X;
  modint_t lm;
  modulus_t m;
  unsigned long order;

  mod_intset_ul (lm, m_par);
  mod_initmod_int (m, lm);
  mod_set_ul (sigma, sigma_par, m);

  if (parameterization == BRENT12)
  {
    if (Brent12_curve_from_sigma (A, X, sigma, m) == 0)
      return 0UL;
  }
  else if (parameterization == MONTY12)
  {
    if (Montgomery12_curve_from_k (A, X, sigma_par, m) == 0)
      return 0UL;
  }
  else
  {
    fprintf (stderr, "ell_curveorder: Unknown parameterization\n");
    abort();
  }
  order = ellM_curveorder_jacobi (A, X, m);

  return order;
}

