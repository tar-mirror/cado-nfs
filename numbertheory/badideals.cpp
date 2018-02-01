/* 
 * Authors: Joshua Peignier and Emmanuel Thomé
 */
#include "cado.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iterator>

#include "utils.h"
#include "ant.hpp"

using namespace std;

char ** original_argv;
gmp_randstate_t state;

/* This program is intended to replicate exactly the behaviour of the
 * scripts/badideals.mag program of old.
 *
 * Now, we acknowledge several bizarre things in the form of the
 * .badideals and .badidealinfo file.
 *
 * For reference, here is a test case:
 *
 * > clear; DEBUG:=1; load "scripts/badideals.mag";
 * Loading "scripts/badideals.mag"
 * 2,2:0: 2
 * d,5:0: 2
 * 2,1:1: 2
 * 2,2:1: 2
 * 3,1:1: 2
 * # bad ideals for poly[0]=240*x^4 + 4327846*x^3 - 11463289949*x^2 -
 * 48524924823332*x + 99623823815957215
 * 2 2 4 0 1 -1
 * 2 2 6 0 -1 1
 * 13 2 18 0 1 1
 * 13 2 31 0 1 1
 * 13 2 44 0 1 1
 * 13 2 57 0 1 1
 * 13 2 70 0 1 1
 * 13 2 83 0 1 1
 * 13 2 109 0 1 1
 * 13 2 122 0 1 1
 * 13 2 135 0 1 1
 * 13 2 148 0 1 1
 * 13 2 161 0 1 1
 * 13 2 5 0 1 -1
 * 13 2 96 0 -1 1
 * # bad ideals for poly[1]=840*x^5 - 16210*x^4 + 2610983*x^3 -
 * 2560484269*x^2 - 34656366009*x + 253976285986533
 * 2 1 1 1 1 -1
 * 2 2 4 1 -1 1
 * 2 2 6 1 1 -1
 * 3 2 7 1 1 1
 * 3 2 1 1 1 -1
 * 3 2 4 1 -1 1
 *
 *
 * The bizarre points are the following:
 *
 *  - That we have both a .badideals and a .badidealinfo file is very
 *    odd.
 *    Actually maybe this info would be just as well saved in the .roots file,
 *    perhaps.
 *  - The "side" info in the .badidealinfo file is weird.
 *  - The 13 lines above are inelegant. A more concise way would be:
 *        13 2 5 0 1 -1
 *        13 2 96 0 -1 1
 *        13 1 5 0 1 1
 *    But that would be non commutative, so this would be a clear change
 *    in semantics.
 *  - The file format, for now, is not ready to handle larger-degree
 *    prime ideals.
 */ 


/* Representation of an element of P^1(Z/p^kZ)
 * -------------------------------------------
 *
 * given u with 0 <= u < p^k,  the integer u represents (u:1)
 * given u with p^k <= u < 2*p^k and p|u  the integer p^k+u represents (1:u)
 *
 */

struct badideal {/*{{{*/
    cxx_mpz p;
    /* The r below is a congruence class which is common to all branches
     * specified in the vector below */
    cxx_mpz r;  /* we have 0<=r<p+1 to account for projective ideals */
    int nbad;   /* number of ideals above this (p,r). Always > 1 by
                   definition */
    string comments;    /* Used to debug FM */
    struct branch {
        int k;
        cxx_mpz r;  /* we have 0<=r<2*p^k to account for projective ideals */
        vector<int> v;
    };
    vector<branch> branches;

    badideal(cxx_mpz const& p, cxx_mpz const& r) : p(p), r(r) {}

    ostream& print_dot_badideals_file(ostream & o, int side) const {/*{{{*/
        o << p
          << "," << r
          << ":" << side
          << ": " << nbad << endl;
        return o;
    }/*}}}*/

    ostream& print_dot_badidealinfo_file(ostream& o, int side) const {/*{{{*/
        o << comments;
        for(unsigned int j = 0 ; j < branches.size() ; j++) {
            badideal::branch const& br(branches[j]);
            o << p << " " << br.k << " " << br.r << " " << side;
            for(unsigned int k = 0 ; k < br.v.size() ; k++) {
                o << " " << br.v[k];
            }
            o << endl;
        }
        return o;
    }/*}}}*/
};/*}}}*/

istream& operator>>(istream& is, cxx_mpz_poly& f)/*{{{*/
{
    vector<cxx_mpz> v;
    cxx_mpz a;
    for( ; is >> a ; v.push_back(a)) ;
    mpz_poly_realloc(f, v.size());
    for(unsigned int i = 0 ; i < v.size() ; i++) {
        mpz_set(f->coeff[i], v[i]);
    }
    mpz_poly_cleandeg(f, v.size()-1);
    is.clear();
    return is;
}
/*}}}*/

