/**
 * @file ropt_str.c
 * Basic structs used in ropt.
 */


#include "cado.h"
#include "ropt_str.h"
#include "portability.h"
#include "area.h"
#include "size_optimization.h"


/* -----------------*/
/* Static Functions */
/* -----------------*/


/**
 * Find bound V for constant rotation.
 */
static inline double
rotate_bounds_V_mpz ( mpz_t *f,
                      mpz_t *g,
                      int d,
                      ropt_bound_t bound )
{
  mpz_t V;
  mpz_poly F0, G0, F, G;
  double lognorm, exp_E = DBL_MAX, min_exp_E = DBL_MAX;
  mpz_poly_init (F0, d);
  mpz_poly_init (F, d);
  mpz_poly_init (G0, 1);
  mpz_poly_init (G, 1);
  mpz_poly_setcoeffs (F0, f, d);
  mpz_poly_setcoeffs (G0, g, 1);

  mpz_poly_ptr fptr, gptr;
  cado_poly poly;
  cado_poly_init (poly);
  fptr = poly->pols[ALG_SIDE];
  fptr->deg = d;
  gptr = poly->pols[RAT_SIDE];
  gptr->deg = 1;
  for (int i = 0; i < (d+1); i++)
    mpz_set(poly->pols[ALG_SIDE]->coeff[i], f[i]);
  for (int i = 0; i < 2; i++)
    mpz_set(poly->pols[RAT_SIDE]->coeff[i], g[i]);

  
  /* look for positive V: 2, 4, 8, ... */
  mpz_init_set_ui (V, 1);
  for (unsigned int i = 0; i < 150; i++, mpz_mul_2exp (V, V, 1) )
  {
    /* F = F0 + V*G0 */
    mpz_poly_rotation (F, F0, G0, V, 0);

    /* translation-optimize the rotated polynomial and compute exp_E */
    sopt_local_descent (F, G, F, G0, 1, -1, SOPT_DEFAULT_MAX_STEPS, 0); 
    lognorm = L2_skew_lognorm (F, SKEWNESS_DEFAULT_PREC);
    exp_E = lognorm + expected_rotation_gain (F, G);
    min_exp_E = fmin(min_exp_E, exp_E);
#if 0
    gmp_printf (" i: %u, translation: %Zd --", i, V);
    printf (" lognorm: %f, exp_apha: %f, exp_E: %f\n", lognorm,
            expected_rotation_gain (F, G),  exp_E);
#endif
    if (exp_E > bound->bound_E) break;
    if (lognorm > bound->bound_lognorm) break;

  }
  mpz_set (bound->global_v_boundr, V);

  /* look for negative V: -2, -4, -8, ... */
  mpz_set_si (V, -1);
  for (unsigned int i = 0; i < 150; i++, mpz_mul_2exp (V, V, 1))
  {
    /* F = F0 + V*G0 */
    mpz_poly_rotation (F, F0, G0, V, 0);

    /* translation-optimize the rotated polynomial */
    sopt_local_descent (F, G, F, G0, 1, -1, SOPT_DEFAULT_MAX_STEPS, 0); 
    lognorm = L2_skew_lognorm (F, SKEWNESS_DEFAULT_PREC);
    exp_E = lognorm + expected_rotation_gain (F, G);
    min_exp_E = fmin(min_exp_E, exp_E);
#if 0
    gmp_printf (" i: %u, translation: %Zd --", i, V);
    printf (" lognorm: %f, exp_apha: %f, exp_E: %f\n", lognorm,
            expected_rotation_gain (F, G),  exp_E);
#endif
    if (exp_E > bound->bound_E) break;
    if (lognorm > bound->bound_lognorm) break;
  }
  mpz_set (bound->global_v_boundl, V);
  
  cado_poly_clear (poly);
  mpz_poly_clear (F0);
  mpz_poly_clear (F);
  mpz_poly_clear (G0);
  mpz_poly_clear (G);
  mpz_clear (V);
  return min_exp_E;
}


/**
 * Find bound U for linear rotation.
 */
