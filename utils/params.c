#include "cado.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>              /* isdigit isspace */
#include <limits.h>             /* INT_MIN INT_MAX */
#include <errno.h>
#include <stdarg.h>
#include <inttypes.h>
#include <pthread.h>

#include "params.h"
#include "macros.h"
#include "misc.h"
#include "portability.h"
#include "version_info.h"
#include "verbose.h"

static pthread_mutex_t mutex[1] = {PTHREAD_MUTEX_INITIALIZER};

void param_list_init(param_list_ptr pl)
{
    memset(pl, 0, sizeof(param_list));
    pl->ndocs_alloc = 16;
    pl->ndocs = 0;
    pl->docs = (param_list_doc *) malloc(pl->ndocs_alloc *
            sizeof(param_list_doc));
    pl->usage_hdr = NULL;
    pl->alloc = 16;
    pl->p = (parameter *) malloc(pl->alloc * sizeof(parameter));
    pl->consolidated = 1;
    pl->size = 0;
    pl->aliases = NULL;
    pl->naliases = 0;
    pl->naliases_alloc = 0;
    pl->switches = NULL;
    pl->nswitches = 0;
    pl->nswitches_alloc = 0;
    ASSERT_ALWAYS(pl->docs != NULL && pl->p != NULL);
}

static void parameter_set(parameter dst, parameter src)
{
    memcpy(dst, src, sizeof(parameter));
    dst->key = src->key ? strdup(src->key) : NULL;
    dst->value = strdup(src->value);
}

static void param_list_doc_set(param_list_doc dst, param_list_doc src)
{
    dst->key = strdup(src->key);
    dst->doc = strdup(src->doc);
}

static void param_list_alias_set(param_list_alias dst, param_list_alias src)
{
    dst->alias = strdup(src->alias);
    dst->key = strdup(src->key);
}
static void param_list_switch_set(param_list_switch dst, param_list_switch src)
{
    dst->switchname = strdup(src->switchname);
    dst->ptr = src->ptr;
}

#define COPY_LIST(__dst, __src, __data, __size, __alloc, __type) do {	\
    __dst->__data = NULL;						\
    __dst->__size = __src->__size;					\
    __dst->__alloc = __src->__size;					\
    if (__src->__size) { 						\
        __dst->__data = malloc(__src->__size * sizeof(__type));		\
        for(int __i = 0 ; __i < (int) __src->__size ; __i++) {		\
            __type ## _set(__dst->__data[__i], __src->__data[__i]);	\
        }								\
    }									\
} while (0)

void param_list_set(param_list_ptr pl, param_list_srcptr pl0)
{
    memcpy(pl, pl0, sizeof(param_list));
    if (pl->usage_hdr) pl->usage_hdr = strdup(pl->usage_hdr);
    COPY_LIST(pl, pl0, p, size, alloc, parameter);
    COPY_LIST(pl, pl0, aliases, naliases, naliases_alloc, param_list_alias);
    COPY_LIST(pl, pl0, docs, ndocs, ndocs_alloc, param_list_doc);
    COPY_LIST(pl, pl0, switches, nswitches, nswitches_alloc, param_list_switch);
}

void param_list_swap(param_list_ptr pl, param_list_ptr pl0)
{
    param_list x;
    memcpy(x, pl, sizeof(param_list));
    memcpy(pl, pl0, sizeof(param_list));
    memcpy(pl0, x, sizeof(param_list));
}

void param_list_clear(param_list_ptr pl)
{
    free(pl->usage_hdr);
    for(int i = 0 ; i < pl->ndocs ; i++) {
        free(pl->docs[i]->key);
        free(pl->docs[i]->doc);
    }
    free(pl->docs);
    for(unsigned int i = 0 ; i < pl->size ; i++) {
        if (pl->p[i]->key) free(pl->p[i]->key);
        free(pl->p[i]->value);
    }
    free(pl->p);
    for(int i = 0 ; i < pl->naliases ; i++) {
        if (pl->aliases[i]->alias) free(pl->aliases[i]->alias);
    }
    free(pl->aliases);
    for(int i = 0 ; i < pl->nswitches ; i++) {
        free(pl->switches[i]->switchname);
    }
    free(pl->switches);
    memset(pl, 0, sizeof(pl[0]));
}

void param_list_usage_header(param_list_ptr pl, const char * hdr, ...)
{
    va_list ap;
    va_start(ap, hdr);
    int rc = vasprintf(&(pl->usage_hdr), hdr, ap);
    ASSERT_ALWAYS(rc >= 0);
    va_end(ap);
}


void param_list_decl_usage(param_list_ptr pl, const char * key, const char * doc)
{
    if (pl->ndocs == pl->ndocs_alloc) {
        pl->ndocs_alloc += 16;
        pl->docs = (param_list_doc *) realloc(pl->docs,
                pl->ndocs_alloc * sizeof(param_list_doc));
        ASSERT_ALWAYS(pl->docs != NULL);
    }
    int i = pl->ndocs;
    pl->ndocs++;
    pl->docs[i]->key = strdup(key);
    pl->docs[i]->doc = strdup(doc);
    ASSERT_ALWAYS(pl->docs[i]->key != NULL && pl->docs[i]->doc != NULL);
    pl->use_doc = 1;
}

static int is_documented_key(param_list_ptr pl, const char *key) {
    for (int i = 0; i < pl->ndocs; ++i) {
        if (strcmp(key, pl->docs[i]->key) == 0)
            return 1;
    }
    return 0;
}

