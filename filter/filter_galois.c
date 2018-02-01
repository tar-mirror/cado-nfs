#include "cado.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>   /* for _O_BINARY */

#include "portability.h"
#include "filter_config.h"
#include "utils_with_io.h"
#include "mod_ul.h"

char *argv0; /* = argv[0] */

/* Renumbering table to convert from (p,r) to an index */
renumber_t renumber_tab;

static uint32_t *H; /* H contains the hash table */
static unsigned long K = 0; /* Size of the hash table */
static unsigned long noutrels = 0; // Number of output relations

/* return in *oname and *oname_tmp two file names for writing the output
 * of processing the given input file infilename. Both files are placed
 * in the directory outdir if not NULL, otherwise in the current
 * directory.  The parameter outfmt specifies the output file extension
 * and format (semantics are as for fopen_maybe_compressed).
 *
 * proper use requires that data be first written to the file whose name
 * is *oname_tmp, and later on upon successful completion, that file must
 * be renamed to *oname. Otherwise disaster may occur, as there is a slim
 * possibility that *oname == infilename on return.
 */
static void
get_outfilename_from_infilename (char *infilename, const char *outfmt,
    const char *outdir, char **oname,
    char **oname_tmp)
{
  const char * suffix_in;
  const char * suffix_out;
  get_suffix_from_filename (infilename, &suffix_in);
  suffix_out = outfmt ? outfmt : suffix_in;

  char * newname = strdup(infilename);
  ASSERT_ALWAYS(strlen(suffix_in) <= strlen(newname));
  newname[strlen(newname)-strlen(suffix_in)]='\0';

#define chkrcp(x) do { int rc = x; ASSERT_ALWAYS(rc>=0); } while (0)
  if(outdir) {
    const char * basename = path_basename(newname);
    chkrcp(asprintf(oname_tmp, "%s/%s.tmp%s", outdir, basename, suffix_out));
    chkrcp(asprintf(oname, "%s/%s%s", outdir, basename, suffix_out));
  } else {
    chkrcp(asprintf(oname_tmp, "%s.tmp%s", newname, suffix_out));
    chkrcp(asprintf(oname, "%s%s", newname, suffix_out));
  }
#undef  chkrcp

#if DEBUG >= 1
  fprintf (stderr, "DEBUG: Input file name: %s,\nDEBUG: temporary output file "
      "name: %s,\nDEBUG: final output file name: %s\n", infilename,
      *oname_tmp, *oname);
#endif
  free(newname);
}

// Global variable for the table of Galois action
// For an ideal of index idx, Gal[idx] gives the index of a
// representative of the class under Galois action. It might be idx
// itself, or another index.
index_t *Gal;

// returns 1/r mod p; FIXME: should be replaced some time.
static p_r_values_t my_inv(p_r_values_t r, p_r_values_t p){
    p_r_values_t sigma_r;

    modulusul_t mm;
    residueul_t xx;
    modul_initmod_ul(mm, p);
    modul_init(xx, mm);
    modul_set_ul(xx, r, mm);
    modul_inv(xx, xx, mm);
    sigma_r = modul_get_ul(xx, mm);
    modul_clear(xx, mm);
    modul_clearmod(mm);
    return sigma_r;
}

// TMP, TMP: should be replaced by stuff in galois_utils.
static p_r_values_t apply_auto(p_r_values_t p, p_r_values_t r, const char *action){
    p_r_values_t sigma_r = 0;

    if(strcmp(action, "1/y") == 0 || strcmp(action, "autom2.1g") == 0){
	if (r == 0)
	    sigma_r = p;
	else if (r == p)
	    sigma_r = 0;
	else {
	    sigma_r = my_inv(r, p);
	    ASSERT_ALWAYS(sigma_r < p);
	}
    }
    else if(strcmp(action, "_y") == 0 || strcmp(action, "autom2.2g") == 0){
	if (r == 0){
	    fprintf(stderr, "WARNING: r=0\n");
	    sigma_r = 0;
	}
	else if (r == p){
	    fprintf(stderr, "WARNING: r=oo\n");
	    sigma_r = p;
	}
	else {
	    sigma_r = p - r;
	    ASSERT_ALWAYS(sigma_r < p);
	}
    }
    else if(strcmp(action, "autom3.1g") == 0){
	// x -> 1-1/x
	if (r == 0){
	    fprintf(stderr, "WARNING: r=0\n");
	    sigma_r = p;
	}
	else if (r == p){
	    fprintf(stderr, "WARNING: r=oo\n");
	    sigma_r = 1;
	}
	else if (r == 1){
	    fprintf(stderr, "WARNING: r=1\n");
	    sigma_r = 0;
	}
	else {
	    // 1 < r < p => 1 < 1/r < p
	    // => 1 < p+1-1/r < p
	    sigma_r = p + 1 - my_inv(r, p);
	    ASSERT_ALWAYS(sigma_r < p);
	}
    }
    else if(strcmp(action, "autom3.2g") == 0){
	// x -> -1-1/x
	if (r == 0){
	    fprintf(stderr, "WARNING: r=0\n");
	    sigma_r = p;
	}
	else if (r == p){
	    fprintf(stderr, "WARNING: r=oo\n");
	    sigma_r = p-1;
	}
	else if (r == p-1){
	    fprintf(stderr, "WARNING: r=-1\n");
	    sigma_r = 0;
	}
	else {
	    // 1 <= r < p-1 => 1 <= 1/r < p-1
	    sigma_r = p - 1 - my_inv(r, p);
	    ASSERT_ALWAYS(sigma_r < p);
	}
    }
    else
      ASSERT_ALWAYS(0); /* should not happen */
    return sigma_r;
}