static inline double
rotate_bounds_U_lu ( mpz_t *f,
                     mpz_t *g,
                     int d,
                     ropt_bound_t bound )
{
  mpz_poly F0, G0, F, G;
  double lognorm, exp_E = DBL_MAX, min_exp_E = DBL_MAX;
  mpz_poly_init (F0, d);
  mpz_poly_init (F, d);
  mpz_poly_init (G0, 1);
  mpz_poly_init (G, 1);
  mpz_poly_setcoeffs (F0, f, d);
  mpz_poly_setcoeffs (G0, g, 1);

  /* Look for positive u: 1, 2, 4, 8, ... */
  int64_t u = 1;
  for (unsigned int i = 0; i < 62; i++, u *= 2)
  {
    /* F = F0 + u*x*G0 */
    mpz_poly_rotation_int64 (F, F0, G0, u, 1);

    /* translation-optimize the rotated polynomial */
    sopt_local_descent (F, G, F, G0, 1, -1, SOPT_DEFAULT_MAX_STEPS, 0); 
    lognorm = L2_skew_lognorm (F, SKEWNESS_DEFAULT_PREC);
    exp_E = lognorm + expected_rotation_gain (F, G);
    min_exp_E = fmin(min_exp_E, exp_E);    
#if 0
    printf (" i: %u, translation: %ld --", i, u);
    printf (" lognorm: %f, exp_apha: %f, exp_E: %f\n", lognorm,
            expected_rotation_gain (F, G),  exp_E);
#endif
    if (exp_E > bound->bound_E) break;
    if (lognorm > bound->bound_lognorm) break;
  }
  bound->global_u_boundr = u;

  /* Look for negative u: -1, -2, -4, -8, ... */
  u = -1;
  for (unsigned int i = 0; i < 62; i++, u *= 2)
  {
    /* F = F0 + u*x*G0 */
    mpz_poly_rotation_int64 (F, F0, G0, u, 1);


    /* translation-optimize the rotated polynomial */
    sopt_local_descent (F, G, F, G0, 1, -1, SOPT_DEFAULT_MAX_STEPS, 0); 
    lognorm = L2_skew_lognorm (F, SKEWNESS_DEFAULT_PREC);
    exp_E = lognorm + expected_rotation_gain (F, G);
    min_exp_E = fmin(min_exp_E, exp_E);    
#if 0
    printf (" i: %u, translation: %ld --", i, u);
    printf (" lognorm: %f, exp_apha: %f, exp_E: %f\n", lognorm,
            expected_rotation_gain (F, G),  exp_E);
#endif
    if (exp_E > bound->bound_E) break;
    if (lognorm > bound->bound_lognorm) break;
  }
  bound->global_u_boundl = u;

  mpz_poly_clear (F0);
  mpz_poly_clear (F);
  mpz_poly_clear (G0);
  mpz_poly_clear (G);
  return exp_E;
}


/**
 * Find bound W for quadratic rotation.
 */
static inline void
rotate_bounds_W_lu ( ropt_poly_t poly,
                     ropt_bound_t bound )
{
  mpz_poly F0, G0, F, G;

  mpz_poly_init (F0, poly->d);
  mpz_poly_init (F, poly->d);
  mpz_poly_init (G0, 1);
  mpz_poly_init (G, 1);
  mpz_poly_setcoeffs (F0, poly->f, poly->d);
  mpz_poly_setcoeffs (G0, poly->g, 1);

  /* look for positive w: 0, 1, 2, ... */
  int64_t w = 0;
  for (int64_t w = 0; w < 4096; w++)
  {
    /* F = F0 + w*x^2*G0 */
    mpz_poly_rotation_int64 (F, F0, G0, w, 2);

    /* translation-optimize the rotated polynomial */
    sopt_local_descent (F, G, F, G0, 1, -1, SOPT_DEFAULT_MAX_STEPS, 0);
    double lognorm = L2_skew_lognorm (F, SKEWNESS_DEFAULT_PREC);

    if (lognorm > bound->bound_lognorm)
      break;
  }
  bound->global_w_boundr = w;

  for (int64_t w = 0; w > -4096; w--)
  {
    /* F = F0 + w*x^2*G0 */
    mpz_poly_rotation_int64 (F, F0, G0, w, 2);

    /* translation-optimize the rotated polynomial */
    sopt_local_descent (F, G, F, G0, 1, -1, SOPT_DEFAULT_MAX_STEPS, 0);
    double lognorm = L2_skew_lognorm (F, SKEWNESS_DEFAULT_PREC);

    if (lognorm > bound->bound_lognorm)
      break;
  }
  bound->global_w_boundl = w;

  mpz_poly_clear (F0);
  mpz_poly_clear (F);
  mpz_poly_clear (G0);
  mpz_poly_clear (G);
}


/* -----------------*/
/* Public Functions */
/* -----------------*/


/**
 * Init ropt_poly_t.
 */
void
ropt_poly_init ( ropt_poly_t poly )
{
  unsigned int i;
  mpz_init (poly->n);
  mpz_init (poly->m);

  /* fx, gx holds pre-computed values f(r), g(r) where 0 <= r < p. */
  poly->f = (mpz_t*) malloc ((MAX_DEGREE + 1) * sizeof (mpz_t));
  poly->g = (mpz_t*) malloc ((MAX_DEGREE + 1) * sizeof (mpz_t));
  (poly->fx) = (mpz_t *) malloc ((primes[NP-1]+1) * sizeof (mpz_t));
  (poly->gx) = (mpz_t *) malloc ((primes[NP-1]+1) * sizeof (mpz_t));
  (poly->numerator) = (mpz_t *) malloc ((primes[NP-1]+1) * sizeof (mpz_t));

  if ( (poly->f == NULL) || (poly->g == NULL) ||
       ((poly->fx) == NULL) || ((poly->gx) == NULL) ||
       ((poly->numerator) == NULL) ) {
    fprintf (stderr, "Error, cannot allocate memory for polynomial"
             " coefficients in ropt_poly_init().\n");
    exit(1);
  }

  for (i = 0; i <= MAX_DEGREE; i++) {
    mpz_init (poly->f[i]);
    mpz_init (poly->g[i]);
  }

  for (i = 0; i <= primes[NP-1]; i++) {
    mpz_init (poly->fx[i]);
    mpz_init (poly->gx[i]);
    mpz_init (poly->numerator[i]);
  }
}


/**
 * clean coefficients
 */
void
ropt_poly_refresh ( ropt_poly_t poly )
{
  unsigned int i;
  mpz_set_ui (poly->n, 0);
  mpz_set_ui (poly->m, 0);
  for (i = 0; i <= MAX_DEGREE; i++) {
    mpz_set_ui (poly->f[i], 0);
    mpz_set_ui (poly->g[i], 0);
  }
  for (i = 0; i <= primes[NP-1]; i++) {
    mpz_set_ui (poly->fx[i], 0);
    mpz_set_ui (poly->gx[i], 0);
    mpz_set_ui (poly->numerator[i], 0);
  }
}


