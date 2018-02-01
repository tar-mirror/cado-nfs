#ifndef GZIP_H_
#define GZIP_H_

#include <stdio.h>
#include <libgen.h>
#include <sys/time.h>
#ifdef  HAVE_GETRUSAGE
#include <sys/resource.h>
#endif

/* Length of preempt buffer. Must be a power of 2. */
#define PREEMPT_BUF (1<<22)

/* Length of one write in preempt buffer. Between 64 and 1024 Ko
   seems best. */
#define PREEMPT_ONE_READ (PREEMPT_BUF>>2)

#ifdef __cplusplus
extern "C" {
#endif

/* Return a unix commands list with antebuffer. Example:
 * antebuffer X file_relation1 | cat -
 * antebuffer X file_relation2.gz file_relation3.gz | gzip -dc -
 * antebuffer X file_relation4.bz2 file_relation5.bz2 | bzip2 -dc -
 * [empty string]

 * antebuffer_cmd is /path/to/antebuffer <X>, where <X> is an integer
 * denoting the size of the antebuffer (24 means 2^24 bytes).
*/
extern char ** prepare_grouped_command_lines (char ** list_of_files);

/* Search the executable in PATH and, if found, return in real_path the
   complete path WITH the executable in the end */
char * search_real_exec_in_path(const char *executable, char *real_path);

/* Search the path of antebuffer and put the complete path + name of the
 * executable "antebuffer" in a static variable used by the gzip.c layer.
 * Arguments are the $0 variable (path from cwd to the current executable
 * file), and path_antebuffer, which is obtained from the command line if
 * it happens to exist. Either, or both, may be NULL. The rule for
 * deriving the path to the antebuffer binary is as follows (first match
 * wins):
 *
 *  - if path_antebuffer is a complete path to an executable program, use
 *  it.
 *  - if executable_filename is non-NULL, use `dirname
 *  $0`/../utils/antebuffer, if that happens to point to a valid
 *  executable filename.
 *  - otherwise, antebuffer is disabled.
 *
 * Configuring to *not* use antebuffer can be done by calling
 * set_antebuffer_path(NULL, NULL), or not calling it at all.
 *
 * The return value is 1 if an executable has been found.
 *
 * This is a configuration function which must be called at most once, and in
 * a monothreaded context.
 */
int set_antebuffer_path (const char *executable_filename, const char *path_antebuffer);

/* There are of course scores of existing basename() codes accessible,
 * starting with POSIX basename. However we fear posible inconsistencies
 * here, so we stick to a simple-and-stupid version, whose specification
 * meets our needs. Here, the returned string is always a substring of
 * the input string, and the latter never undergoes any modification.
 */
extern const char * path_basename(const char * path);

extern int is_supported_compression_format(const char * s);

/* Put in sfx the suffix in s (can be "" or NULL) */
extern void get_suffix_from_filename (char *s, char const **sfx);

/* Takes a filename, possibly ending with any recognized compression
 * extension, and returns the associated file stream. The stream may have
 * been opened by either fopen() of popen(), depending on whether an
 * external decompression program has to be called. If the caller is
 * intersted in knowing, the integer *p_pipeflag is filled with 1 (for
 * popen) or 0 (for fopen). In fact, the caller should care, because this
 * can be used to decide whether to close the stream with pclose or
 * fclose (even if fclose works, it's almost guaranteed to create
 * zombies).
 * If non-NULL, suf is the location of a pointer which is position to the
 * recognized suffix, which has been used to decide on which compression
 * method.
 */
extern FILE * fopen_maybe_compressed2(const char * name, const char * mode, int* p_pipeflag, char const ** suf);
extern FILE * fopen_maybe_compressed(const char * name, const char * mode);

/* This one just looks at the file name, and guesses again whether popen() or
 * fopen() was used. The file stream is then closed with pclose() or
 * fclose() accordingly.  */
extern int fclose_maybe_compressed(FILE *, const char * name);

#ifdef  HAVE_GETRUSAGE
/* Same, but recovers the time taken by the underlying process */
extern int fclose_maybe_compressed2 (FILE * f, const char * name, struct rusage * r);
#endif

#ifdef __cplusplus
}
#endif

#endif	/* GZIP_H_ */