void param_list_print_usage(param_list_ptr pl, const char * argv0, FILE *f)
{
    if (argv0 != NULL)
        fprintf(f, "Usage: %s <parameters>\n", argv0);
    if (pl->usage_hdr != NULL)
        fputs(pl->usage_hdr, f);
    fprintf(f, "The available parameters are the following:\n");
    char whites[20];
    for (int i = 0; i < 20; ++i)
        whites[i] = ' ';
    for (int i = 0; i < pl->ndocs; ++i) {
        int l = strlen(pl->docs[i]->key);
        l = MAX(1, 10-l);
        whites[l] ='\0';
        fprintf(f, "    -%s%s%s\n", pl->docs[i]->key, whites, pl->docs[i]->doc);
        whites[l] =' ';
    }
}

static void make_room(param_list_ptr pl, unsigned int more)
{
    if (pl->size + more <= pl->alloc) {
        return;
    }

    for( ; pl->size + more > pl->alloc ; ) {
      pl->alloc += 8 + (pl->alloc >> 1);
    }

    pl->p = (parameter *) realloc(pl->p, pl->alloc * sizeof(parameter));
}

static int param_list_add_key_nostrdup(param_list_ptr pl,
        char * key, char * value, enum parameter_origin o)
{
    make_room(pl, 1);
    int r = pl->size;
    pl->p[pl->size]->key = key;
    pl->p[pl->size]->value = value;
    pl->p[pl->size]->origin = o;
    // switches always count as parsed, of course. Hence the (value==NULL) thing
    pl->p[pl->size]->parsed = (value == NULL);
    // values above 1 are built within the sorting step.
    pl->p[pl->size]->seen = 1;
    pl->size++;
    pl->consolidated = 0;
    return r;
}

int param_list_add_key(param_list_ptr pl,
        const char * key, const char * value, enum parameter_origin o)
{
    int r = param_list_add_key_nostrdup(pl,
            key ? strdup(key) : NULL, value ? strdup(value) : NULL, o);
    return r;
}

struct sorting_data {
    parameter_srcptr s;
    int p;
};

typedef int (*sortfunc_t) (const void *, const void *);

int strcmp_or_null(const char * a, const char * b)
{
    if (a == NULL) { return b ? -1 : 0; }
    if (b == NULL) { return a ? 1 : 0; }
    return strcmp(a, b);
}

int paramcmp(const struct sorting_data * a, const struct sorting_data * b)
{
    int r;
    r = strcmp_or_null(a->s->key, b->s->key);
    if (r) return r;
    r = a->s->origin - b->s->origin;
    if (r) return r;
    /* Compare by pointer position so that the sort is stable */
    return a->p - b->p;
}

/* Sort (for searching), and remove duplicates. */
static void param_list_consolidate(param_list_ptr pl)
{
    if (pl->consolidated) {
        return;
    }
    struct sorting_data * intermediate;
    intermediate = malloc(pl->size * sizeof(struct sorting_data));
    for(unsigned int i = 0 ; i < pl->size ; i++) {
        intermediate[i].p = i;
        intermediate[i].s = pl->p[i];
    }
    qsort(intermediate, pl->size, sizeof(struct sorting_data),
            (sortfunc_t) &paramcmp);

    parameter * np = (parameter *) malloc(pl->alloc * sizeof(parameter));
    for(unsigned int i = 0 ; i < pl->size ; i++) {
        memcpy(np[i], pl->p[intermediate[i].p], sizeof(parameter));
    }
    free(pl->p);
    pl->p = np;
    free(intermediate);

    // now remove duplicates. The sorting has priorities right.
    unsigned int j = 0;
    for(unsigned int i = 0 ; i < pl->size ; i++) {
        if (pl->p[i]->key != NULL && i + 1 < pl->size && strcmp(pl->p[i]->key, pl->p[i+1]->key) == 0) {
            /* The latest pair in the list is the one having highest
             * priority. So we don't do the copy at this moment.
             */
            free(pl->p[i]->key);
            free(pl->p[i]->value);
            // this value is useful for switches
            pl->p[i+1]->seen += pl->p[i]->seen;
        } else {
            // in theory memcpy does not allow overlaps, even trivial ones.
            if (i != j) {
                memcpy(pl->p[j], pl->p[i], sizeof(parameter));
            }
            j++;
        }
    }
    pl->size = j;

    /* consolidated list have to guarantee ordering. We have broken
     * ordering, clearly */
    pl->consolidated = 0;
}

void param_list_remove_key(param_list_ptr pl, const char * key)
{
    unsigned int j = 0;
    for(unsigned int i = 0 ; i < pl->size ; i++) {
        if (strcmp_or_null(pl->p[i]->key, key) == 0) {
            if (pl->p[i]->key) free(pl->p[i]->key);
            free(pl->p[i]->value);
        } else {
            if (i != j) {
                memcpy(pl->p[j], pl->p[i], sizeof(parameter));
            }
            j++;
        }
    }
    pl->size = j;
    /* if the list was consolidated (sorted), it still is. If it wasn't,
     * then, well, clearly it's not better */
}

/* If step_on_empty_line is non-zero, then this function reads the file until a
 * line containing only space caracters (check with isspace) is found. It allows
 * to read a file contaning more than one polynomial.
 * Otherwise the function reads the whole file.
 */