/**
 * Evaluation polynomials at many points.
 */
static inline void
ropt_poly_setup_eval (mpz_t *fr, mpz_t *gr, mpz_t *numerator, mpz_poly_srcptr f,
                      mpz_poly_srcptr g, const unsigned int *p)
{
  unsigned long i;
  mpz_t tmp;
  mpz_init (tmp);

  for ( i = 0; i <= p[NP-1]; i ++ ) {
    mpz_set_ui (fr[i], 0);
    mpz_set_ui (gr[i], 0);
    mpz_poly_eval_ui (fr[i], f, i);
    mpz_poly_eval_ui (gr[i], g, i);
    mpz_poly_eval_diff_ui (numerator[i], f, i);
    mpz_mul (numerator[i], numerator[i], gr[i]);
    mpz_neg (numerator[i], numerator[i]);
    mpz_mul (tmp, fr[i], g->coeff[1]);
    mpz_add (numerator[i], tmp, numerator[i]);
  }

  mpz_clear (tmp);
}


/**
 * check if g[0]/g[1] is a root of f (mod n)
 */
bool
ropt_poly_setup_check ( ropt_poly_t poly )
{
  int i;
  mpz_t t;
  mpz_init (t);

  /* degree */
  for ( (poly->d) = MAX_DEGREE;
        (poly->d) > 0 && mpz_cmp_ui ((poly->f[poly->d]), 0) == 0;
        poly->d -- );

  /* m = -Y0/Y1 mod n */
  mpz_invert (poly->m, poly->g[1], poly->n);
  mpz_neg (poly->m, poly->m);
  mpz_mul (poly->m, poly->m, poly->g[0]);
  mpz_mod (poly->m, poly->m, poly->n);

  /* check if m is a root of f mod n */
  mpz_set (t, poly->f[poly->d]);
  for (i = poly->d - 1; i >= 0; i --) {
    mpz_mul (t, t, poly->m);
    mpz_add (t, t, poly->f[i]);
    mpz_mod (t, t, poly->n);
  }

  if (mpz_cmp_ui (t, 0) != 0) {
    mpz_clear (t);
    return 0;
  }
  mpz_clear (t);
  return 1;
}


/**
 * Precompute fx, gx and numerator in ropt_poly_t. Note: poly->f,
 * poly->g, poly->d, poly->n must be set in prior.
 * This function can be called to reset poly after rotation.
 */
void
ropt_poly_setup ( ropt_poly_t poly )
{
  if (!ropt_poly_setup_check (poly)) {
    fprintf (stderr, "ERROR: The following polynomial have no common"
             " root. \n");
    print_cadopoly_fg (stderr, poly->f, poly->d, poly->g, 1, poly->n);
    exit (1);
  }

  mpz_poly F, G;
  F->coeff = poly->f;
  F->deg = poly->d;
  G->coeff = poly->g;
  G->deg = 1;

  /* pre-compute f(r) for all r < B */
  ropt_poly_setup_eval (poly->fx, poly->gx, poly->numerator,
                        F, G, primes);
  /* projective alpha */
  poly->alpha_proj = get_biased_alpha_projective (F, ALPHA_BOUND);
}


/**
 * Free ropt_poly_t.
 */
void
ropt_poly_free ( ropt_poly_t poly )
{
  unsigned int i;

  for (i = 0; i <= primes[NP-1]; i ++) {
    mpz_clear(poly->fx[i]);
    mpz_clear(poly->gx[i]);
    mpz_clear(poly->numerator[i]);
  }

  for (i = 0; i <= MAX_DEGREE; i++) {
    mpz_clear (poly->f[i]);
    mpz_clear (poly->g[i]);
  }

  mpz_clear (poly->n);
  mpz_clear (poly->m);

  free (poly->fx);
  free (poly->gx);
  free (poly->numerator);
  free (poly->f);
  free (poly->g);
}


/**
 * Init ropt_bound_t.
 */
void
ropt_bound_init ( ropt_bound_t bound )
{
  bound->global_w_boundl = 0;
  bound->global_w_boundr = 0;
  bound->global_u_boundl = 0;
  bound->global_u_boundr = 0;
  mpz_init_set_ui (bound->global_v_boundl, 0UL);
  mpz_init_set_ui (bound->global_v_boundr, 0UL);
  bound->init_lognorm = 0.0;
  bound->bound_lognorm = DBL_MAX;
  bound->bound_E = 0.0;
  bound->exp_min_alpha = 0.0;
}


/**
 * Subroutine for ropt_bound_setup().
 */
static inline void
ropt_bound_setup_normbound ( ropt_poly_t poly,
                             ropt_bound_t bound,
                             ropt_param_t param,
                             double incr )
{
  mpz_poly F, G;
  double skewness;
  F->coeff = poly->f;
  F->deg = poly->d;
  G->coeff = poly->g;
  G->deg = 1;
  skewness = L2_skewness (F, SKEWNESS_DEFAULT_PREC);
  bound->init_lognorm = L2_lognorm (F, skewness);
  bound->bound_E = (bound->init_lognorm + expected_rotation_gain (F, G))
    * incr;
  /* setup lognorm bound, either from input or by default
     By default, we do not use bound_lognorm to bound the 
     rotation; instead we use (lognorm + exp_E)          */
  if (param->bound_lognorm > 0) 
    bound->bound_lognorm = param->bound_lognorm;
}


