#include "cado.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <time.h>
#include <limits.h> /* for CHAR_BIT */
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>   /* for _O_BINARY */

#include "portability.h"
#include "macros.h"
#include "relation.h"
#include "params.h"
#include "cado_poly.h"
#include "gzip.h"
#include "las-duplicate.hpp"
#include "las-coordinates.hpp"
#include "las-norms.hpp"
#include "verbose.h"


static void *
dupsup (FILE *output, relation & rel, las_todo_entry const& doing, const int is_dupe)
{
  if (0)
    gmp_fprintf (output, "# sq = %Zd, rho = %Zd, side = %d\n",
            (mpz_srcptr) doing.p,
            (mpz_srcptr) doing.r, doing.side);
  rel.print(output, is_dupe ? "# DUPE " : "");
  return NULL;
}

/* If the line is a special-q comment, sets sq and rho and returns 1.
   Otherwise returns 0. */
static int
read_sq_comment(las_todo_entry & doing, const char *line)
{
  if (gmp_sscanf(line, "# Sieving side-%d q=%Zd; rho=%Zd;",
              &doing.side,
              (mpz_ptr) doing.p,
              (mpz_ptr) doing.r) == 3) {
    return 1;
  }
  return 0;
}

static void
read_poly(cado_poly_ptr cpoly, param_list_ptr pl)
{
    cado_poly_init(cpoly);
    const char *cpoly_filename;
    if ((cpoly_filename = param_list_lookup_string(pl, "poly")) == NULL) {
        fprintf(stderr, "Error: -poly is missing\n");
        param_list_print_usage(pl, NULL, stderr);
	cado_poly_clear(cpoly);
	param_list_clear(pl);
        exit(EXIT_FAILURE);
    }

    if (!cado_poly_read(cpoly, cpoly_filename)) {
	fprintf(stderr, "Error reading polynomial file %s\n", cpoly_filename);
	cado_poly_clear(cpoly);
	param_list_clear(pl);
	exit(EXIT_FAILURE);
    }
}

static int
parse_config(siever_config & sc, param_list_ptr pl)
{
    sc.side = 1; // Legacy default.
    param_list_parse_int(pl, "-sqside", &sc.side);
    int seen = 1;
    if (param_list_lookup_string(pl, "A")) {
        seen &= param_list_parse_int  (pl, "A",    &(sc.logA));
        if (param_list_lookup_string(pl, "I")) {
            fprintf(stderr, "# -A and -I are incompatible\n");
            exit(EXIT_FAILURE);
        }
    } else if (param_list_lookup_string(pl, "I")) {
        int I;
        seen &= param_list_parse_int  (pl, "I", &I);
        sc.logA = 2 * I - 1;
        printf("# Interpreting -I %d as meaning -A %d\n", I, sc.logA);
    }
    seen &= param_list_parse_ulong (pl, "lim0",  &(sc.sides[0].lim));
    seen &= param_list_parse_int   (pl, "lpb0",  &(sc.sides[0].lpb));
    seen &= param_list_parse_int   (pl, "mfb0",  &(sc.sides[0].mfb));
    seen &= param_list_parse_double(pl, "lambda0", &(sc.sides[0].lambda));
    seen &= param_list_parse_int   (pl, "ncurves0",  &(sc.sides[0].ncurves));
    seen &= param_list_parse_ulong (pl, "lim1",  &(sc.sides[1].lim));
    seen &= param_list_parse_int   (pl, "lpb1",  &(sc.sides[1].lpb));
    seen &= param_list_parse_int   (pl, "mfb1",  &(sc.sides[1].mfb));
    seen &= param_list_parse_double(pl, "lambda1", &(sc.sides[1].lambda));
    seen &= param_list_parse_int   (pl, "ncurves1",  &(sc.sides[1].ncurves));
    long dupqmin[2] = {0, 0};
    param_list_parse_long_and_long(pl, "dup-qmin", dupqmin, ",");
    sc.sides[0].qmin = dupqmin[0];
    sc.sides[1].qmin = dupqmin[1];

    /* Change 0 (not initialized) into LONG_MAX */
    for (int side = 0; side < 2; side ++)
      if (sc.sides[side].qmin == 0)
	sc.sides[side].qmin = LONG_MAX;

    /* If qmin is not given, use lim on the special-q side by default. */
    if (sc.sides[sc.side].qmin == LONG_MAX)
      sc.sides[sc.side].qmin = sc.sides[sc.side].lim;

    int logI = (sc.logA+1)/2;
    if (!param_list_parse_ulong(pl, "powlim0", &sc.sides[0].powlim))
        sc.sides[0].powlim = (1<<logI) - 1;
    if (!param_list_parse_ulong(pl, "powlim1", &sc.sides[1].powlim))
        sc.sides[1].powlim = (1<<logI) - 1;
    return seen;
}