int param_list_read_stream(param_list_ptr pl, FILE *f, int stop_on_empty_line)
{
    int all_ok=1;
    const int linelen = 2048;
    char line[linelen];
    char * newkey;
    char * newvalue;
    while (!feof(f)) {
        if (fgets(line, linelen, f) == NULL)
            break;
        if (line[0] == '#')
            continue;
        // remove possible comment at end of line.
        char * hash;
        if ((hash = strchr(line, '#')) != NULL) {
            *hash = '\0';
        }

        char * p = line;

        // trailing space
        int l = strlen(p);
        for( ; l && isspace((int)(unsigned char)p[l-1]) ; l--);
        p[l] = '\0';

        // leading space.
        for( ; *p && isspace((int)(unsigned char)*p) ; p++, l--);

        // empty ps are ignored (unless stop_on_empty_line is non-zero).
        if (l == 0)
        {
          if (stop_on_empty_line)
            break;
          else
            continue;
        }

        // look for a left-hand-side.
        l = 0;
        if (!(isalpha((int)(unsigned char)p[l]) || p[l] == '_' || p[l] == '-')) {
            param_list_add_key(pl, NULL, line, PARAMETER_FROM_FILE);
            continue;
        }
        for( ; p[l] && (isalnum((int)(unsigned char)p[l]) || p[l] == '_' || p[l] == '-') ; l++);

        int lhs_length = l;

        if (lhs_length == 0) {
            fprintf(stderr, "Parse error, no usable key for config line:\n%s\n",
                    line);
            all_ok=0;
            continue;
        }

        /* Now we can match (whitespace*)(separator)(whitespace*)(data)
         */
        char * q = p + lhs_length;
        for( ; *q && isspace((int)(unsigned char)*q) ; q++);

        /* match separator, which is one of : = := */
        if (*q == '=') {
            q++;
        } else if (*q == ':') {
            q++;
            if (*q == '=')
                q++;
        } else if (q == p + lhs_length) {
            fprintf(stderr, "Parse error, no separator for config line:\n%s\n",
                    line);
            all_ok=0;
            continue;
        }
        for( ; *q && isspace((int)(unsigned char)*q) ; q++);

        newkey = malloc(lhs_length + 1);
        memcpy(newkey, p, lhs_length);
        newkey[lhs_length]='\0';

        newvalue = strdup(q);

        param_list_add_key_nostrdup(pl, newkey, newvalue, PARAMETER_FROM_FILE);
    }
    param_list_consolidate(pl);

    return all_ok;
}

int param_list_read_file(param_list_ptr pl, const char * name)
{
    FILE * f;
    f = fopen(name, "r");
    if (f == NULL) {
        fprintf(stderr, "Cannot read %s\n", name);
        exit(1);
    }
    int r = param_list_read_stream(pl, f, 0);
    fclose(f);
    return r;
}

int param_list_configure_alias(param_list_ptr pl, const char * key, const char * alias)
{
    if (pl->use_doc) {
        const char *k = key;
        if (k[0] == '-')
            k++;
        if (!is_documented_key(pl, k))
            fprintf(stderr, "# Warning: an alias %s is declared to the key %s that is undocumented\n", alias, key);
    }

    size_t len = strlen(alias);

    ASSERT_ALWAYS(alias != NULL);
    ASSERT_ALWAYS(key != NULL);

    /* A switch may be aliased, but only as another switch !!! */
    ASSERT_ALWAYS(key[0] != '-' || (alias[0] == '-' && alias[len-1] != '='));

    if (alias[0] != '-' && alias[len-1] != '=') {
        /* Then, accept both the -xxx, --xxx, and xxx= forms */
        char * tmp;
        tmp = malloc(len + 4);
        snprintf(tmp, len+4, "-%s", alias);
        param_list_configure_alias(pl, key, tmp);
        snprintf(tmp, len+4, "--%s", alias);
        param_list_configure_alias(pl, key, tmp);
        snprintf(tmp, len+4, "%s=", alias);
        param_list_configure_alias(pl, key, tmp);
        free(tmp);
        return 0;
    }

    if (pl->naliases == pl->naliases_alloc) {
        pl->naliases_alloc += 1;
        pl->naliases_alloc <<= 1;
        pl->aliases = realloc(pl->aliases, pl->naliases_alloc * sizeof(param_list_alias));
    }
    pl->aliases[pl->naliases]->alias = strdup(alias);
    pl->aliases[pl->naliases]->key = key;
    pl->naliases++;
    return 0;
}

int param_list_configure_switch(param_list_ptr pl, const char * switchname, int * ptr)
{
    /* Some switches are passed as switchname = "switch", some as "-switch",
       and some as "--switch" ... */
    int offset = (switchname[0] == '-') ? (switchname[1] == '-' ? 2 : 1) : 0;
    ASSERT_ALWAYS(switchname != NULL);
    if (pl->use_doc)
        if (!is_documented_key(pl, offset+switchname))
            fprintf(stderr, "# Warning: a switch %s is declared but is undocumented\n", switchname);

    if ((pl->nswitches + 1) >= pl->nswitches_alloc) {
        pl->nswitches_alloc += 2;
        pl->nswitches_alloc <<= 1;
        pl->switches = realloc(pl->switches, pl->nswitches_alloc * sizeof(param_list_switch));
    }
    char * tmp = (char *) malloc(strlen(switchname)+2);
    tmp[0]='-';
    if (switchname[1] == '-') { // have -- in the switch
        strncpy(tmp, switchname, strlen(switchname) + 1);
    } else {
        strncpy(tmp+1, switchname, strlen(switchname) + 1);
    }
    // put the -- version
    pl->switches[pl->nswitches]->switchname = strdup(tmp);
    pl->switches[pl->nswitches]->ptr = ptr;
    if (ptr) *ptr = 0;
    pl->nswitches++;
    // put the - version
    pl->switches[pl->nswitches]->switchname = strdup(tmp+1);
    pl->switches[pl->nswitches]->ptr = ptr;
    if (ptr) *ptr = 0;
    pl->nswitches++;
    free(tmp);

    return 0;
}