#if 0
double
ropt_bound_expected_E (mpz_poly F, mpz_poly G)
{
  mpz_t *f = F->coeff;
  mpz_t *g = G->coeff;
  unsigned int d = F->deg;
  ropt_bound_t bound;
  double exp_E;

  ASSERT_ALWAYS(G->deg == 1);
  ropt_bound_init (bound);
  F->coeff = f;
  F->deg = d;
  bound->init_lognorm = L2_skew_lognorm (F, SKEWNESS_DEFAULT_PREC);
  bound->bound_lognorm = bound->init_lognorm * BOUND_LOGNORM_INCR_MAX;
  exp_E = rotate_bounds_U_lu (f, g, d, bound);
  exp_E = fmin(exp_E, rotate_bounds_V_mpz (f, g, d, bound));
  ropt_bound_free (bound);  
  return exp_E;
}
#endif


/**
 * Subroutine for ropt_bound_setup().
 */
static inline void
ropt_bound_setup_globalbound ( ropt_poly_t poly,
                               ropt_bound_t bound,
                               ropt_param_t param )
{
  /* w bound */
  if (poly->d == 6) {
    if (param->w_length > 0) {
      bound->global_w_boundl = param->w_left_bound;
      bound->global_w_boundr = param->w_left_bound + param->w_length - 1;
    }
    else
      rotate_bounds_W_lu (poly, bound);
  }
  else {
    bound->global_w_boundr = 0;
    bound->global_w_boundl = 0;
    param->w_left_bound = 0;
    param->w_length = 1;
  }

  /* u bound */
  rotate_bounds_U_lu (poly->f, poly->g, poly->d, bound);

  /* v bound */
  rotate_bounds_V_mpz (poly->f, poly->g, poly->d, bound);
}


/**
 * Subroutine for ropt_bound_setup().
 */
static inline void
ropt_bound_setup_others ( ropt_poly_t poly,
                          ropt_bound_t bound )
{
  mpz_poly F, G;
  mpz_poly_init (F, poly->d);
  mpz_poly_init (G, 1);
  mpz_poly_setcoeffs (F, poly->f, poly->d);
  mpz_poly_setcoeffs (G, poly->g, 1);
  bound->exp_min_alpha = expected_rotation_gain (F, G);
  mpz_poly_clear (F);
  mpz_poly_clear (G);
}


/**
 * Setup ropt_bound_t (independent of manually-input param).
 * Note, this function should be called in the very beginning
 * before doing any rotation, since the init_lognorm parameter
 * will be set to decide the rotation range in the rest.
 */
void
ropt_bound_setup ( ropt_poly_t poly,
                   ropt_bound_t bound,
                   ropt_param_t param,
                   double incr )
{
  /* set bound->bound_lognorm */
  ropt_bound_setup_normbound (poly, bound, param, incr);

  /* set w, u, v bounds */
  ropt_bound_setup_globalbound (poly, bound, param);

  /* set exp_min_alpha */
  ropt_bound_setup_others (poly, bound);

  if (param->verbose >= 1) {
    gmp_fprintf ( stderr, "# Info: global bounds (%d:%d, %ld:%ld, %Zd:%Zd)"
                  " gives:\n",
                  bound->global_w_boundl,
                  bound->global_w_boundr,
                  bound->global_u_boundl,
                  bound->global_u_boundr,
                  bound->global_v_boundl,
                  bound->global_v_boundr );
    if (bound->bound_lognorm==DBL_MAX) {
      gmp_fprintf ( stderr, "# Info: exp_alpha: %.3f, norm bound: DBL_MAX, "
                    "E bound: %.3f (init. norm: %.3f)\n",
                    bound->exp_min_alpha,
                    bound->bound_E,
                    bound->init_lognorm );
    }
    else {
      gmp_fprintf ( stderr, "# Info: exp_alpha: %.3f, norm bound: %.3f, "
                    "E bound: %.3f (init. norm: %.3f)\n",
                    bound->exp_min_alpha,
                    bound->bound_lognorm,
                    bound->bound_E,
                    bound->init_lognorm );
    }
  }
}


/**
 * This is for tuning function 
 */
void
ropt_bound_setup_incr ( ropt_poly_t poly,
                        ropt_bound_t bound,
                        ropt_param_t param,
                        double incr )
{
  ropt_bound_setup_normbound (poly, bound, param, incr);
  ropt_bound_setup_globalbound (poly, bound, param);
  ropt_bound_setup_others (poly, bound);
  if (param->verbose >= 2) {
    gmp_fprintf ( stderr, "# Info: tune (%d:%d, %ld:%ld, %Zd:%Zd)\n",
                  bound->global_w_boundl,
                  bound->global_w_boundr,
                  bound->global_u_boundl,
                  bound->global_u_boundr,
                  bound->global_v_boundl,
                  bound->global_v_boundr );
  }
}


/**
 * For existing rsparam and when rs is changed,
 */