ostream& operator<<(ostream& o, cxx_mpz_poly const& v)/*{{{*/
{
    /* note that we can't cheat and use cxx_mpz here */
    ostream_iterator<mpz_t> it(o, " ");
    if (v->deg>=0) {
        copy(v->coeff, v->coeff + v->deg, it);
        o << v->coeff[v->deg];
    } else {
        o << "0";
    }
    return o;
}/*}}}*/

vector<pair<cxx_mpz, int> > trial_division(cxx_mpz const& n0, unsigned long B, cxx_mpz & cofactor)/*{{{*/
{
    vector<pair<cxx_mpz, int> > res;
    prime_info pinf;

    prime_info_init (pinf);
    cxx_mpz n = n0;

    for (unsigned long p = 2; p < B; p = getprime_mt (pinf)) {
        if (!mpz_divisible_ui_p(n, p)) continue;
        int k = 0;
        for( ; mpz_divisible_ui_p(n, p) ; mpz_fdiv_q_ui(n, n, p), k++);
        res.push_back(make_pair(cxx_mpz(p), k));
    }
    // cout << "remaining nriminant " << n << "\n";
    cofactor = n;
    prime_info_clear (pinf); /* free the tables */
    return res;
}
/*}}}*/

struct all_valuations_above_p {/*{{{*/
    cxx_mpz_poly f;
    cxx_mpz p;
private:
    cxx_mpq_mat O;
    cxx_mpz_mat M;
    vector<pair<cxx_mpz_mat, int> > F;
    vector<int> inertia;
    pair<cxx_mpz_mat, cxx_mpz> jjinv;
    vector<cxx_mpz_mat> helpers;
    vector<int> val_base;

public:
    all_valuations_above_p(cxx_mpz_poly const& f, cxx_mpz const& p) : f(f), p(p) {/*{{{*/
        O = p_maximal_order(f, p);
        M = multiplication_table_of_order(O, f);
        F = factorization_of_prime(O, f, p, state);
        for(unsigned int k = 0 ; k < F.size() ; k++) {
            cxx_mpz_mat const& fkp(F[k].first);
            inertia.push_back(prime_ideal_inertia_degree(fkp));
            helpers.push_back(valuation_helper_for_ideal(M, fkp, p));
        }
        cxx_mpq_mat jjinv_gen(2, f->deg);
        mpq_set_ui(mpq_mat_entry(jjinv_gen,0,0),1,1);
        mpq_set_ui(mpq_mat_entry(jjinv_gen,1,1),1,1);
        jjinv = ::generate_ideal(O,M,jjinv_gen);

        val_base.assign(f->deg, 0);
        val_base = (*this)(jjinv);
    }/*}}}*/
    vector<int> operator()(pair<cxx_mpz_mat, cxx_mpz> const& Id) const {/*{{{*/
        int w = mpz_p_valuation(Id.second, p);
        vector<int> res;
        for(unsigned int k = 0 ; k < F.size() ; k++) {
            cxx_mpz_mat const& a(helpers[k]);
            int v = valuation_of_ideal_at_prime_ideal(M, Id.first, a, p);
            int e = F[k].second;
            res.push_back(v - w * e - val_base[k]);
        }
        return res;
    }/*}}}*/
    void print_info(ostream& o, int k, cxx_mpz const& r, int side) const {/*{{{*/
        cxx_mpz_mat const& fkp(F[k].first);
        pair<cxx_mpz, cxx_mpz_mat> two = prime_ideal_two_element(O, f, M, fkp);
        /* Write the uniformizer as a polynomial with respect to the
         * polynomial basis defined by f */
        cxx_mpq_mat theta_q;
        {
            mpq_mat_set_mpz_mat(theta_q, two.second);
            mpq_mat_mul(theta_q, theta_q, O);
        }

        /* That's only for debugging, so it's not terribly important.
         * But we may have a preference towards giving ideal info in
         * a more concise way, based on alpha_hat for instance */
        ostringstream alpha;
        alpha << "alpha" << side;
        string uniformizer = write_element_as_polynomial(theta_q, alpha.str());

        int e = F[k].second;
        o << "# I" << k
            << ":=ideal<O" << side << "|" << two.first << "," << uniformizer << ">;"
            << " // f=" << prime_ideal_inertia_degree(fkp)
            << " e="<< e
            << endl;
        o << "# I_" << two.first << "_" << r << "_" << side << "_" << k
            << " " << two.first
            << " " << r
            << " " << side
            << " " << theta_q
            << " // " << prime_ideal_inertia_degree(fkp)
            << " " << e
            << endl;
    }/*}}}*/
    pair<cxx_mpz_mat, cxx_mpz> generate_ideal(cxx_mpq_mat const& gens) const {/*{{{*/
        return ::generate_ideal(O, M, gens);
    }/*}}}*/
    pair<cxx_mpz_mat, cxx_mpz> generate_ideal(cxx_mpz_mat const& gens) const {/*{{{*/
        return ::generate_ideal(O, M, cxx_mpq_mat(gens));
    }/*}}}*/
    /* create ideal I=<p^k,p^k*alpha,v*alpha-u> and decompose I*J */
    vector<int> operator()(int k, cxx_mpz const& r) const {/*{{{*/
        cxx_mpz pk;
        mpz_pow_ui(pk, p, k);
        cxx_mpz_mat Igens(3, f->deg);
        mpz_set(mpz_mat_entry(Igens,0,0),pk);
        if (mpz_cmp(r, pk) < 0) {
            mpz_neg(mpz_mat_entry(Igens,1,0),r);
            mpz_set_ui(mpz_mat_entry(Igens,1,1),1);
        } else {
            mpz_set_si(mpz_mat_entry(Igens,1,0),-1);
            mpz_sub(mpz_mat_entry(Igens,1,1), r, pk);
        }
        /* hell, do I _really_ need p*alpha here ??? */
        mpz_set(mpz_mat_entry(Igens,2,1),pk);
        pair<cxx_mpz_mat, cxx_mpz> I = generate_ideal(Igens);
        return (*this)(I);
    }/*}}}*/
    vector<int> multiply_inertia(vector<int> const& v) const {/*{{{*/
        ASSERT_ALWAYS(v.size() == inertia.size());
        vector<int> res(v.size(),0);
        for(unsigned int i = 0 ; i < v.size() ; i++) {
            res[i] = v[i] * inertia[i];
        }
        return res;
    }/*}}}*/
};/*}}}*/