static int param_list_update_cmdline_alias(param_list_ptr pl,
        param_list_alias al,
        int * p_argc, char *** p_argv)
{
    if (!pl->cmdline_argv0) {
        pl->cmdline_argv0 = *p_argv;
        pl->cmdline_argc0 = *p_argc;
    }
    const char * a = (*p_argv[0]);
    if (al->alias[strlen(al->alias)-1] == '=') {
        // since switches are aliased only by switches, we know we have a plain
        // option here.
        if (strncmp(a, al->alias, strlen(al->alias)) != 0)
            return 0;
        a += strlen(al->alias);
        if (a[0] == '\0')
            return 0;
        // no +1 , since the alias contains the = sign already.
        param_list_add_key(pl, al->key, a, PARAMETER_FROM_CMDLINE);
        (*p_argv)+=1;
        (*p_argc)-=1;
        return 1;
    }
    if (strcmp(a, al->alias) == 0) {
        if (al->key[0] == '-') {
            /* This is a switch ; we have to treat it accordingly. The
             * difficult part is to properly land on
             * param_list_update_cmdline_switch at the proper time. This
             * means in particular not necessarily there. It's
             * considerably easier to simply change the value in the
             * command line. Except that it's a const cast, it's ugly.
             * Okay, my apologies, blah blah.
             */
            (*p_argv)[0] = (char*) al->key;
            /* leave argv and argc unchanged. */
            return 0;
        }
        (*p_argv)+=1;
        (*p_argc)-=1;
        if (*p_argc == 0) {
            fprintf(stderr, "Option %s requires an argument\n", a);
            exit(1);
        }
        param_list_add_key(pl, al->key, (*p_argv[0]), PARAMETER_FROM_CMDLINE);
        (*p_argv)+=1;
        (*p_argc)-=1;
        return 1;
    }
    return 0;
}

static int param_list_update_cmdline_switch(param_list_ptr pl,
        param_list_switch switchpar,
        int * p_argc, char *** p_argv)
{
    if (!pl->cmdline_argv0) {
        pl->cmdline_argv0 = *p_argv;
        pl->cmdline_argc0 = *p_argc;
    }
    const char * a = (*p_argv[0]);
    if (strcmp(a, switchpar->switchname) == 0) {
        param_list_add_key(pl, switchpar->switchname, NULL, PARAMETER_FROM_CMDLINE);
        (*p_argv)+=1;
        (*p_argc)-=1;
        if (switchpar->ptr) (*(switchpar->ptr))++;
        return 1;
    }
    if (strncmp(switchpar->switchname, "--", 2) != 0)
        return 0;
    char * inv_switch;
    int rc = asprintf(&inv_switch, "--no-%s", switchpar->switchname+2);
    ASSERT_ALWAYS(rc>=0);
    int match = strcmp(inv_switch, a) == 0;
    free(inv_switch);
    if (match) {
        param_list_add_key(pl, a, NULL, PARAMETER_FROM_CMDLINE);
        (*p_argv)+=1;
        (*p_argc)-=1;
        if (switchpar->ptr) (*(switchpar->ptr))=0;
        return 1;
    }
    return 0;
}

int param_list_update_cmdline(param_list_ptr pl,
        int * p_argc, char *** p_argv)
{
    if (!pl->cmdline_argv0) {
        pl->cmdline_argv0 = *p_argv;
        pl->cmdline_argc0 = *p_argc;
    }
    if (*p_argc == 0)
        return 0;

    int i;

    /* We rely on having alias scanning first, because this incurs a
     * command line changed (could get along without except in the case
     * of switches where it's particularly handy).
     */
    for(i = 0 ; i < pl->naliases ; i++) {
        if (param_list_update_cmdline_alias(pl, pl->aliases[i], p_argc, p_argv))
            return 1;
    }

    for(i = 0 ; i < pl->nswitches ; i++) {
        if (param_list_update_cmdline_switch(pl, pl->switches[i], p_argc, p_argv))
            return 1;
    }

    const char * a = (*p_argv[0]);
    if (*p_argc >= 2 && a[0] == '-') {
        a++;
        a+= *a == '-';
        int x=0;
        /* parameters _must_ begin by alphabetic characters, or
         * otherwise we suffer to distinguish immediate numerical
         * entries.
         */
        if (!isalpha((int)(unsigned char)a[x]))
            return 0;
        for( ; a[x] && (isalnum((int)(unsigned char)a[x]) || a[x] == '_' || a[x] == '-') ; x++);
        if (a[x] == '\0') {
            param_list_add_key(pl, a, (*p_argv)[1], PARAMETER_FROM_CMDLINE);
            (*p_argv)+=2;
            (*p_argc)-=2;
            return 1;
        }
    } else {
        /* Check for <key>=<value> syntax */
        int x=0;
        for( ; a[x] && (isalnum((int)(unsigned char)a[x]) || a[x] == '_' || a[x] == '-') ; x++);
        if (a[x] == '=' && a[x+1]) {
            char * newkey = malloc(x+1);
            memcpy(newkey, a, x);
            newkey[x]='\0';
            char * newvalue = strdup(a+x+1);
            param_list_add_key_nostrdup(pl,
                    newkey, newvalue, PARAMETER_FROM_CMDLINE);
            (*p_argv)+=1;
            (*p_argc)-=1;
            return 1;
        }
    }
    return 0;
}

int param_strcmp(const char * a, parameter_srcptr b)
{
    return strcmp_or_null(a, b->key);
}