void
ropt_bound_reset ( ropt_poly_t poly,
                   ropt_bound_t bound,
                   ropt_param_t param )
{

  /* u bound */
  rotate_bounds_U_lu (poly->f, poly->g, poly->d, bound);

  /* v bound */
  rotate_bounds_V_mpz (poly->f, poly->g, poly->d, bound);

  /* set exp_min_alpha */
  ropt_bound_setup_others (poly, bound);

  if (param->verbose >= 2) {
    gmp_fprintf ( stderr, "# Info: reset bounds (%d:%d, %ld:%ld, %Zd:%Zd)"
                  " gives:\n",
                  bound->global_w_boundl,
                  bound->global_w_boundr,
                  bound->global_u_boundl,
                  bound->global_u_boundr,
                  bound->global_v_boundl,
                  bound->global_v_boundr );
    gmp_fprintf ( stderr, "# Info: exp_alpha: %.3f, norm bound: %.3f, "
                  "E bound: %.3f (init norm: %.3f)\n",
                  bound->exp_min_alpha,
                  bound->bound_lognorm,
                  bound->bound_E,
                  bound->init_lognorm );
  }
}


/**
 * Free ropt_bound_t.
 */
void
ropt_bound_free ( ropt_bound_t bound )
{
  mpz_clear (bound->global_v_boundl);
  mpz_clear (bound->global_v_boundr);
}


/**
 * Init stage 1 parameters. The default customisation/parameters
 * happens in ropt_s1param_setup().
 */
void
ropt_s1param_init ( ropt_s1param_t s1param )
{
  int i;

  /* will be set either from param (stdin) or by default */
  s1param->len_e_sl = 0;
  s1param->tlen_e_sl = 0;
  s1param->nbest_sl = 0;
  s1param->nbest_sl_tune = 1;
  s1param->nbest_sieve = 0;
  s1param->modbound = 0;
  
  /* set to 1 for using quicker, smaller nbest_sl for tuning
     sublattices */
  s1param->nbest_sl_tunemode = 0;

  mpz_init_set_ui (s1param->modulus, 1UL);

  s1param->e_sl = (unsigned int*)
    malloc ( NUM_SUBLATTICE_PRIMES * sizeof (unsigned int) );

  s1param->individual_nbest_sl = (unsigned int*)
    malloc ( NUM_SUBLATTICE_PRIMES * sizeof (unsigned int) );

  if (s1param->e_sl == NULL || s1param->individual_nbest_sl == NULL) {
    fprintf (stderr, "Error, cannot allocate memory in "
             "ropt_s1param_init().\n");
    exit (1);
  }

  for (i = 0; i < NUM_SUBLATTICE_PRIMES; i ++) {
    s1param->e_sl[i] = 0;
    s1param->individual_nbest_sl[i] = 0;
  }
}


/**
 * Helper function to setup "e_sl[]".
 * Note the factor 16 in the following function seems
 * important. It has the effect to reduce the bounds
 * which is better in practice.
 */
static inline void
ropt_s1param_setup_e_sl ( ropt_poly_t poly,
                          ropt_s1param_t s1param,
                          ropt_bound_t bound,
                          ropt_param_t param )
{
  unsigned int i, j;
  unsigned long modbound;
  mpz_t bound_by_v;
  mpz_init (bound_by_v);

  /* check bound_by_u */
  unsigned long bound_by_u = (unsigned long)
    (bound->global_u_boundr < (-bound->global_u_boundl)) ?
    bound->global_u_boundr : (-bound->global_u_boundl);

  /* check bound_by_v */
  mpz_fdiv_q_ui (bound_by_v, bound->global_v_boundr,
                 SIZE_SIEVEARRAY_V_MIN);

  /* compare two bounds */
  if ( mpz_cmp_ui (bound_by_v, bound_by_u) < 0 ) {
    modbound = mpz_get_ui (bound_by_v);
    if (modbound > bound_by_u / 8)
      modbound = bound_by_u / 8;
  }
  else
    modbound = bound_by_u / 8;

  /* adjust for small skewness but large bound (not sure if this is good) */
  mpz_poly F;
  F->coeff = poly->f;
  F->deg = poly->d;
  double skew = L2_skewness (F, SKEWNESS_DEFAULT_PREC);
  if ((double) modbound > skew)
    modbound = (unsigned long) skew;

  /* find i */
  for (i = 0; i < NUM_DEFAULT_SUBLATTICE; i ++)
    if (default_sublattice_prod[i] > modbound)
      break;

  /* check if i > limit and fix if so */
  i = (i >= NUM_DEFAULT_SUBLATTICE) ?
    (NUM_DEFAULT_SUBLATTICE - 1) : i;

  /* set e_sl[] from default array */
  for (j = 0; j < NUM_SUBLATTICE_PRIMES; j++) {
    s1param->e_sl[j] = default_sublattice_pe[i][j];
  }

  /* get mod bound */
  s1param->modbound = default_sublattice_prod[i];
  
  /* overwrite e_sl[] from from stdin, if needed */
  if (param->s1_num_e_sl != 0) {
    s1param->modbound = 1;
    for (i = 0; i < NUM_SUBLATTICE_PRIMES; i++) {
      s1param->e_sl[i] = param->s1_e_sl[i];
      s1param->modbound *= param->s1_e_sl[i];
    }
  }
  
  mpz_clear (bound_by_v);
}


/**
 *  Function to setup "individual_nbest_sl[]".
 */
void
ropt_s1param_setup_individual_nbest_sl (ropt_s1param_t s1param)
{
  unsigned int i;
  for (i = 0; i < s1param->len_e_sl; i ++)
    s1param->individual_nbest_sl[i] =
        s1_size_each_sublattice[s1param->tlen_e_sl - 1][i];
}


/**
 *  Function to setup shorter "individual_nbest_sl[]".
 */
