#ifndef GENERATE_FACTORING_METHOD
#define  GENERATE_FACTORING_METHOD

#include <gmp.h>
#include "facul.h"
#include "tab_fm.h"
#include "tab_point.h"

/************************************************************************/
/*                      To generate numbers n=pq*/
/************************************************************************/

double *distribution_prime_number(int min, int max);

void generate_prime_factor(mpz_t res, gmp_randstate_t state, int lenFact);

void
generate_composite_integer(mpz_t res, gmp_randstate_t state,
			   int lenFact1, int lenFactall);

int
select_random_index_according_dist(double *dist, int len);

int
generate_composite_integer_interval(mpz_t res, gmp_randstate_t state,
				    double *dist, int lenFact1_min,
				    int lenFact1_max, int lenFactall);

/************************************************************************/
/*                To model our factoring methods                        */
/************************************************************************/

facul_strategy_t *generate_fm(int method, int curve, unsigned long B1,
			      unsigned long B2);

/************************************************************************/
/*                      Create Factoring Methods */
/************************************************************************/

int *choice_parameters(int method, int len_p_min);

tabular_fm_t *bench_proba_time_pset(int method, int curve,
				    gmp_randstate_t state,
				    int len_p_min, int len_p_max,
				    int len_n, int *param_region);

tabular_fm_t *generate_factoring_methods(gmp_randstate_t state, int len_p_min,
					 int len_p_max, int len_n, int opt_ch,
					 int *param_sieve);

tabular_fm_t *generate_factoring_methods_mc(gmp_randstate_t state,
					    int len_p_min, int len_p_max,
					    int len_n, int method, int curve,
					    int opt_ch, int *param_sieve);

/************************************************************************/
/*                      ANALYSE AND FILTER */
/************************************************************************/

double bench_proba_fm(facul_strategy_t * strategy, gmp_randstate_t state,
 		      unsigned long len_p, unsigned long len_n, mpz_t* N,
		      int nb_test_max);

void bench_proba(gmp_randstate_t state, tabular_fm_t * fm, int len_p_min,
        int p_max, int nb_test_max);


double bench_time_fm_onelength(facul_strategy_t * method, mpz_t* N,
			       int nb_test);

void bench_time(gmp_randstate_t state, tabular_fm_t * fm, int nb_test);


tabular_fm_t *filtering(tabular_fm_t * fm, int final_nb_methods);

/************************************************************************/
/*                      CONVEX_HULL_FM                                  */
/************************************************************************/

tabular_point_t *convert_tab_point_to_tab_fm(tabular_fm_t * t);

tabular_fm_t *convert_tab_fm_to_tab_point(tabular_point_t * t,
					  tabular_fm_t * init);

tabular_fm_t *convex_hull_fm(tabular_fm_t * t);

/************************************************************************/
/*               COMPUTE THE CONVEX_HULL FROM A FILE OF FM              */
/************************************************************************/

tabular_fm_t *convex_hull_from_file(FILE * file_in, FILE * file_out);

#endif				/* GENERATE_FACTORING_METHOD */