static void declare_usage(param_list_ptr pl)
{
  param_list_decl_usage(pl, "path_antebuffer", "path to antebuffer program");
  param_list_decl_usage(pl, "poly", "polynomial file");
  param_list_decl_usage(pl, "I",    "set sieving region to 2^I times J");
  param_list_decl_usage(pl, "A",    "set sieving region to 2^A");
  param_list_decl_usage(pl, "skew", "skewness");
  param_list_decl_usage(pl, "lim0", "rational factor base bound");
  param_list_decl_usage(pl, "lim1", "algebraic factor base bound");
  param_list_decl_usage(pl, "lpb0", "set rational large prime bound to 2^lpb0");
  param_list_decl_usage(pl, "lpb1", "set algebraic large prime bound to 2^lpb1");
  param_list_decl_usage(pl, "mfb0", "set rational cofactor bound 2^mfb0");
  param_list_decl_usage(pl, "mfb1", "set algebraic cofactor bound 2^mfb1");
  param_list_decl_usage(pl, "ncurves0", "set rational number of curves");
  param_list_decl_usage(pl, "ncurves1", "set algebraic number of curves"); 
  param_list_decl_usage(pl, "lambda0", "rational lambda value");
  param_list_decl_usage(pl, "lambda1", "algebraic lambda value");
  param_list_decl_usage(pl, "powlim0", "limit on powers on rat side");
  param_list_decl_usage(pl, "powlim1", "limit on powers on alg side");
  param_list_decl_usage(pl, "dup-qmin", "limits of q-sieving for 2-sided duplicate removal");
  param_list_decl_usage(pl, "sqside", "side of special-q (default=1)");
  param_list_decl_usage(pl, "mt",   "number of threads to use");
  verbose_decl_usage(pl);
}

static void
usage (param_list_ptr pl, char *argv0)
{
    param_list_print_usage(pl, argv0, stderr);
    exit(EXIT_FAILURE);
}


int
main (int argc, char * argv[])
{
    char * argv0 = argv[0];
    siever_config conf;
    int nb_threads = 1;

    param_list pl;
    param_list_init(pl);
    declare_usage(pl);
    argv++,argc--;

#ifdef HAVE_MINGW
    _fmode = _O_BINARY;     /* Binary open for all files */
#endif

    if (argc == 0)
      usage (pl, argv0);

    for( ; argc ; ) {
        if (param_list_update_cmdline(pl, &argc, &argv)) { continue; }
        /* Since we accept file names freeform, we decide to never abort
         * on unrecognized options */
        break;
        // fprintf (stderr, "Unknown option: %s\n", argv[0]);
        // abort();
    }
    /* print command-line arguments */
    verbose_interpret_parameters(pl);
    param_list_print_command_line (stdout, pl);
    fflush(stdout);

    if (argc == 0) {
      fprintf(stderr, "Error, provide freeform file names\n");
      usage(pl, argv0);
    }

    cado_poly cpoly;
    read_poly(cpoly, pl);
    int ok = parse_config(conf, pl);
    ok = ok && param_list_parse_double(pl, "skew",    &(cpoly->skew));
    if (!ok) {
        fprintf(stderr, "Error: mandatory parameter missing.\n");
	param_list_clear(pl);
        exit(EXIT_FAILURE);
    }
    param_list_parse_int(pl, "mt", &nb_threads);

    if (param_list_warn_unused(pl))
    {
      fprintf(stderr, "Error, unused parameters are given\n");
      usage(pl, argv0);
    }

    tune_las_memset();

    std::shared_ptr<facul_strategies_t> strategies(facul_make_strategies(
            conf.sides[0].lim,
            conf.sides[0].lpb,
            conf.sides[0].mfb,
            conf.sides[1].lim,
            conf.sides[1].lpb,
            conf.sides[1].mfb,
            conf.sides[0].ncurves,
            conf.sides[1].ncurves,
            NULL, 0), facul_clear_strategies);

    mpz_t sq, rho;
    mpz_init(sq);
    mpz_init(rho);

    for (int argi = 0; argi < argc; argi++) {
      FILE *f = fopen_maybe_compressed(argv[argi], "rb");
      if (f == NULL) {
          perror(argv[argi]);
          abort();
      }
      sieve_info * psi = NULL;
      for (int row = 0 ; !feof(f) ; row++) {
        char line[1024];
        if (fgets(line, sizeof(line), f) == NULL)
          break;
        las_todo_entry doing;
        if (read_sq_comment(doing, line)) {
            /* TODO we need to fix that for dynamic I */
            if (psi) delete psi;
            uint32_t I = 1UL << ((conf.logA+1)/2);
            uint32_t J = 1UL << ((conf.logA-1)/2);
            int nb_threads = 1;
            psi = fill_in_sieve_info(doing, I, J, cpoly, conf, nb_threads);
            psi->strategies = strategies;
        } else {
            relation rel;
            if (rel.parse(line)) {
                int is_dupe = relation_is_duplicate(rel, nb_threads, *psi);
                dupsup(stdout, rel, doing, is_dupe);
            }
        }
      }
      fclose_maybe_compressed(f, argv[argi]);
      if (psi) delete psi;
    }
    
    cado_poly_clear(cpoly);
    mpz_clear(sq);
    mpz_clear(rho);
    param_list_clear(pl);

    return 0;
}