void
ropt_s1param_setup_individual_nbest_sl_tune (ropt_s1param_t s1param)
{
  unsigned int i;
  for (i = 0; i < s1param->len_e_sl; i ++)
    s1param->individual_nbest_sl[i]
        = s1_size_each_sublattice_tune[i];
}


/**
 * Setup s1param by default parameters and/or param (stdin).
 */
void
ropt_s1param_setup ( ropt_poly_t poly,
                     ropt_s1param_t s1param,
                     ropt_bound_t bound,
                     ropt_param_t param )
{
  unsigned int i, j;

  /* Set 1: "len_e_sl" and "tlen_e_sl" */
  s1param->len_e_sl = NUM_SUBLATTICE_PRIMES;
  s1param->tlen_e_sl = s1param->len_e_sl;

  /* Set 2: "nbest_sl", depending on the size of n */
  j = mpz_sizeinbase (poly->n, 10);
  for (i = 0; i < NUM_DEFAULT_DIGITS-1; i ++)
    if (size_total_sublattices[i][0] > j)
      break;

  s1param->nbest_sl = (unsigned int) ((double) size_total_sublattices[i][1]);

  s1param->nbest_sieve = (unsigned int) ((double) size_total_sublattices[i][2]);

  s1param->nbest_sl_tune = (unsigned int) ((double) size_total_sublattices[i][3]);
  

  if (s1param->nbest_sl < 4)
    s1param->nbest_sl = 4;
  if (param->verbose >= 1) {
    printf ("# Info: s1param->nbest_sl: %u (= size_total_sublattices*ropteffort)\n", s1param->nbest_sl);
    printf ("# Info: s1param->nbest_sieve: %u (for sieving)\n", s1param->nbest_sieve);
  }

  /* Set 3: set "e_sl[]" */
  ropt_s1param_setup_e_sl (poly, s1param, bound, param);

  /* Set 4: set "individual_nbest_sl[]" */
  ropt_s1param_setup_individual_nbest_sl (s1param);

}


/**
 * Setup s1param by default parameters and/or param (stdin).
 */
void
ropt_s1param_resetup ( ropt_poly_t poly,
                       ropt_s1param_t s1param,
                       ropt_bound_t bound,
                       ropt_param_t param,
                       unsigned int nbest )
{
  unsigned int i, j;
  s1param->len_e_sl = NUM_SUBLATTICE_PRIMES;
  s1param->tlen_e_sl = s1param->len_e_sl;
  j = mpz_sizeinbase (poly->n, 10);
  for (i = 0; i < NUM_DEFAULT_DIGITS; i ++)
    if (size_total_sublattices[i][0] > j)
      break;
  s1param->nbest_sl = (unsigned long) nbest;
  s1param->nbest_sieve = (unsigned long) ((double) size_total_sublattices[i][2]);
  if (s1param->nbest_sl < 4)
    s1param->nbest_sl = 4;

  /* Set 3: set "e_sl[]" */
  ropt_s1param_setup_e_sl (poly, s1param, bound, param);

  /* Set 4: set "individual_nbest_sl[]" */
  ropt_s1param_setup_individual_nbest_sl (s1param);

}


/**
 * Setup s1param with modbound as extra input
 */
void
ropt_s1param_resetup_modbound ( ropt_poly_t poly,
                                ropt_s1param_t s1param,
                                ropt_bound_t bound,
                                ropt_param_t param,
                                unsigned int nbest,
                                unsigned long modbound)
{
  ropt_s1param_resetup (poly, s1param, bound, param, nbest);

  /* find i */
  int i, j;
  for (i = 0; i < NUM_DEFAULT_SUBLATTICE; i ++)
    if (default_sublattice_prod[i] > modbound)
      break;

  /* check if i > limit and fix if so */
  i = (i >= NUM_DEFAULT_SUBLATTICE) ?
    (NUM_DEFAULT_SUBLATTICE - 1) : i;

  /* set e_sl[] from default array */
  for (j = 0; j < NUM_SUBLATTICE_PRIMES; j++) {
    s1param->e_sl[j] = default_sublattice_pe[i][j];
  }

  /* get mod bound */
  s1param->modbound = default_sublattice_prod[i];

}


/**
 * Free s1param.
 */
void
ropt_s1param_free ( ropt_s1param_t s1param )
{
  free(s1param->e_sl);
  free(s1param->individual_nbest_sl);
  mpz_clear (s1param->modulus);
}


/**
 * Init ropt_s2param.
 */
void
ropt_s2param_init ( ropt_poly_t poly,
                    ropt_s2param_t s2param )
{
  int i;
  s2param->len_p_rs = NP - 1;
  s2param->Amax = s2param->Amin = 0;
  s2param->Bmax = s2param->Bmin = 0;

  mpz_init_set_ui (s2param->Umax, 0UL);
  mpz_init_set_ui (s2param->Umin, 0UL);
  mpz_init_set_ui (s2param->Vmax, 0UL);
  mpz_init_set_ui (s2param->Vmin, 0UL);
  mpz_init_set_ui (s2param->A, 0UL);
  mpz_init_set_ui (s2param->B, 0UL);
  mpz_init_set_ui (s2param->MOD, 0UL);

  s2param->f = (mpz_t*) malloc ((poly->d + 1) * sizeof (mpz_t));
  s2param->g = (mpz_t*) malloc (2 * sizeof (mpz_t));
  if (s2param->f == NULL || s2param->g == NULL) {
    fprintf ( stderr, "Error, cannot allocate memory in "
              "ropt_s2param_init().\n" );
    exit (1);
  }

  for (i = 0; i <= poly->d; i++)
    mpz_init (s2param->f[i]);
  mpz_init (s2param->g[0]);
  mpz_init (s2param->g[1]);
}