vector<cxx_mpz> lift_p1_elements(cxx_mpz const& p, int k, cxx_mpz const& x)/*{{{*/
{
    /* Given x which represents an element of P^1(Z/p^kZ), return all the p
     * lifts of x in P^1(Z/p^(k+1)Z), all following the same representation
     * convention (detailed somewhat above)
     */
    /* Note how we have complexity O(p) here ! */
    ASSERT_ALWAYS(k >= 1);
    vector<cxx_mpz> res;
    cxx_mpz pk, pkp1;
    mpz_pow_ui(pk, p, k);
    mpz_mul(pkp1, pk, p);
    cxx_mpz xx = x;
    if (mpz_cmp(xx, pk) >= 0) {
        mpz_sub(xx, xx, pk);
        mpz_add(xx, xx, pkp1);
    }
    for(unsigned int w = 0 ; mpz_cmp_ui(p, w) > 0 ; w++) {
        res.push_back(xx);
        mpz_add(xx, xx, pk);
    }
    return res;
}/*}}}*/

vector<badideal::branch> lift_root(all_valuations_above_p const& A, int k0, cxx_mpz const& Q, vector<int> v)/*{{{*/
{
    vector<badideal::branch> dead_branches_reports;
    vector<pair<cxx_mpz, vector<int> > > live_branches;
    vector<int> live_ideals;
    cxx_mpz const& p(A.p);

    //int k = k0 + 1;

    vector<cxx_mpz> rootlifts = lift_p1_elements(p, k0, Q);

    for(unsigned int i = 0 ; i < rootlifts.size() ; i++) {
        cxx_mpz const& nQ(rootlifts[i]);
        vector<int> newvals = A(k0 + 1, nQ);
        vector<int> alive;
        /* All valuations are expected to be >= the base valuation. For
         * some, it's going to lift higher. Find which ones.  */
        for(unsigned int j = 0 ; j < v.size() ; j++) {
            if (newvals[j] != v[j]) alive.push_back(j);
        }
        if (alive.empty()) {
            badideal::branch br;
            br.k = k0 + 1;
            br.r = nQ;
            br.v = A.multiply_inertia(v);
            dead_branches_reports.push_back(br);
        } else {
            live_branches.push_back(make_pair(nQ, newvals));
        }
        live_ideals.insert(live_ideals.end(), alive.begin(), alive.end());
    }
    if (live_ideals.size() <= 1) {
        /* Then the decision is basically taken */
        vector<int> vv = A.multiply_inertia(v);
        int sumvv = 0;
        for(unsigned int j = 0 ; j < vv.size() ; sumvv+=vv[j++]);
        for(unsigned int j = 0 ; j < live_ideals.size() ; j++) {
            int jj = live_ideals[j];
            vv[jj] = vv[jj] - sumvv;
        }
        badideal::branch br;
        br.k = k0;
        br.r = Q;
        br.v = vv;
        return vector<badideal::branch>(1, br);
    }

    vector<badideal::branch> res = dead_branches_reports;

    for(unsigned int i = 0 ; i < live_branches.size() ; i++) {
        cxx_mpz const& nQ(live_branches[i].first);
        vector<int> const& nv(live_branches[i].second);

        vector<badideal::branch> add = lift_root(A, k0 + 1, nQ, nv);

        res.insert(res.end(), add.begin(), add.end());
    }
    return res;
}/*}}}*/

