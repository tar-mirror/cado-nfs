// data structure for reporting actions during the merge; in standard mode
// (say mono proc), this is just a wrap around for a FILE; otherwise,
// it can be used to register things in an array that will be examined and
// flushed from time to time. See the MPI version for more.
typedef struct{
    char type;
    // '0' for the standard stuff
    FILE *outfile;
    // '1' for MPI
    index_t **history;
    int mark;
    int bufsize; // says it!
} report_t;

extern void report1(report_t *rep, index_signed_t i, index_t j);
extern void reportn(report_t *rep, index_signed_t *ind, int n, index_t j);