/**
 * Free ropt_s2param.
 */
void
ropt_s2param_free ( ropt_poly_t poly,
                    ropt_s2param_t s2param )
{
  int i;

  mpz_clear (s2param->Umax);
  mpz_clear (s2param->Umin);
  mpz_clear (s2param->Vmax);
  mpz_clear (s2param->Vmin);
  mpz_clear (s2param->A);
  mpz_clear (s2param->B);
  mpz_clear (s2param->MOD);
  for (i = 0; i <= poly->d; i++)
    mpz_clear (s2param->f[i]);
  mpz_clear (s2param->g[0]);
  mpz_clear (s2param->g[1]);
  free (s2param->f);
  free (s2param->g);
}


/**
 * Setup sublattice (u, v), mod and Umax, Umin, Vmax, Vmin.
 */
static inline void
ropt_s2param_setup_sublattice ( ropt_s2param_t s2param,
                                mpz_t A,
                                mpz_t B,
                                mpz_t MOD )
{
  mpz_set (s2param->A, A);
  mpz_set (s2param->B, B);
  /* the s1param->modulus might be not true since rsparam might be
     changed for different quadratic rotations. Instead, the true mod
     is recorded in the priority queue, now in 'MOD'. */
  mpz_set (s2param->MOD, MOD);
  ab2uv (s2param->A, s2param->MOD, s2param->Amax, s2param->Umax);
  ab2uv (s2param->A, s2param->MOD, s2param->Amin, s2param->Umin);
  ab2uv (s2param->B, s2param->MOD, s2param->Bmax, s2param->Vmax);
  ab2uv (s2param->B, s2param->MOD, s2param->Bmin, s2param->Vmin);
}


/**
 * Set sieving region for s2param -> Amax, Amin, Amax, Amin.
 */
static inline void
ropt_s2param_setup_range ( ropt_bound_t bound,
                           ropt_s2param_t s2param,
                           ropt_param_t param,
                           mpz_t mod )
{
  /* read sieve length from param (stdin) */
  if (param->s2_Amax >= 0 && param->s2_Bmax > 0) {
    s2param->Amax = param->s2_Amax;
    s2param->Bmax = param->s2_Bmax;
  }
  /* or compute sieve V length */
  else {
    s2param->Amax = 0;
    unsigned long len;
    mpz_t q;
    mpz_init (q);
    mpz_fdiv_q (q, bound->global_v_boundr, mod);
    len =  mpz_get_ui (q);

    /* upper bound */
    s2param->Bmax = ( (len > SIZE_SIEVEARRAY_V_MAX) ?
                      SIZE_SIEVEARRAY_V_MAX : (long) len );

    /* fix if len is too small */
    if (s2param->Bmax == 0)
      s2param->Bmax = 128;

    mpz_clear (q);
  }

  s2param->Bmin = -s2param->Bmax;
  s2param->Amin = -s2param->Amax;
}


/**
 * Set up sieving length and sublattice for s2param.
 */
void
ropt_s2param_setup ( ropt_bound_t bound,
                     ropt_s1param_t s1param,
                     ropt_s2param_t s2param,
                     ropt_param_t param,
                     mpz_t A,
                     mpz_t B,
                     mpz_t MOD )
{
  /* normally we should have more primes than those in the sublattice */
  if (s2param->len_p_rs < s1param->len_e_sl) {
    fprintf ( stderr, "# Warning: number of primes considered "
              "in stage 2 is smaller than that in "
              "stage 1. See ropt_s2param_setup().\n" );
  }

  /* set sieving length */
  ropt_s2param_setup_range (bound, s2param, param, MOD);

  /* set sublattice */
  ropt_s2param_setup_sublattice (s2param, A, B, MOD);
}


/**
 * Set up s2param without s1param.
 */
void
ropt_s2param_setup_stage2_only ( ropt_bound_t bound,
                                 ropt_s2param_t s2param,
                                 ropt_param_t param,
                                 mpz_t A,
                                 mpz_t B,
                                 mpz_t MOD )
{
  s2param->len_p_rs = NP - 1;

  /* set sieving length */
  ropt_s2param_setup_range (bound, s2param, param, MOD);

  /* set sublattice */
  ropt_s2param_setup_sublattice (s2param, A, B, MOD);
}


/**
 * Set tune (short) sieving region for s2param -> Amax, Amin, Amax, Amin.
 */
static inline void
ropt_s2param_setup_tune_range  ( ropt_s2param_t s2param,
                                 unsigned long Amax,
                                 unsigned long Bmax )
{
  s2param->Amax = (long) Amax;
  s2param->Bmax = (long) Bmax;
  s2param->Bmin = -s2param->Bmax;
  s2param->Amin = -s2param->Amax;
}


/**
 * Set up tune sieving length and sublattice for s2param.
 */