vector<cxx_mpz> projective_roots_modp(cxx_mpz_poly const& f, cxx_mpz const& p)/*{{{*/
{
    /* p must be prime */
    vector<cxx_mpz> roots;
    mpz_t * rr = new mpz_t[f->deg];
    for(int i = 0 ; i < f->deg ; i++) mpz_init(rr[i]);

    int d = mpz_poly_roots(rr, f, p);
    for(int i = 0 ; i < d ; i++) {
        cxx_mpz a;
        mpz_set(a, rr[i]);
        roots.push_back(a);
    }
    if (mpz_divisible_p(f->coeff[f->deg], p)) {
        roots.push_back(p);
    }
    for(int i = 0 ; i < f->deg ; i++) mpz_clear(rr[i]);
    delete[] rr;
    return roots;
}/*}}}*/

vector<badideal> badideals_above_p(cxx_mpz_poly const& f, int side, cxx_mpz const& p)/*{{{*/
{
    vector<badideal> badideals;

    all_valuations_above_p A(f, p);

    vector<cxx_mpz> roots = projective_roots_modp(f, p);

    for(unsigned int i = 0 ; i < roots.size() ; i++) {
        /* first try to decompose <p,(v*alpha-u)>*J */
        vector<int> vals = A(1, roots[i]);

        vector<int> nonzero;
        for(unsigned int k = 0 ; k < vals.size() ; k++) {
            if (vals[k]) nonzero.push_back(k);
        }
        if (nonzero.size() == 1)
            continue;

        badideal b(p,roots[i]);
        b.nbad = nonzero.size();

        vector<badideal::branch> lifts = lift_root(A, 1, roots[i], vals);

        ostringstream cmt;
        cmt << "# p=" << p << ", r=" << roots[i] << " : " << nonzero.size() << " ideals among " << vals.size() << " are bad\n";
        for(unsigned int j = 0 ; j < nonzero.size() ; j++) {
            A.print_info(cmt, nonzero[j], roots[i], side);
        }
        cmt << "# " << lifts.size() << " branch"
            << (lifts.size() == 1 ? "" : "es") << " found\n";
        b.comments = cmt.str();

        /* compres all branches so that we keep only the valuations in
         * the nonzero indirection table */
        for(unsigned int j = 0 ; j < lifts.size() ; j++) {
            badideal::branch & br(lifts[j]);
            vector<int> w;
            for(unsigned int k = 0 ; k < nonzero.size() ; k++) {
                w.push_back(br.v[nonzero[k]]);
            }
            swap(br.v, w);
            b.branches.push_back(br);
        }
        badideals.push_back(b);
    }
    return badideals;
}/*}}}*/

vector<badideal> badideals_for_polynomial(cxx_mpz_poly const& f, int side)/*{{{*/
{
    vector<badideal> badideals;

    if (f->deg == 1) return badideals;

    ASSERT_ALWAYS(f->deg > 1);

    cxx_mpz disc;
    mpz_poly_discriminant(disc, f);
    mpz_mul(disc, disc, f->coeff[f->deg]);

    /* We're not urged to use ecm here */
    vector<pair<cxx_mpz,int> > small_primes = trial_division(disc, 10000000, disc);

    typedef vector<pair<cxx_mpz,int> >::const_iterator vzci_t;


    for(vzci_t it = small_primes.begin() ; it != small_primes.end() ; it++) {
        vector<badideal> tmp = badideals_above_p(f, side, it->first);
        badideals.insert(badideals.end(), tmp.begin(), tmp.end());
    }

    return badideals;
}/*}}}*/