static int assoc(param_list_ptr pl, const char * key)
{
    if (pl->use_doc) {
        const char *k = (key[0] == '-') ? (1+key) : key;
        if (!is_documented_key(pl, k)) 
            fprintf(stderr, "# Warning: parameter %s is checked by this program but is undocumented.\n", k);
    }

    void * found;

    param_list_consolidate(pl);
    found = bsearch(key, pl->p, pl->size,
            sizeof(parameter), (sortfunc_t) param_strcmp);
    if (found == NULL)
        return -1;
    parameter * c = (parameter *) found;
    return c-pl->p;
}

/* Look up an entry in a param_list, and update the parsed flag. It does
   mutex locking to make look-ups thread safe; the caller must not access
   any param_list entries by itself. */
static int
get_assoc(param_list_ptr pl, const char * const key, char ** const value, int * const seen)
{
    pthread_mutex_lock(mutex);
    const int v = assoc(pl, key);
    const int found = (v >= 0);
    if (found) {
        pl->p[v]->parsed = 1;
        if (value != NULL) {
            *value = pl->p[v]->value;
        }
        if (seen != NULL) {
            *seen = pl->p[v]->seen;
        }
    }
    pthread_mutex_unlock(mutex);
    return found;
}

int param_list_parse_long(param_list_ptr pl, const char * key, long * r)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    char * end;
    long res;
    res = strtol(value, &end, 0);
    if (*end != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s is not a long: %s\n",
                key, value);
        exit(1);
    }
    if (r)
        *r = res;
    return seen;
}

int param_list_parse_int(param_list_ptr pl, const char * key, int * r)
{
    long res;
    int rc;
    rc = param_list_parse_long(pl, key, &res);
    if (rc == 0)
        return 0;
    if (res > INT_MAX || res < INT_MIN) {
        fprintf(stderr, "Parse error:"
                " parameter for key %s does not fit within an int: %ld\n",
                key, res);
        exit(1);
    }
    if (r)
        *r  = res;
    return rc;
}

int param_list_parse_long_and_long(param_list_ptr pl, const char * key, long * r, const char * sep)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    char *orig_value = value, * end;
    long res[2];
    res[0] = strtol(value, &end, 0);
    if (strncmp(end, sep, strlen(sep)) != 0) {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d%s%%d; got %s\n",
                key, sep, orig_value);
        exit(1);
    }
    value = end + strlen(sep);
    res[1] = strtol(value, &end, 0);
    if (*end != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d%s%%d; got %s\n",
                key, sep, orig_value);
        exit(1);
    }
    if (r) {
        r[0] = res[0];
        r[1] = res[1];
    }
    return seen;
}

int param_list_parse_int_and_int(param_list_ptr pl, const char * key, int * r, const char * sep)
{
#if 1
  long rr[2] = {0, 0};
    if (r) {
        rr[0] = r[0];
        rr[1] = r[1];
    }
    int seen = param_list_parse_long_and_long(pl, key, rr, sep);
    if (r) {
        r[0] = rr[0];
        r[1] = rr[1];
    }
    return seen;
#else
    long rr[2];
    int seen = param_list_parse_long_and_long(pl, key,r ? rr : NULL, sep);
    if (r) {
        r[0] = rr[0];
        r[1] = rr[1];
    }
    return seen;
#endif
}

int param_list_parse_uint_and_uint(param_list_ptr pl, const char * key, unsigned int * r, const char * sep)
{
    long rr[2] = {0, 0};
    if (r) {
        rr[0] = r[0];
        rr[1] = r[1];
    }
    int seen = param_list_parse_long_and_long(pl, key, rr, sep);
    if (r) {
        r[0] = rr[0];
        r[1] = rr[1];
    }
    return seen;
}

int param_list_parse_intxint(param_list_ptr pl, const char * key, int * r)
{
    return param_list_parse_int_and_int(pl, key, r, "x");
}

int param_list_parse_uint64_and_uint64(param_list_ptr pl, const char * key,
    uint64_t * r, const char * sep)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    char *orig_value = value, * end;
    unsigned long long int res[2];
    res[0] = strtoull(value, &end, 0);
    if (strncmp(end, sep, strlen(sep)) != 0) {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d%s%%d; got %s\n",
                key, sep, orig_value);
        exit(1);
    }
    value = end + strlen(sep);
    res[1] = strtoull(value, &end, 0);
    if (*end != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d%s%%d; got %s\n",
                key, sep, orig_value);
        exit(1);
    }
    if (r) {
        r[0] = (uint64_t) res[0];
        r[1] = (uint64_t) res[1];
    }
    return seen;
}

int param_list_parse_ulong(param_list_ptr pl, const char * key, unsigned long * r)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    char * end;
    unsigned long res;
    res = strtoul(value, &end, 0);
    if (*end != '\0') {
        fprintf(stderr, "Parse error:"
                " parameter for key %s is not an ulong: %s\n",
                key, value);
        exit(1);
    }
    if (r)
        *r = res;
    return seen;
}

int param_list_parse_size_t(param_list_ptr pl, const char * key, size_t * r)
{
    unsigned long t;
    int res;
    res = param_list_parse_ulong(pl, key, &t);
    if (res && r) { *r = t; }
    return res;
}


int param_list_parse_int64(param_list_ptr pl, const char * key, int64_t * r)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    char * end;
    int64_t res;
    res = strtoimax(value, &end, 0);
    if (*end != '\0') {
        fprintf(stderr, "Parse error:"
                " parameter for key %s is not an int64_t: %s\n",
                key, value);
        exit(1);
    }
    if (r)
        *r = res;
    return seen;
}

