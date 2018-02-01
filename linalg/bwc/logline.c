#include "cado.h"

#include <stdio.h>
#include <stdarg.h>

#include "select_mpi.h"
#include "utils.h"

/* This is intended to provide progress info, and optionally also more
 * detailed progress info when needed. Detailed progress info is assumed
 * to possibly take several lines, while terse progress info is expected
 * to take only one line.
 */

/* Log messages are provided in the form [[x, "text"]], with x an integer
 * indicating a level of detail. 0 is the lowest level of detail, and
 * higher means more detailed (and printed less often).
 *
 * Whenever we output an info which is more detailed than what we had
 * before, and with no finishing newline, then the unfinished previous
 * line is terminated with ..., and the terminating messaged will be
 * issued alone later on.
 *
 * An exception is for level 0 (introduced by logline_begin()), for which
 * the "title" is then echoed when finishing the printing.
 */
struct logline {
    /* Only log messages prefixed with <x> with x <= max will be printed
     */
    int max;
    /* Title string introducing this log set */
    char * header;
    /* Did last print contain a trailing newline ? */
    int eol;
    /* Last printed detail level */
    int lastlevel;

    FILE * f;

    /* Number of newlines seen (used to tell whether the title has to be
     * echoed again) */
    int nnl;

    double start;
};


static struct logline * current;

size_t logline_thresholds[10] = {
    /* This list should be increasing */
    SIZE_MAX,
    SIZE_MAX,
    SIZE_MAX,
    SIZE_MAX,
    SIZE_MAX,
    SIZE_MAX,
    SIZE_MAX,
    SIZE_MAX,
    SIZE_MAX,
    SIZE_MAX,
};

int logline_timings = 1;

int logline_print_all_mpi_nodes = 0;

static double start_time = -1;

void logline_init_timer()
{
    start_time = wct_seconds();
}

int logline_report_wct = 0;

static double logline_timer()
{
    return logline_report_wct ? wct_seconds() : seconds();
}

void logline_decl_usage(param_list_ptr pl)
{
    param_list_decl_usage(pl, "logline_threshold",
            "print log lines of verbosity level i only for sizes greater than i-th item in this comma-separated list");
    param_list_decl_usage(pl, "logline_timings",
            "print timings with log lines");
    param_list_decl_usage(pl, "logline_report_wct",
            "print wct taken by each step marked by log lines");
    param_list_decl_usage(pl, "logline_print_all_mpi_nodes",
            "enable logline printing on all MPI nodes");

}

int logline_interpret_parameters(param_list_ptr pl)
{
    int thr[10];
    int n = param_list_parse_int_list(pl, "logline_threshold", thr, 10, ",");
    for(int i = 0 ; i < n ; i++) {
        logline_thresholds[i] = thr[i];
    }
    param_list_parse_int(pl, "logline_timings", &logline_timings);
    param_list_parse_int(pl, "logline_report_wct", &logline_report_wct);
    param_list_parse_int(pl, "logline_print_all_mpi_nodes", &logline_print_all_mpi_nodes);
    return 0;
}

static void logline_puts_raw(int level, const char * s)
{
    if (!current) return;
    if (level > current->max) return;
    if (level != current->lastlevel && !current->eol) {
        fputs("\n", current->f);
        current->nnl += (current->eol = 1);
    }
    if (logline_timings && current->eol) {
        char buf1[16];
        char buf2[16];
        size_disp_fine(1024UL * Memusage2(), buf1, 10000.0);
        size_disp_fine(1024UL * PeakMemusage(), buf2, 10000.0);
        fprintf(current->f, "[%.3f %.3f %s %s] ", wct_seconds() - start_time, seconds(), buf1, buf2);
    }
    fputs(s, current->f);
    size_t n = strlen(s);
    current->nnl += (current->eol = s[n-1] == '\n');
    current->lastlevel = level;
}


int logline_begin(FILE * f, size_t size, const char * fmt, ...)
{
    va_list ap;
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (rank && !logline_print_all_mpi_nodes) return 0;
    va_start(ap, fmt);
    ASSERT_ALWAYS(!current);
    if (size < logline_thresholds[0]) return 0;
    int level;
    for(level = 0 ; level < 10 && size >= logline_thresholds[level] ; level++);
    if (level == 0) return 0;
    level--;
    current = malloc(sizeof(struct logline));
    memset(current, 0, sizeof(struct logline));
    current->max = level;
    current->lastlevel = 0;
    current->f = f;
    current->start = logline_timer();
    int rc = vasprintf(&(current->header), fmt, ap);
    ASSERT_ALWAYS(rc >= 0);
    current->eol = 1;
    logline_puts_raw(0, current->header);
    va_end(ap);
    return 1;
}

int logline_end(double * rr, const char * fmt, ...)
{
    if (!current) return 0;
    va_list ap;
    va_start(ap, fmt);
    char * text;
    char * text2;
    int rc = vasprintf(&text, fmt, ap);
    ASSERT_ALWAYS(rc >= 0);
    double tt = logline_timer() - current->start;
    rc = asprintf(&text2, "%s [%.2f]\n", text, tt);
    ASSERT_ALWAYS(rc >= 0);
    free(text);
    if (current->nnl)
        logline_puts_raw(0, current->header);
    logline_puts_raw(0, text2);
    free(text2);
    free(current->header);
    free(current);
    current = NULL;
    va_end(ap);
    if (rr) *rr += tt;
    return 1;
}


int logline_vprintf(int level, const char * fmt, va_list ap)
{
    if (!current) return 0;
    char * text;
    int rc = vasprintf(&text, fmt, ap);
    ASSERT_ALWAYS(rc >= 0);
    logline_puts_raw(level, text);
    free(text);
    return 1;
}

int logline_printf(int level, const char * fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logline_vprintf(level, fmt, ap);
    va_end(ap);
    return 1;
}