static void
compute_galois_action (renumber_t tab, cado_poly cpoly, const char *action)
{
  index_t i;
  p_r_values_t old_p, p, r[20], rr;
  index_t ind[20];
  int side, old_side;
  int nr;
  int j, ord, imat[4];
  old_p = 0;
  old_side = 42; // any value different from the legit ones.
  nr = 0;

  Gal = (index_t *) malloc(tab->size * sizeof(index_t));
  ASSERT_ALWAYS(Gal != NULL);

  automorphism_init(&ord, imat, action);
  for (i = 0; i < tab->size; i++) {
//    if (i % (1<<16) == 0)
//      fprintf(stderr, "at %lu\n", (unsigned long)i);
    if (tab->table[i] == RENUMBER_SPECIAL_VALUE) {
      Gal[i] = i;
    } else {
      renumber_get_p_r_from_index(tab, &p, &rr, &side, i, cpoly);
      // Is it a new (p, side) ?
      if (old_p == p && old_side == side) {
        r[nr] = rr;
        ind[nr] = i;
        nr++;
      }
      // If needed, take care of previous (p,side)
      if ((old_p != p || old_side != side) || i == tab->size-1)
      {
        if (old_p != 0) {
          // Sort the roots, to put sigma(r) near r.
	  // -> build orbits r, sigma(r), ..., sigma^{ord-1}(r)
	  if ((nr % ord) != 0){
            fprintf(stderr,
		"Warning: number of roots not divisible by %d,"
		"skipping p=%" PRpr ", r=%" PRpr "\n", ord, old_p, r[0]);
            for (int k = 0; k < nr; ++k)
              Gal[ind[k]] = ind[k];
          } else {
            int k = 0;
            while (k < nr) {
              // Get sigma(r[k]) mod p
	      p_r_values_t sigma_r = apply_auto(old_p, r[k], action);

	      for(j = 1; j < ord; j++){
		  // r[k], ..., sigma^{j-1}(r[k]) already treated
		  // eq. r[k], ..., r[k+j-1]
		  // Find the index of sigma_r
		  int l;
		  for (l = k+j; l <= nr; ++l) {
		      if (r[l] == sigma_r)
			  break;
		  }
		  ASSERT_ALWAYS(l < nr);
		  // Swap position k+j and l
		  r[l] = r[k+j];
		  r[k+j] = sigma_r;
		  int tmp = ind[l];
		  ind[l] = ind[k+j];
		  ind[k+j] = tmp;
		  sigma_r = apply_auto(old_p, sigma_r, action);
	      }
	      ASSERT_ALWAYS(sigma_r == r[k]);
              // Next
              k += ord;
            }
            // Store the correspondence between conjugate ideals
            for (k = 0; k < nr; k += ord) {
		for(j = 0; j < ord; j++)
		    Gal[ind[k+j]] = ind[k];
            }
          }
        }
        // Prepare for next
        old_p = p;
        old_side = side;
        nr = 1;
        r[0] = rr;
        ind[0] = i;
      }
    }
  }
}

// Case 2.1 (x -> 1/x): (a-b/x) = 1/x*(-b-(-a)*x) = (-b, -a) ~ (b, a)
// Hash value that is the same for (a,b) and (b,a)
// (with sign normalization).
static inline uint64_t myhash_2_1(int64_t a, uint64_t b)
{
  uint64_t h0, h1;
  h0 = CA_DUP2 * (uint64_t) a + CB_DUP2 * b;
  if (a > 0) {
    h1 = CA_DUP2 * b + CB_DUP2 * (uint64_t)a;
  } else {
    int64_t bb = -((int64_t)b);
    h1 = CA_DUP2 *(uint64_t) bb + CB_DUP2 * (uint64_t) (-a);
  }
  return h0 ^ h1;
}