int param_list_parse_uint64(param_list_ptr pl, const char * key, uint64_t * r)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    char * end;
    uint64_t res;
    res = strtoumax(value, &end, 0);
    if (*end != '\0') {
        fprintf(stderr, "Parse error:"
                " parameter for key %s is not an uint64_t: %s\n",
                key, value);
        exit(1);
    }
    if (r)
        *r = res;
    return seen;
}

int param_list_parse_uint(param_list_ptr pl, const char * key, unsigned int * r)
{
    unsigned long res;
    int rc = param_list_parse_ulong(pl, key, &res);
    if (rc == 0)
        return 0;
    if (res > UINT_MAX) {
        fprintf(stderr, "Parse error:"
                " parameter for key %s does not fit within an unsigned int: %ld\n",
                key, res);
        exit(1);
    }
    if (r)
        *r  = res;
    return rc;
}

int param_list_parse_uchar(param_list_ptr pl, const char * key, unsigned char * r)
{
    unsigned long res;
    int rc = param_list_parse_ulong(pl, key, &res);
    if (rc == 0)
        return 0;
    if (res > UCHAR_MAX) {
        fprintf(stderr, "Parse error:"
                " parameter for key %s does not fit within an unsigned int: %ld\n",
                key, res);
        exit(1);
    }
    if (r)
        *r  = (unsigned char) res;
    return rc;
}

int param_list_parse_double(param_list_ptr pl, const char * key, double * r)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    char * end;
    double res;
    res = strtod(value, &end);
    if (*end != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s is not an int: %s\n",
                key, value);
        exit(1);
    }
    if (r)
        *r = res;
    return seen;
}

int param_list_parse_double_and_double(param_list_ptr pl, const char * key,
    double * r, const char * sep)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    char *orig_value = value, * end;
    double res[2];
    res[0] = strtod(value, &end);
    if (strncmp(end, sep, strlen(sep)) != 0) {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d%s%%d; got %s\n",
                key, sep, orig_value);
        exit(1);
    }
    value = end + strlen(sep);
    res[1] = strtod(value, &end);
    if (*end != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d%s%%d; got %s\n",
                key, sep, orig_value);
        exit(1);
    }
    if (r) {
        r[0] = res[0];
        r[1] = res[1];
    }
    return seen;
}

int param_list_parse_string(param_list_ptr pl, const char * key, char * r, size_t n)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    if (r && strlen(value) > n-1) {
        fprintf(stderr, "Parse error:"
                " parameter for key %s does not fit within string buffer"
                " of length %lu\n", key, (unsigned long) n);
        exit(1);
    }
    if (r)
        strncpy(r, value, n);
    return seen;
}

int param_list_parse_string_list_alloc(param_list_ptr pl, const char * key, char *** r, int * n, const char * sep)
{
    char * value;
    *r = NULL;
    *n = 0;
    int parsed = 0;
    if (!get_assoc(pl, key, &value, NULL))
        return 0;
    for( ; ; ) {
        char * v = strstr(value, sep);
        int itemsize;
        if (v == NULL) {
            itemsize = strlen(value);
        } else {
            itemsize = v - value;
        }
        *r = realloc(*r, (parsed + 1) * sizeof(char *));
        (*r)[parsed++] = strndup(value, itemsize);
        if (!v)
            break;
        value = v + strlen(sep);
    }
    *n = parsed;
    return parsed;
}

int param_list_get_list_count(param_list_ptr pl, const char * key)
{
    if (!param_list_lookup_string(pl, key))
        return 0;

    char ** names;
    int nitems;
    int rc = param_list_parse_string_list_alloc(pl, key, &names, &nitems, ",");
    if (rc == 0)
        return 0;
    for(int midx = 0 ; midx < nitems ; midx++) {
        free(names[midx]);
    }
    free(names);
    return nitems;
}


int param_list_parse_int_list(param_list_ptr pl, const char * key, int * r, size_t n, const char * sep)
{
    char *value;
    if (!get_assoc(pl, key, &value, NULL))
        return 0;
    char *orig_value = value, * end;
    int * res = malloc(n * sizeof(int));
    memset(res, 0, n * sizeof(int));
    size_t parsed = 0;
    for( ;; ) {
        res[parsed] = strtol(value, &end, 0);
        if (parsed++ == n)
            break;
        if (parsed && *end == '\0')
            break;
        if (strncmp(end, sep, strlen(sep)) != 0) {
            fprintf(stderr, "Parse error: parameter for key %s"
                    " must match %%d(%s%%d)*; got %s\n",
                    key, sep, orig_value);
            exit(1);
        }
        value = end + strlen(sep);
    }
    if (*end != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d(%s%%d){0,%zu}; got %s\n",
                key, sep, n-1, orig_value);
        exit(1);
    }
    if (r) {
        memcpy(r, res, n * sizeof(int));
    }
    free(res);
    return parsed;
}

int param_list_parse_uint_list(param_list_ptr pl, const char * key,
    unsigned int * r, size_t n, const char * sep)
{
    char *value;
    if (!get_assoc(pl, key, &value, NULL))
        return 0;
    char *orig_value = value, * end;
    unsigned int * res = malloc(n * sizeof(unsigned int));
    memset(res, 0, n * sizeof(unsigned int));
    size_t parsed = 0;
    for( ;; ) {
        unsigned long int tmp = strtoul(value, &end, 0);
        ASSERT(tmp <= UINT_MAX);
        res[parsed] = tmp;
        if (parsed++ == n)
            break;
        if (parsed && *end == '\0')
            break;
        if (strncmp(end, sep, strlen(sep)) != 0) {
            fprintf(stderr, "Parse error: parameter for key %s"
                    " must match %%d(%s%%d)*; got %s\n",
                    key, sep, orig_value);
            exit(1);
        }
        value = end + strlen(sep);
    }
    if (*end != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d(%s%%d){0,%zu}; got %s\n",
                key, sep, n-1, orig_value);
        exit(1);
    }
    if (r) {
        memcpy(r, res, n * sizeof(unsigned int));
    }
    free(res);
    return parsed;
}