void badideals_declare_usage(param_list_ptr pl)/*{{{*/
{
    param_list_decl_usage(pl, "badideals", "badideals file");
    param_list_decl_usage(pl, "badidealinfo", "badidealinfo file");
    param_list_decl_usage(pl, "polystr", "polynomial (string)");
    param_list_decl_usage(pl, "poly", "polynomial file");
    param_list_decl_usage(pl, "seed", "random seed");
    param_list_decl_usage(pl, "ell", "ell (for computing default number of maps)");
}/*}}}*/

void usage(param_list_ptr pl, char ** argv, const char * msg = NULL)/*{{{*/
{
    param_list_print_usage(pl, argv[0], stderr);
    if (msg) {
        fprintf(stderr, "%s\n", msg);
    }
    exit(EXIT_FAILURE);
}/*}}}*/


int main(int argc, char * argv[])
{
    param_list pl;
    param_list_init(pl);

    badideals_declare_usage(pl);
    param_list_configure_alias(pl, "polystr", "f");

    original_argv = argv;

    argv++,argc--;
    /* switches, if any. See below */
    /* aliases, if any. See below */

    for( ; argc ; ) {
        if (param_list_update_cmdline(pl, &argc, &argv)) { continue; }
        /* Do perhaps some other things on the argument that haven't
         * been eaten at all. Like check whether it is a valid file to
         * source in order to get more options. See
         * param_list_read_stream and param_list_read_file for that. */
        fprintf (stderr, "Unknown option: %s\n", argv[0]);
        usage(pl, original_argv);
    }

    gmp_randinit_default(state);
    unsigned long seed = 1;
    if (param_list_parse_ulong(pl, "seed", &seed)) {
        gmp_randseed_ui(state, seed);
    }

    typedef vector<badideal>::const_iterator vbci_t;

    const char * tmp;
    if ((tmp = param_list_lookup_string(pl, "polystr")) != NULL) {
        int side = 0;
        cxx_mpz_poly f;
        string stmp(tmp);
        for(unsigned int i = 0 ; i < stmp.size() ; i++) {
            if (stmp[i]==',') stmp[i]=' ';
        }
        istringstream is(stmp);
        if (!(is >> f))
            usage(pl, original_argv, "cannot parse polynomial");

        vector<badideal> badideals = badideals_for_polynomial(f, side);
        cout << "--- .badideals data ---\n";
        for(vbci_t it = badideals.begin() ; it != badideals.end() ; it++) {
            badideal const& b(*it);
            b.print_dot_badideals_file(cout, side);
        }

        cout << "--- .badidealinfo data ---\n";
        cout << "# bad ideals for poly"<<side<<"=" << f.print_poly("x") << endl;
        for(vbci_t it = badideals.begin() ; it != badideals.end() ; it++) {
            badideal const& b(*it);
            b.print_dot_badidealinfo_file(cout, side);
        }
    } else if ((tmp = param_list_lookup_string(pl, "poly")) != NULL) {
        cado_poly cpoly;
        cado_poly_init(cpoly);
        cado_poly_read(cpoly, tmp);
        const char * fbname = param_list_lookup_string(pl, "badideals");
        const char * fbiname = param_list_lookup_string(pl, "badidealinfo");
        if (!fbname || !fbiname)
            usage(pl, original_argv, "-poly requires both -badideals and -badidealinfo");

        ofstream fb(fbname);
        ofstream fbi(fbiname);

        for(int side = 0 ; side < cpoly->nb_polys ; side++) {
            cxx_mpz_poly f(cpoly->pols[side]);
            if (f->deg == 1) continue;
            vector<badideal> badideals = badideals_for_polynomial(f, side);
            for(vbci_t it = badideals.begin() ; it != badideals.end() ; it++) {
                badideal const& b(*it);
                b.print_dot_badideals_file(fb, side);
            }

            fbi << "# bad ideals for poly"<<side<<"=" << f.print_poly("x") << endl;
            for(vbci_t it = badideals.begin() ; it != badideals.end() ; it++) {
                badideal const& b(*it);
                b.print_dot_badidealinfo_file(fbi, side);
            }
        }
        cxx_mpz ell;
        if (param_list_parse_mpz(pl, "ell", ell)) {
            for(int side = 0 ; side < cpoly->nb_polys ; side++) {
                sm_side_info sm;
                sm_side_info_init(sm, cpoly->pols[side], ell);
                cout << "# nmaps" << side << " " << sm->nsm << endl;
                sm_side_info_clear(sm);
            }
        }
        cado_poly_clear(cpoly);
    } else {
        usage(pl, original_argv, "-poly or -polystr are mandatory");
    }

    gmp_randclear(state);
    param_list_clear(pl);
}