// Case 2.2 (x -> -x): (a-b*(-x)) = (a-(-b)*x) = (a, -b) ~ (-a, b).
// Hash value that is the same for (a,b) and (-a,b): H((|a|, b))
// (with sign normalization).
static inline uint64_t myhash_2_2(int64_t a, uint64_t b)
{
    int64_t absa = (a >= 0 ? a : -a);
    return (CA_DUP2 * (uint64_t) absa + CB_DUP2 * b);
}

static inline void lexico3(int64_t *aa, int64_t *bb, 
			   int64_t a1, int64_t b1, 
			   int64_t a2, int64_t b2, 
			   int64_t a3, int64_t b3)
{
    // make signs ok
    if(b2 < 0){ a2 = -a2; b2 = -b2; }
    if(b3 < 0){ a3 = -a3; b3 = -b3; }
    // take largest pair (a_i, b_i) in lexicographic order
    if(a1 > a2){
	if(a1 > a3){ *aa = a1; *bb = b1; }
	else if(a1 < a3){ *aa = a3; *bb = b3; }
	else{ // a1 == a3
	    if(b1 >= b3){ *aa = a1; *bb = b1; }
	    else{ *aa = a3; *bb = b3; }
	}
    }
    else{ // a1 <= a2
	if(a2 < a3){ *aa = a3; *bb = b3; }
	else if(a2 > a3){ *aa = a2; *bb = b2; }
	else{ // a1 <= a2 == a3: we cannot have a1 = a2 = a3 (?)
	    if(b2 >= b3){ *aa = a2; *bb = b2; }
	    else{ *aa = a3; *bb = b3; }
	}
    }
    ASSERT_ALWAYS(*aa > 0);
}

// Case 3.1 (x -> 1-1/x): (a, b), (b, b-a), (b-a, -a)
// If a < b: (a, b), (b, b-a), (b-a, -a) ~ (a-b, a) if a > 0 else (b-a, -a).
// If a > b: (a, b), (b, b-a) ~ (-b, a-b); (b-a, -a) ~ (a-b, a).
static inline uint64_t myhash_3_1(int64_t a, uint64_t b)
{
    int64_t b1 = (int64_t)b, a1 = a, a2, b2, a3, b3, aa, bb;
    a2 = b1; b2 = -a1+b1;
    a3 = b2; b3 = -a2+b2;
    lexico3(&aa, &bb, a1, b1, a2, b2, a3, b3);
    uint64_t h = (CA_DUP2 * (uint64_t) aa + CB_DUP2 * (uint64_t)bb);
    fprintf(stderr, "HASH3.1: %" PRId64 " %" PRIu64 " -> %" PRId64 " %" PRId64 " -> h=%" PRIu64 "\n", a, b, aa, bb, h);
    return h;
}

// Case 3.2 (x -> -1-1/x): (a, b), (b, -a-b), (-a-b, a)
static inline uint64_t myhash_3_2(int64_t a, uint64_t b)
{
    int64_t b1 = (int64_t)b, a1 = a, a2, b2, a3, b3, aa, bb;
    a2 = b1; b2 = -a1-b1;
    a3 = b2; b3 = -a2-b2;
    lexico3(&aa, &bb, a1, b1, a2, b2, a3, b3);
    uint64_t h = (CA_DUP2 * (uint64_t) aa + CB_DUP2 * (uint64_t)bb);
    fprintf(stderr, "HASH3.2: %" PRId64 " %" PRIu64 " -> %" PRId64 " %" PRId64 " -> h=%" PRIu64 "\n", a, b, aa, bb, h);
    return h;
}

static inline uint64_t myhash(int64_t a, uint64_t b, char *action)
{
    if(strcmp(action, "1/y") == 0 || strcmp(action, "autom2.1g") == 0)
	return myhash_2_1(a, b);
    else if(strcmp(action, "_y") == 0 || strcmp(action, "autom2.2g") == 0)
	return myhash_2_2(a, b);
    else if(strcmp(action, "autom3.1g") == 0)
	return myhash_3_1(a, b);
    else if(strcmp(action, "autom3.2g") == 0)
	return myhash_3_2(a, b);
    else
	return 0;
}