int param_list_parse_uint64_list(param_list_ptr pl, const char * key,
    uint64_t * r, size_t n, const char * sep)
{
    char *value;
    if (!get_assoc(pl, key, &value, NULL))
        return 0;
    char *orig_value = value, * end;
    uint64_t * res = malloc(n * sizeof(uint64_t));
    memset(res, 0, n * sizeof(unsigned int));
    size_t parsed = 0;
    for( ;; ) {
        res[parsed] = (uint64_t)strtoull(value, &end, 0);
        if (parsed++ == n)
            break;
        if (parsed && *end == '\0')
            break;
        if (strncmp(end, sep, strlen(sep)) != 0) {
            fprintf(stderr, "Parse error: parameter for key %s"
                    " must match %%d(%s%%d)*; got %s\n",
                    key, sep, orig_value);
            exit(1);
        }
        value = end + strlen(sep);
    }
    if (*end != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d(%s%%d){0,%zu}; got %s\n",
                key, sep, n-1, orig_value);
        exit(1);
    }
    if (r) {
        memcpy(r, res, n * sizeof(uint64_t));
    }
    free(res);
    return parsed;
}

int param_list_parse_uchar_list(param_list_ptr pl, const char * key,
    unsigned char * r, size_t n, const char * sep)
{
    char *value;
    if (!get_assoc(pl, key, &value, NULL))
        return 0;
    char *orig_value = value, * end;
    unsigned char * res = malloc(n * sizeof(unsigned char));
    memset(res, 0, n * sizeof(unsigned char));
    size_t parsed = 0;
    for( ;; ) {
        long int tmp = strtol(value, &end, 0);
        ASSERT(tmp <= UCHAR_MAX);
        res[parsed] = tmp;
        if (parsed++ == n)
            break;
        if (parsed && *end == '\0')
            break;
        if (strncmp(end, sep, strlen(sep)) != 0) {
            fprintf(stderr, "Parse error: parameter for key %s"
                    " must match %%d(%s%%d)*; got %s\n",
                    key, sep, orig_value);
            exit(1);
        }
        value = end + strlen(sep);
    }
    if (*end != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d(%s%%d){0,%zu}; got %s\n",
                key, sep, n-1, orig_value);
        exit(1);
    }
    if (r) {
        memcpy(r, res, n * sizeof(unsigned char));
    }
    free(res);
    return parsed;
}

void param_list_parse_int_list_size(param_list_ptr pl, const char * key , int ** r ,
    unsigned int * t)
{
  char *value;
  if (!get_assoc(pl, key, &value, NULL))
    return ;
  char *tmp = value;
  int i = 0;
  * r = (int * ) malloc(sizeof(int) * 1);
  * t = 1;
  for (;;) {
    int ret = sscanf(tmp, "%d", &i);
    if (ret != 1) {
      fprintf (stderr, "Error while parsing coffecient array %s[%d]\n", key, *t
          - 1);
      exit(1);
    }
    (*r)[*t - 1] = i;
    if (*tmp == '-') {
      tmp++;
    }
    while (isdigit(*tmp)) {
      tmp++;
    }
    if(*tmp == '\0') {
      break;
    }
    if (*tmp != ',') {
      fprintf (stderr, "Error while parsing array %s\n", key);
      exit(1);
    }
    tmp++;
    *t = *t + 1;
    * r = realloc(* r, sizeof(int) * (*t));
  }
}

int param_list_parse_double_list(param_list_ptr pl, const char * key,
    double * r, size_t n, const char * sep)
{
    char *value;
    if (!get_assoc(pl, key, &value, NULL))
        return 0;
    char *orig_value = value, * end;
    double * res = (double *) malloc(n * sizeof(double));
    memset(res, 0, n * sizeof(double));
    size_t parsed = 0;
    for( ;; ) {
        double tmp = strtod(value, &end);
        ASSERT(tmp <= UCHAR_MAX);
        res[parsed] = tmp;
        if (parsed++ == n)
            break;
        if (parsed && *end == '\0')
            break;
        if (strncmp(end, sep, strlen(sep)) != 0) {
            fprintf(stderr, "Parse error: parameter for key %s"
                    " must match %%d(%s%%d)*; got %s\n",
                    key, sep, orig_value);
            exit(1);
        }
        value = end + strlen(sep);
    }
    if (*end != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s"
                " must match %%d(%s%%d){0,%zu}; got %s\n",
                key, sep, n-1, orig_value);
        exit(1);
    }
    if (r) {
        memcpy(r, res, n * sizeof(double));
    }
    free(res);
    return parsed;
}

