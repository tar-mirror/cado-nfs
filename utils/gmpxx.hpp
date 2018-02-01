#include <gmp.h>
#include <istream>
#include <ostream>
#include <string>

// used to be protected by: !defined(HAVE_GMPXX) && !defined(HAVE_MPIRXX)

/* Provide the C++ I/O functions by ourselves, so that we don't *rely* on
 * (gmp|mpir)xx
 *
 * We make no effort to do this very accurately, though.
 */
extern std::ostream& operator<<(std::ostream& os, mpz_srcptr x);
extern std::ostream& operator<<(std::ostream& os, mpq_srcptr x);
extern std::istream& operator>>(std::istream& is, mpz_ptr x);
extern std::istream& operator>>(std::istream& is, mpq_ptr x);
// #endif