static inline uint32_t
insert_relation_in_dup_hashtable (earlyparsed_relation_srcptr rel,
				  unsigned int *is_dup, char *action)
{
  uint64_t h;
  uint32_t i, j;

  h = myhash(rel->a, rel->b, action);
  i = h % K;
  j = (uint32_t) (h >> 32);
  while (H[i] != 0 && H[i] != j) {
    i++;
    if (UNLIKELY(i == K))
      i = 0;
  }

  if (H[i] == j) {
    *is_dup = 1;
  } else {
    H[i] = j;
    *is_dup = 0;
  }
  return i;
}

static void *
thread_galois (void * context_data, earlyparsed_relation_ptr rel, char *action)
{
  unsigned int is_dup;
  FILE * output = (FILE*) context_data;
  insert_relation_in_dup_hashtable (rel, &is_dup, action);
  if (is_dup)
    return NULL;

  noutrels++;
  
  char buf[1 << 12], *p, *op;
  size_t t;
  unsigned int i, j;

  p = d64toa16(buf, rel->a);
  *p++ = ',';
  p = u64toa16(p, rel->b);
  *p++ = ':';

  for (i = 0; i < rel->nb; i++)
  {
    ASSERT_ALWAYS(rel->primes[i].e != 0);
    index_t h = rel->primes[i].h;
    index_t hrep = Gal[h];
    // The new sign of the exponent is the XOR of the original sign and of 
    // the fact that we change the prime ideal for its conjugate.
    int neg = (rel->primes[i].e < 0) ^ (hrep != h);
    op = p;
    if (neg) { *p++ = '-'; }
    p = u64toa16(p, (uint64_t) hrep);
    *p++ = ',';
    t = p - op;
    if (rel->primes[i].e < 0) {
      j = (unsigned int) ((-rel->primes[i].e) - 1);
    } else {
      j = (unsigned int) (rel->primes[i].e - 1);
    }
    while (j != 0) {
      memcpy(p, op, t);
      p += t;
      j--;
    }
  }

  *(--p) = '\n';
  p[1] = 0;
  if (fputs(buf, output) == EOF) {
    perror("Error writing relation");
    abort();
  }
  return NULL;
}

// TODO: do better than having a function per automorphism.
static void *
thread_galois_2_1 (void * context_data, earlyparsed_relation_ptr rel)
{
    return thread_galois(context_data, rel, "1/y");
}

static void *
thread_galois_2_2 (void * context_data, earlyparsed_relation_ptr rel)
{
    return thread_galois(context_data, rel, "_y");
}

static void *
thread_galois_3_1 (void * context_data, earlyparsed_relation_ptr rel)
{
    return thread_galois(context_data, rel, "autom3.1g");
}

static void *
thread_galois_3_2 (void * context_data, earlyparsed_relation_ptr rel)
{
    return thread_galois(context_data, rel, "autom3.2g");
}

static void declare_usage(param_list pl)
{
  param_list_decl_usage(pl, "filelist", "file containing a list of input files");
  param_list_decl_usage(pl, "basepath", "path added to all file in filelist");
  param_list_decl_usage(pl, "poly", "input polynomial file");
  param_list_decl_usage(pl, "renumber", "input file for renumbering table");
  param_list_decl_usage(pl, "outdir", "by default, input files are overwritten");
  param_list_decl_usage(pl, "outfmt",
      "format of output file (default same as input)");
  param_list_decl_usage(pl, "force-posix-threads", "(switch)");
  param_list_decl_usage(pl, "path_antebuffer", "path to antebuffer program");
  param_list_decl_usage(pl, "nrels", "(approximate) number of input relations");
  param_list_decl_usage(pl, "galois", "Galois action among 1/y or _y");
  verbose_decl_usage(pl);
}

static void
usage (param_list pl, char *argv0)
{
  param_list_print_usage(pl, argv0, stderr);
  exit(EXIT_FAILURE);
}