void param_list_parse_mpz_poly(param_list_ptr pl, const char * key,
    mpz_poly_ptr f)
{
  char *value;
  if (!get_assoc(pl, key, &value, NULL))
    return ;
  char *tmp = value;
  mpz_t coeff;
  mpz_init(coeff);
  for(unsigned int j = 0; ; j++) {
    int ret = gmp_sscanf(tmp, "%Zd", coeff);
    if (ret != 1) {
      fprintf (stderr, "Error while parsing coefficients of degree %u "
          "of polynomial %s\n", j, key);
      exit(1);
    }
    mpz_poly_setcoeff(f, j, coeff);
    if (*tmp == '-') {
      tmp++;
    }
    while (isdigit(*tmp)) {
      tmp++;
    }
    if(*tmp == '\0') {
      break;
    }
    if (*tmp != ',') {
      fprintf (stderr, "Error while parsing polynomial %s\n", key);
      exit(1);
    }
    tmp++;
  }
  mpz_clear(coeff);
}

const char * param_list_lookup_string(param_list_ptr pl, const char * key)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return NULL;
    return value;
}

int param_list_parse_mpz(param_list_ptr pl, const char * key, mpz_ptr r)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    unsigned int nread;
    int rc;
    if (r) {
        rc = gmp_sscanf(value, "%Zi%n", r, &nread);
    } else {
        /* scan even when the result is not wanted */
        rc = gmp_sscanf(value, "%*Zi%n", &nread);
    }
    if (rc != 1 || value[nread] != '\0') {
        fprintf(stderr, "Parse error: parameter for key %s is not an mpz: %s\n",
                key, value);
        exit(1);
    }
    return seen;
}

int param_list_parse_switch(param_list_ptr pl, const char * key)
{
    char *value;
    int seen;
    if (!get_assoc(pl, key, &value, &seen))
        return 0;
    if (value != NULL) {
        fprintf(stderr, "Parse error: option %s accepts no argument\n", key);
        exit(1);
    }
    return seen;
}

int param_list_all_consumed(param_list_ptr pl, char ** extraneous)
{
    for(unsigned int i = 0 ; i < pl->size ; i++) {
        if (!pl->p[i]->parsed) {
            pl->p[i]->parsed = 1;
            if (extraneous) {
                *extraneous = pl->p[i]->key;
            }
            return 0;
        }
    }
    return 1;
}

int param_list_warn_unused(param_list_ptr pl)
{
    int u = 0;
    for(unsigned int i = 0 ; i < pl->size ; i++) {
        if (pl->p[i]->origin != PARAMETER_FROM_FILE && !pl->p[i]->parsed) {
            fprintf(stderr, "Warning: unused command-line parameter %s\n",
                    pl->p[i]->key);
            u++;
        }
    }
    return u;
}

void param_list_display(param_list_ptr pl, FILE *f)
{
    param_list_consolidate(pl);
    for(unsigned int i = 0 ; i < pl->size ; i++) {
        fprintf(f,"%s=%s\n", pl->p[i]->key, pl->p[i]->value);
    }
}

void param_list_save(param_list_ptr pl, const char * filename)
{
    FILE * f = fopen(filename, "w");
    if (f == NULL) {
        fprintf(stderr, "fopen(%s): %s\n", filename, strerror(errno));
        exit(1);
    }

    param_list_display(pl, f);
    fclose(f);
}

int param_list_save_parameter(param_list_ptr pl, enum parameter_origin o, 
        const char * key, const char * format, ...)
{
    va_list ap;
    va_start(ap, format);

    char * tmp;
    int rc;
    rc = vasprintf(&tmp, format, ap);
    param_list_add_key(pl, key, tmp, o);
    free(tmp);

    return rc;
}


void param_list_print_command_line(FILE * stream, param_list_ptr pl)
{
    /* remember that the API for calling param_list functions mandates
     * that the binary name $0 is stripped from the provided lists */
    if (pl->cmdline_argv0 == NULL)
        return;

    char **argv = pl->cmdline_argv0-1;
    int argc = pl->cmdline_argc0+1;

    if (verbose_enabled(CADO_VERBOSE_PRINT_CMDLINE)) {
        /* print command line */
        fprintf (stream, "# (%s) %s", cado_revision_string, argv[0]);
        for (int i = 1; i < argc; i++)
            fprintf (stream, " %s", argv[i]);
        fprintf (stream, "\n");
    }
    if (verbose_enabled(CADO_VERBOSE_PRINT_MODIFIED_FILES)) {
        if (strlen(cado_modified_files) > 1)
          fprintf (stream, "# List of modified files in working directory and "
                   "their SHA1 sum:\n%s", cado_modified_files);
    }
    if (verbose_enabled(CADO_VERBOSE_PRINT_COMPILATION_INFO)) {
#ifdef  __GNUC__
        fprintf (stream, "# Compiled with gcc " __VERSION__ "\n");

        /* Apple Clang (based on llvm) identifies itself as a flavour of GNUC 4.2.1
           but seems to compile CADO-NFS properly 
           $ echo | clang -dD -E -pipe -
# 1 "<stdin>"
# 1 "<stdin>" 1
# 1 "<built-in>" 1
# 1 "<built-in>" 3
#define __llvm__ 1
#define __clang__ 1
#define __clang_major__ 4
#define __clang_minor__ 1
#define __clang_patchlevel__ 0
#define __clang_version__ "4.1 ((tags/Apple/clang-421.11.66))"
#define __GNUC_MINOR__ 2
#define __GNUC_PATCHLEVEL__ 1
#define __GNUC__ 4
...
*/

#if GNUC_VERSION_ATLEAST(4,1,2) && GNUC_VERSION_ATMOST(4,2,2) \
        && ! (__llvm__ || __clang__)
        fprintf (stream, "# WARNING: this version of GCC is known to miscompile CADO-NFS. See https://gforge.inria.fr/tracker/index.php?func=detail&aid=14490\n");
#endif
#endif
        fprintf(stream, "# Compilation flags " CFLAGS "\n");
    }
}