void
ropt_s2param_setup_tune ( ropt_s1param_t s1param,
                          ropt_s2param_t s2param,
                          mpz_t A,
                          mpz_t B,
                          mpz_t MOD,
                          unsigned long Amax,
                          unsigned long Bmax,
                          unsigned int len_p_rs )
{
  /* setup s2param->len_p_rs */
  if (len_p_rs < s1param->tlen_e_sl) {
    fprintf ( stderr, "# Warning: number of primes considered "
              "in stage 2 (%d) is smaller than that (%d) in "
              "stage 1. See ropt_s2param_setup().\n",
              len_p_rs, s1param->tlen_e_sl );
  }
  s2param->len_p_rs = len_p_rs;

  /* set tune sieving length */
  ropt_s2param_setup_tune_range (s2param, Amax, Bmax);

  /* set sublattice */
  ropt_s2param_setup_sublattice (s2param, A, B, MOD);
}


/**
 * Print s2param.
 */
void
ropt_s2param_print ( ropt_s2param_t s2param )
{
  fprintf ( stderr, "# Info: sieving matrix: "
            "[%ld, %ld] x [%ld, %ld], len. prime = %d.\n",
            s2param->Amin, s2param->Amax,
            s2param->Bmin, s2param->Bmax,
            s2param->len_p_rs );

  gmp_fprintf ( stderr,
                "# Info: (u, v) = (%Zd + i * %Zd, %Zd + j * %Zd)\n",
                s2param->A, s2param->MOD, s2param->B,
                s2param->MOD );

  gmp_fprintf ( stderr,
                "# Info: (Amin: %4ld, Amax: %4ld) -> "
                "(Umin: %6Zd, Umax: %6Zd)\n",
                s2param->Amin, s2param->Amax,
                s2param->Umin, s2param->Umax );

  gmp_fprintf ( stderr,
                "# Info: (Bmin: %4ld, Bmax: %4ld) -> "
                "(Vmin: %6Zd, Vmax: %6Zd)\n",
                s2param->Bmin, s2param->Bmax,
                s2param->Vmin, s2param->Vmax );
}


/**
 * Init bestpoly.
 */
void
ropt_bestpoly_init ( ropt_bestpoly_t bestpoly,
                     int d )
{
  int i;
  bestpoly->f = (mpz_t*) malloc ((d + 1) * sizeof (mpz_t));
  bestpoly->g = (mpz_t*) malloc (2 * sizeof (mpz_t));

  if ((bestpoly->f == NULL) || (bestpoly->g == NULL)) {
    fprintf ( stderr, "Error, cannot allocate memory for"
              " ropt_bestpoly_init()\n" );
    exit (1);
  }

  for (i = 0; i <= d; i++)
    mpz_init (bestpoly->f[i]);
  mpz_init (bestpoly->g[0]);
  mpz_init (bestpoly->g[1]);
}


/**
 * Setup bestpoly.
 */
void
ropt_bestpoly_setup ( ropt_bestpoly_t bestpoly,
                      mpz_t *f,
                      mpz_t *g,
                      int d )
{
  int i;
  for (i = 0; i <= d; i++)
    mpz_set (bestpoly->f[i], f[i]);
  mpz_set (bestpoly->g[0], g[0]);
  mpz_set (bestpoly->g[1], g[1]);
}


/**
 * Free bestpoly.
 */
void
ropt_bestpoly_free ( ropt_bestpoly_t bestpoly,
                     int d )
{
  int i;
  for (i = 0; i <= d; i++)
    mpz_clear (bestpoly->f[i]);

  mpz_clear (bestpoly->g[0]);
  mpz_clear (bestpoly->g[1]);
  free (bestpoly->f);
  free (bestpoly->g);
}


/**
 * Init param.
 */
void
ropt_param_init ( ropt_param_t param )
{
  mpz_init (param->s2_u);
  mpz_init (param->s2_v);
  mpz_init (param->s2_mod);
  mpz_init (param->n);
  mpz_set_ui (param->s2_u, 0);
  mpz_set_ui (param->s2_v, 0);
  mpz_set_ui (param->s2_mod, 0);
  mpz_set_ui (param->n, 0);
  param->w_left_bound = 0;
  param->w_length = 0;
  param->s1_num_e_sl = 0;
  param->s2_Amax = 0;
  param->s2_Bmax = 0;
  param->s2_w = 0;
  param->bound_lognorm = -1;
  param->s1_e_sl = (unsigned short*)
    malloc ( NUM_SUBLATTICE_PRIMES * sizeof (unsigned short) );
  int i;
  for (i = 0; i < NUM_SUBLATTICE_PRIMES; i ++)
    param->s1_e_sl[i] = 0;
  param->d = 0;
  param->verbose = 0;
  param->effort = DEFAULT_ROPTEFFORT;
  param->skip_ropt = 0;
  param->gen_raw = 0;
  param->sopt = 0;
}


/**
 * Free param.
 */
void
ropt_param_free ( ropt_param_t param )
{
  mpz_clear (param->s2_u);
  mpz_clear (param->s2_v);
  mpz_clear (param->s2_mod);
  mpz_clear (param->n);
  free (param->s1_e_sl);
}


/**
 * Init info.
 */
void
ropt_info_init ( ropt_info_t info )
{
  info->ave_MurphyE = 0.0;
  info->best_MurphyE = 0.0;
  info->mode = 0;
  info->w = 0;
  info->ropt_time_stage1 = 0.0;
  info->ropt_time_tuning = 0.0;
  info->ropt_time_stage2 = 0.0;
}


/**
 * Free info.
 */
void
ropt_info_free ( ropt_info_t info )
{
  info->ave_MurphyE = 0.0;
  info->best_MurphyE = 0.0;
  info->mode = 0;
  info->w = 0;
}