int
main (int argc, char *argv[])
{
  argv0 = argv[0];
  cado_poly cpoly;
  unsigned long nrels_expected = 0;

  param_list pl;
  param_list_init(pl);
  declare_usage(pl);
  argv++,argc--;

  param_list_configure_switch(pl, "force-posix-threads",
      &filter_rels_force_posix_threads);

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
  }
  /* print command-line arguments */
  verbose_interpret_parameters(pl);
  param_list_print_command_line (stdout, pl);
  fflush(stdout);

  const char * polyfilename = param_list_lookup_string(pl, "poly");
  const char * outfmt = param_list_lookup_string(pl, "outfmt");
  const char * filelist = param_list_lookup_string(pl, "filelist");
  const char * basepath = param_list_lookup_string(pl, "basepath");
  const char * outdir = param_list_lookup_string(pl, "outdir");
  const char * renumberfilename = param_list_lookup_string(pl, "renumber");
  const char * path_antebuffer = param_list_lookup_string(pl, "path_antebuffer");
  const char * action = param_list_lookup_string(pl, "galois");
  param_list_parse_ulong(pl, "nrels", &nrels_expected);

  if (param_list_warn_unused(pl))
  {
    fprintf(stderr, "Error, unused parameters are given\n");
    usage(pl, argv0);
  }
  if (polyfilename == NULL)
  {
    fprintf(stderr, "Error, missing -poly command line argument\n");
    usage(pl, argv0);
  }
  if (renumberfilename == NULL)
  {
    fprintf (stderr, "Error, missing -renumber command line argument\n");
    usage(pl, argv0);
  }
  if (basepath && !filelist)
  {
    fprintf(stderr, "Error, -basepath only valid with -filelist\n");
    usage(pl, argv0);
  }
  if (outfmt && !is_supported_compression_format(outfmt)) {
    fprintf(stderr, "Error, output compression format unsupported\n");
    usage(pl, argv0);
  }
  if ((filelist != NULL) + (argc != 0) != 1) {
    fprintf(stderr, "Error, provide either -filelist or freeform file names\n");
    usage(pl, argv0);
  }
  if (nrels_expected == 0)
  {
    fprintf(stderr, "Error, missing -nrels command line argument "
                    "(or nrels = 0)\n");
    usage(pl, argv0);
  }
  K = 100 + 1.2 * nrels_expected;

  if( action == NULL 
      || (strcmp(action, "1/y") 
	  && strcmp(action, "_y") 
	  && strcmp(action, "autom3.1g")
	  && strcmp(action, "autom3.2g")
	 )
    )
  {
    fprintf(stderr, "Error, missing -action command line argument\n");
    usage(pl, argv0);
  }

  H = (uint32_t*) malloc (K * sizeof (uint32_t));
  ASSERT_ALWAYS(H);
  memset (H, 0, K * sizeof (uint32_t));

  cado_poly_init (cpoly);
  if (!cado_poly_read (cpoly, polyfilename)) {
    fprintf (stderr, "Error reading polynomial file\n");
    exit (EXIT_FAILURE);
  }

  set_antebuffer_path (argv0, path_antebuffer);

  renumber_init_for_reading (renumber_tab);
  renumber_read_table (renumber_tab, renumberfilename);

  fprintf(stderr, "Computing Galois action %s on ideals\n", action);
  compute_galois_action(renumber_tab, cpoly, action);

  fprintf(stderr, "Rewriting relations files\n");
  char ** files;
  unsigned int nb_files = 0;
  files = filelist ? filelist_from_file (basepath, filelist, 0) : argv;
  for (char ** p = files; *p; p++)
    nb_files++;

  struct filter_rels_description desc[2] = {
    { .f = thread_galois_2_1, .arg=0, .n=1, },
    { .f = NULL, },
  };

  if(strcmp(action, "_y") == 0)
      desc[0].f = thread_galois_2_2;
  else if(strcmp(action, "autom3.1g") == 0)
      desc[0].f = thread_galois_3_1;
  else if(strcmp(action, "autom3.2g") == 0)
      desc[0].f = thread_galois_3_2;

  fprintf (stderr, "Reading files (using %d auxiliary threads):\n", desc[0].n);
  for (char **p = files; *p ; p++) {
    FILE * output = NULL;
    char * oname, * oname_tmp;
    char * local_filelist[] = { *p, NULL};

    get_outfilename_from_infilename (*p, outfmt, outdir, &oname, &oname_tmp);
    output = fopen_maybe_compressed(oname_tmp, "w");
    desc[0].arg = (void*) output;

    filter_rels2(local_filelist, desc,
        EARLYPARSE_NEED_AB_HEXA | EARLYPARSE_NEED_INDEX,
        NULL, NULL);

    fclose_maybe_compressed(output, oname_tmp);

#ifdef HAVE_MINGW /* For MinGW, rename cannot overwrite an existing file */
    remove (oname);
#endif
    if (rename(oname_tmp, oname)) {
      fprintf(stderr, "Error while renaming %s into %s\n", oname_tmp, oname);
      abort();
    }

    free(oname);
    free(oname_tmp);
  }

  fprintf(stderr, "Number of output relations: %lu\n", noutrels);

  if (filelist)
    filelist_clear(files);

  param_list_clear(pl);
  renumber_clear (renumber_tab);
  cado_poly_clear (cpoly);
  free(H);
  return 0;
}
