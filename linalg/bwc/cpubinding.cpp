#include "cado.h"

#include <cstddef>      /* see https://gcc.gnu.org/gcc-4.9/porting_to.html */
#include <string>
#include <map>
#include <list>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <iterator>
#include <vector>

#include <hwloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "utils.h"
#include "cpubinding.h"

/* This causes all messages to be immediately printed to stderr, instead
 * of being captured to a string */
#define xxxCPUBINDING_DEBUG

/* All output is prefixed by this. */
#define PRE "cpubinding: "

using namespace std;

void cpubinding_decl_usage(param_list_ptr pl)
{
    param_list_decl_usage(pl, "input-topology-file",
            "simulated topology, only for testing");
    param_list_decl_usage(pl, "input-topology-string",
            "simulated topology, only for testing");
    param_list_decl_usage(pl, "cpubinding", "path to a cpubinding.conf file, or explicit CPU binding string");
}

void cpubinding_lookup_parameters(param_list_ptr pl)
{
    param_list_lookup_string(pl, "cpubinding");
}


/* {{{ sugar */
template<class T> invalid_argument operator<<(invalid_argument const& i, T const& a)
{
    ostringstream os;
    os << i.what() << a;
    return invalid_argument(os.str());
}
template<typename T> istream& parse_and_copy_to_list(istream& is, list<T>& L)
{
    L.clear();
    /* The following is neat, but is does nothing to distinguish eof from
     * parse failure */
#if 0
    istream_iterator<T> w0(is);
    istream_iterator<T> w1;
    copy(w0, w1, back_inserter(L));
#endif
    /* Here we promise to set failbit on failure. */
    string t;
    while (getline(is, t, ' ')) {
        if (t.empty()) continue;
        T v;
        if (istringstream(t) >> v) {
            L.push_back(v);
        } else {
            is.clear();
            is.setstate(is.rdstate() | ios_base::failbit);
            return is;
        }
    }
    /* EOF, but no fail */
    is.clear();
    is.setstate(is.rdstate() | ios_base::eofbit);
    // is.setstate(is.rdstate() | ios_base::failbit);
    // is.setstate(is.rdstate() & ~ios_base::goodbit);
    return is;
}
/* }}} */

/* {{{ class thread_split understands 2-dimension pairs */
class thread_split {
    int t[2];
    public:
    int const& operator[](int i) const { return t[i]; }
    int& operator[](int i) { return t[i]; }
    explicit operator int() const { return t[0] * t[1]; }
    thread_split() { t[0] = t[1] = 1; }
    thread_split(int t0, int t1) { t[0] = t0; t[1] = t1; }
    thread_split(int tt[2]) { t[0] = tt[0]; t[1] = tt[1]; }
    bool operator<(const thread_split& o) const {
        int tt = (int) *this;
        int oo = (int) o;
        return tt < oo || (tt == oo && t[0] < o[0]);
    }
    thread_split operator*(thread_split const& o) const {
        return thread_split(t[0]*o[0], t[1]*o[1]);
    }
    thread_split& operator*=(thread_split const& o) {
        t[0]*=o[0]; t[1]*=o[1]; return *this;
    }
    thread_split operator/(thread_split const& o) const {
        return thread_split(t[0]/o[0], t[1]/o[1]);
    }
    thread_split& operator/=(thread_split const& o) {
        t[0]/=o[0]; t[1]/=o[1]; return *this;
    }
    bool is_divisible_by(thread_split const& o) const {
        return t[0]%o[0]==0 && t[1]%o[1]==0;
    }
    bool operator==(thread_split const& o) const {
        return t[0] == o.t[0] && t[1] == o.t[1];
    }
    inline bool operator!=(thread_split const& o) const {
        return (!operator==(o));
    }

};
ostream& operator<<(ostream& os, thread_split const& t) {
    os << t[0] << 'x' << t[1];
    return os;
}
istream& operator>>(istream& is, thread_split& t) {
    /* a "thr=" may be prepended */
    string s;
    if (!(is>>s)) return is;

    const char * digits = "0123456789";
    string::size_type x00;
    x00 = s.find_first_of(digits);
    if (x00) {
        if (s[x00-1] == '=') {
            /* we might want to do something with s.substr(0, x00-1) */
            // t.key = s.substr(0, x00-1);
        } else {
            is.setstate(ios_base::failbit);
            return is;
        }
    }
    string::size_type x01 = s.find_first_not_of(digits, x00);
    if (x01 == string::npos
            || x01 >= s.size()
            || !(istringstream(s.substr(x00, x01 - x00)) >> t[0]))
    {
        is.setstate(ios_base::failbit);
        return is;
    }

    string::size_type x10 = x01 + 1;
    if (!(istringstream(s.substr(x10)) >> t[1])) {
        is.setstate(ios_base::failbit);
        return is;
    }

    return is;
}
/* }}} */

/* {{{ topology_level is the type describing one level in the processor
 * topology tree */
class topology_level {
public:
    string object;
    int n;
    topology_level() {}
    topology_level(string const& s, int n) : object(s), n(n) {
        if (s == "Socket") object="Package";
    }
    friend istream& operator>>(istream& is, topology_level& t);
    bool operator<(topology_level const& o) const {
        if (object < o.object) return true;
        return (object == o.object && n < o.n);
    }
    bool operator==(topology_level const& o) const { return object == o.object && n == o.n; }
    bool operator!=(topology_level const& o) const { return !operator==(o); }
};
istream& operator>>(istream& is, topology_level& t)
{
    string s;
    if (!(is>>s)) return is;
    string::size_type colon = s.find(':');
    if (colon != string::npos) {
        t.object = s.substr(0, colon);
        if (t.object == "Socket") t.object="Package";
        if (!(istringstream(s.substr(colon+1)) >> t.n)) {
            is.setstate(ios_base::failbit);
        }
    }
    return is;
}
ostream& operator<<(ostream& os, const topology_level& t)
{
    return os << t.object << ":" << t.n;
}
/* }}} */

typedef list<topology_level> synthetic_topology;

/* {{{ dealing with hwloc synthetic topology strings */
synthetic_topology hwloc_synthetic_topology(hwloc_topology_t topology)
{
    /* too bad hwloc itself has no function for this ! */
    hwloc_obj_t obj = hwloc_get_root_obj(topology);

    if (!obj->symmetric_subtree) {
        throw invalid_argument("Cannot output assymetric topology in synthetic format.");
    }

    synthetic_topology result;

    for(unsigned int arity = obj->arity ; arity ; arity = obj->arity) {
        obj = obj->first_child;
        char t[64];
        int d = hwloc_obj_type_snprintf(t, sizeof(t), obj, 1);
        if (d >= (int) sizeof(t))
            throw overflow_error("Too long hwloc type name.");
        result.push_back(topology_level(t, arity));
    }
    return result;
}



std::ostream& operator<<(std::ostream& os, synthetic_topology const& t)
{
    copy(t.begin(), t.end(), ostream_iterator<synthetic_topology::value_type>(os, " "));
    return os;
}
istream& operator>>(istream& is, synthetic_topology& L)
{
    return parse_and_copy_to_list(is, L);
}

/* }}} */

/* {{{ mapping strings are elementary objects of mapping indications */
struct mapping_string {
    string object;
    int group;
    thread_split t;
    mapping_string() : group(0) {}
    mapping_string(string const& type, int g, thread_split const& t) : object(type), group(g), t(t) {
        if (type == "Socket") { object = "Package"; }
    }
    mapping_string(string const& type, int g, int t[2]) : object(type), group(g), t(t) {
        if (type == "Socket") { object = "Package"; }
    }
    mapping_string(string const& type, int g): object(type), group(g) {
        if (type == "Socket") { object = "Package"; }
    }
    bool operator<(const mapping_string& o) const {
        if (object != o.object) return object < o.object;
        if (group != o.group)   return group < o.group;
        return t < o.t;
    }
};
ostream& operator<<(ostream& os, const mapping_string& ms)
{
    os << ms.object;
    if (ms.group) os << "*" << ms.group;
    return os << "=>" << ms.t;
}
istream& operator>>(istream& is, mapping_string& ms)
{
    ms = mapping_string();
    string s;
    if (!(is>>s)) {
        return is;
    }
    string::size_type rel = s.find("=>");
    if (rel == string::npos || !(istringstream(s.substr(rel+2)) >> ms.t)) {
        is.setstate(ios_base::failbit);
    } else {
        string::size_type star = s.find('*');
        if (star == string::npos) {
            ms.object = s.substr(0, rel);
            if (ms.object == "Socket") ms.object="Package";
        } else if (star < rel) {
            ms.object = s.substr(0, star);
            if (ms.object == "Socket") ms.object="Package";
            if (!(istringstream(s.substr(star+1, rel-(star+1))) >> ms.group)) {
                is.setstate(ios_base::failbit);
            }
        } else {
            is.setstate(ios_base::failbit);
        }
    }
    return is;
}

ostream& operator<<(ostream& os, list<mapping_string> const& L)
{
    if (L.empty()) return os;
    copy(L.begin(), --L.end(), ostream_iterator<mapping_string>(os, " "));
    os << L.back();
    return os;
}

istream& operator>>(istream& is, list<mapping_string>& L)
{
    return parse_and_copy_to_list(is, L);
}


/* }}} */

/* {{{ matching strings are just the same, with jokers */
struct matching_string : public topology_level {
    private:
        typedef topology_level super;
    public:
    string joker;
    bool operator<(const matching_string& o) const {
        if (!joker.empty() && !o.joker.empty()) { return joker < o.joker; }
        if (!joker.empty()) { return true; }
        if (!o.joker.empty()) { return true; }
        return (const topology_level&)*this < (const topology_level&)o;
    }
};

istream& operator>>(istream& is, matching_string& ms)
{
    char c;
    ms = matching_string();
    if (!(is>>c)) return is;
    if (c == '@') {
        return is >> ms.joker;
    } else {
        is.putback(c);
        return is >> (topology_level&) ms;
    }
}

ostream& operator<<(ostream& os, const matching_string& ms)
{
    if (!ms.joker.empty()) return os << '@' << ms.joker;
    return os << (const topology_level&) ms;
}

ostream& operator<<(ostream& os, list<matching_string> const& L)
{
    if (L.empty()) return os;
    copy(L.begin(), --L.end(), ostream_iterator<matching_string>(os, " "));
    os << L.back();
    return os;
}

istream& operator>>(istream& is, list<matching_string>& L)
{
    return parse_and_copy_to_list(is, L);
}

/* }}} */

typedef map<list<matching_string>,
            map<thread_split,
                list<mapping_string>>> conf_file;

/* {{{ Dealing with the configuration file */
istream& operator>>(istream& f, conf_file& result)
{
    string s;
    result.clear();

    pair<conf_file::key_type, conf_file::mapped_type> current;

    for(int lnum = 0; getline(f, s) ; lnum++) {
        istringstream is(s);
        thread_split t;
        char c;

        if (!(is>>c)) continue;
        if (c == '#') continue;

        if (c == '[') {
            string::size_type i0, i1;
            for(i0 = 0 ; i0 < s.size() && isspace(s[i0]) ; i0++) ;
            for(i1 = s.size() ; --i1 < s.size() && isspace(s[i1]) ; ) ;
            if (i0 == string::npos) goto conf_file_parse_error;
            if (i1 == string::npos) goto conf_file_parse_error;
            i0++;
            if (i0 >= i1) goto conf_file_parse_error;

            if (!current.first.empty()) {
                if (result.find(current.first) != result.end()) {
                    throw invalid_argument("")
                        << "Found two sections with header " << current.first;
                }
                result.insert(current);
            }

            current = conf_file::value_type();

            if (!(istringstream(s.substr(i0, i1 - i0)) >> current.first)) {
                goto conf_file_parse_error;
            }
            continue;
        }
        is.putback(c);
        if (is >> skipws >> t) {
            string rhs;
            for( ; (is>>c) && isspace(c) ; ) ;
            is.putback(c);
            getline(is, rhs);
            conf_file::mapped_type::mapped_type v;
            if (rhs == "remove") {
                /* then an empty v is fine */
            } else if (!(istringstream(rhs) >> v)) {
                /* try special cases */
                goto conf_file_parse_error;
            }
            current.second.insert(make_pair(t, v));
            continue;
        }

conf_file_parse_error:
        throw invalid_argument("") << "parse error on line " << lnum << ": " << s;
    }
    if (!current.first.empty()) {
        if (result.find(current.first) != result.end()) {
            throw invalid_argument("")
                << "Found two sections with header " << current.first;
        }
        result.insert(current);
    }

    /* reaching EOF is *normal* here ! */
    bool fail = f.rdstate() & ios_base::failbit;
    bool eof = f.rdstate() & ios_base::eofbit;
    if (fail && !eof) {
        return f;
    } else if (eof) {
        f.clear();
    } else {
        f.setstate(f.rdstate() | ios_base::failbit);
        f.setstate(f.rdstate() & ~ios_base::goodbit);
    }
    return f;
}

ostream& operator<<(ostream& os, conf_file const& conf)
{
    for(auto it : conf) {
        os << "[" << it.first << "]" << endl;
        for(auto jt : it.second) {
            os << jt.first << " " << jt.second << endl;
        }
    }
    return os;
}
/* }}} */

/* {{{ the matching code within the conf file */
bool compare_to_section_title(ostream& os, synthetic_topology & topology, list<matching_string> const& title, int& njokers, list<mapping_string>& extra)
{
    njokers = 0;
    extra.clear();

    auto t = topology.begin();
    for(auto s : title) {
        if (t == topology.end()) {
            /* no exact match possible. */
            return false;
        }
        if (s.joker == "merge_caches") {
            while (t->object.find("Cache") != string::npos) {
                int g = t->n;
                t = topology.erase(t);
                if (t == topology.end()) {
                    throw invalid_argument("@merge_caches failure");
                }
                t->n *= g;
            }
            if (t->object.find("Core") == string::npos) {
                os << PRE << "Warning: @merge_caches should encounter Caches above a \"Core\"\n";
                os << PRE << "Warning: got " << *t << " instead, weird.\n";
            }
            njokers++;
            continue;
        } else if (s.joker == "group_PU") {
            if (t->object != "PU")
                return false;
            /* This sets the group argument to t->n */
            extra.push_back(mapping_string("PU", t->n));
            t++;
            njokers++;
            continue;
        } else if (!s.joker.empty()) {
            throw invalid_argument("") << "Bad joker " << s;
        }
        /* now compare the topology level *t with the section title token s */
        if (s != *t) return false;
        t++;
    }

    return t == topology.end();
}

/* }}} */

/* {{{ pinning_group */
class pinning_group {
    /* owned pointers. This whole class is only about ownership, really.
     * The only thing we care about is to not leave stray pointers in the
     * wild. */
    hwloc_bitmap_t cpu;
    hwloc_bitmap_t mem;
public:
    pinning_group() {
        cpu = hwloc_bitmap_alloc();
        mem = hwloc_bitmap_alloc();
    }
    ~pinning_group() {
        hwloc_bitmap_free(cpu);
        hwloc_bitmap_free(mem);
    }
    pinning_group(pinning_group const& o) {
        cpu = hwloc_bitmap_dup(o.cpu);
        mem = hwloc_bitmap_dup(o.mem);
    }
    pinning_group(hwloc_obj_t p) {
        cpu = hwloc_bitmap_dup(p->cpuset);
        mem = hwloc_bitmap_dup(p->nodeset);
    }
    pinning_group const& operator=(pinning_group const& o) {
        if (this == &o) return *this;
        hwloc_bitmap_copy(cpu, o.cpu);
        hwloc_bitmap_copy(mem, o.mem);
        return *this;
    }
    /* define mergeing operations */
    pinning_group operator+(pinning_group const& o) const {
        pinning_group r;
        hwloc_bitmap_or(r.cpu, cpu, o.cpu);
        hwloc_bitmap_or(r.mem, mem, o.mem);
        return r;
    }
    pinning_group& operator+=(pinning_group const& o) {
        hwloc_bitmap_or(cpu, cpu, o.cpu);
        hwloc_bitmap_or(mem, mem, o.mem);
        return *this;
    }
    friend ostream& operator<<(ostream& o, pinning_group const & p);
    int pin(hwloc_topology_t topology, int flags) const {
        return hwloc_set_cpubind(topology, cpu, flags);
    }
};
ostream& operator<<(ostream& o, pinning_group const & p) {
    char * c, * m;
    hwloc_bitmap_list_asprintf(&c, p.cpu);
    hwloc_bitmap_list_asprintf(&m, p.mem);
    o << "cpu:" << c << " ; mem:" << m;
    free(c);
    free(m);
    return o;
}
/* }}} */


class cpubinder {
    /* {{{ pinning_group_matrices */
    /* This is an internal type for stage_mapping, really. We have a list of
     * matrices of pinning groups. All pinning groups are distinct. At the
     * beginning of the stage_mapping processing, they collectively represent
     * the whole system. Later, as chop_off gets called (which might be
     * never), we have a restricted view.
     * The two inner dimensions are fixed by t, the outer one
     * is implied by the length of m.
     */
    class pinning_group_matrices {
        public:
        thread_split t;
        vector<pinning_group> m;
        int outer_dimension() const { return m.size() / (int) t; }
    //private:
        pinning_group_matrices(thread_split const& t) : t(t) {}
    public:
        /* flat constructors */
        pinning_group_matrices() {}
        pinning_group_matrices(pinning_group const& p) : m(1,p) {}
        pinning_group_matrices(vector<pinning_group> const& p) : m(p) {}
        /* i <= t[0], j <= t[1] */
        pinning_group& xs(int k, int i, int j) {
            ASSERT_ALWAYS(k >= 0 && k < outer_dimension());
            ASSERT_ALWAYS(i >= 0 && i < t[0]);
            ASSERT_ALWAYS(j >= 0 && j < t[1]);
            return m[(k*t[0]+i)*t[1]+j];
        }
        const pinning_group& xs(int k, int i, int j) const {
            ASSERT_ALWAYS(k >= 0 && k < outer_dimension());
            ASSERT_ALWAYS(i >= 0 && i < t[0]);
            ASSERT_ALWAYS(j >= 0 && j < t[1]);
            return m[(k*t[0]+i)*t[1]+j];
        }
        pinning_group& xs(int i, int j) { return this->xs(0, i, j); }
        const pinning_group& xs(int i, int j) const { return this->xs(0, i, j); }

        pinning_group_matrices& coarsen(thread_split n) {
            if (outer_dimension() % (int) n) {
                throw invalid_argument("")
                        << "Cannot coarsen a list of " << outer_dimension()
                        << " matrices of pinning groups in blocks of size " << n;
            }
            pinning_group_matrices result(t*n);
            result.m.assign(m.size(), pinning_group());
            for(int k = 0 ; k < outer_dimension() / (int) n ; k++) {
                /* place (int) n blocks */
                for(int n0 = 0 ; n0 < n[0] ; n0++) {
                    for(int n1 = 0 ; n1 < n[1] ; n1++) {
                        int nn = n0 * n[1] + n1;
                        /* place the elementary blocks at the right places */
                        for(int t0 = 0 ; t0 < t[0] ; t0++) {
                            for(int t1 = 0 ; t1 < t[1] ; t1++) {
                                result.xs(k, n0 * t[0] + t0, n1 * t[1] + t1) =
                                    (pinning_group const&) xs(k*(int)n+nn, t0, t1);
                            }
                        }
                    }
                }
            }
            swap(*this, result);
            return *this;
        }
        pinning_group_matrices& chop_off(int n) {
            pinning_group_matrices result(t);
            ASSERT_ALWAYS(outer_dimension() > 0 && outer_dimension() % n == 0);
            for(int k = 0 ; k < outer_dimension() ; k+=n) {
                copy(&xs(k,0,0), &xs(k+1,0,0), back_inserter(result.m));
            }
            swap(*this, result);
            return *this;
        }
    };
    /* }}} */
    ostream& os;
    /* initialized by ctor, freed by dtor */
    hwloc_topology_t topology;

    /* filled by ::read_param_list */
    conf_file cf;
    bool fake;

    /* filled by ::find and ::force */
    synthetic_topology stopo;
    thread_split thr;
    list<mapping_string> mapping;

    /* filled by ::stage */
    thread_split coarse;
    pinning_group_matrices coarse_slots;

    public:
    /* we don't want the hwloc private thing be copied around without
     * notice. */
    cpubinder(cpubinder const&) = delete;
    cpubinder&operator=(cpubinder const&) = delete;
    cpubinder(ostream& os) : os(os) { hwloc_topology_init(&topology); }
    ~cpubinder() { hwloc_topology_destroy(topology); }
    void read_param_list(param_list_ptr pl, int want_conf_file);
    bool find(thread_split const& thr);
    void force(thread_split const& ,const char * desc);
    void set_permissive_binding();
    void stage();
    /* this one may be called in MT context */
    void apply(int i, int j) const;
};


void cpubinder::read_param_list(param_list_ptr pl, int want_conf_file)
{
    /* the first two arguments here are not parsed in the cado-nfs
     * context. It's only used by the helper binary I have for testing
     * the topology matching code */
    const char * topology_file = param_list_lookup_string(pl, "input-topology-file");
    const char * topology_string = param_list_lookup_string(pl, "input-topology-string");
    const char * cpubinding_conf = param_list_lookup_string(pl, "cpubinding");

    /* If we arrive here, then cpubinding_conf is not something which
     * looks like a forced binding, so this should be a config file
     */
    if (cpubinding_conf && want_conf_file) {
        if (ifstream(cpubinding_conf) >> cf) {
            os << PRE << "Read configuration from " << cpubinding_conf << endl;
        } else {
            throw invalid_argument("")
                << "Could not read cpubinding conf file "
                << cpubinding_conf;
        }
        // cerr << "Configuration:\n" << cf << endl;
    }

    unsigned long flags = 0;
#if HWLOC_API_VERSION >= 0x010700
    flags = hwloc_topology_get_flags(topology);
#endif  /* HWLOC_API_VERSION >= 0x010700 */
    /* we must make sure to remove these flags, but it's likely that
     * they're off by default anyway */
    flags &= ~(HWLOC_TOPOLOGY_FLAG_IO_DEVICES | HWLOC_TOPOLOGY_FLAG_IO_BRIDGES);
    hwloc_topology_set_flags(topology, flags);
    /* {{{ retrieve the topology */
    if (topology_file) {
        int rc = hwloc_topology_set_xml(topology, topology_file);
        ASSERT_ALWAYS(rc >= 0);
        fake = true;
    } else if (topology_string) {
        /* hwloc-1.4.1 does not seem to understand "NUMANode" when
         * parsing synthetic strings. With 1.9.1 it works fine. I don't
         * know when exactly that changed.
         * 1.4.1 wants "node" in the synthetic string instead. 1.9.1
         * still groks that. Let's do a transformation for pre-1.9, that
         * should keep us safe.
         */
        int rc = -1;
        {
            istringstream is(topology_string);
            synthetic_topology stopo;
            if (is >> stopo) {
#if HWLOC_API_VERSION < 0x010b00
                for(auto& x: stopo) {
#if HWLOC_API_VERSION < 0x010900
                    if (x.object == "NUMANode") { x.object="node"; }
#endif
                    if (x.object == "Package") { x.object="Socket"; }
                }
#endif
                ostringstream os;
                os << stopo;
                string ss(os.str());
                const char * v = ss.c_str();
                rc = hwloc_topology_set_synthetic(topology, v);
                if (rc < 0)
                    cerr << "hwloc_topology_set_synthetic("<< v <<") [mangled for hwloc<1.9] failed\n";
            } else {
                cerr << "pre-hwloc-1.9 mangling for hwloc_topology_set_synthetic("<< topology_string <<") failed\n";
                rc = -1;
            }
        }
        ASSERT_ALWAYS(rc >= 0);
        fake = true;
    } else {
        fake = false;
    }
    hwloc_topology_load(topology);
    /* }}} */
}

void cpubinder::set_permissive_binding()
{
    /* stopo and thr must have been set */
    os << PRE << "Selecting permissive mapping:"
        << " " << mapping << endl;
    mapping.clear();
    mapping_string top(stopo.front().object, stopo.front().n, thr);
    mapping.push_back(top);
}

/* {{{ finds a mapping for thr. Return true if successful. */
/* This sets the fields [stopo] [thr] [mapping].
 */
bool cpubinder::find(thread_split const& thr)
{
    this->thr = thr;
    stopo = hwloc_synthetic_topology(topology);
    os << PRE << "Hardware: " << stopo << endl;
    os << PRE << "Target split: " << thr << endl;

    struct {
        int n;
        pair<conf_file::key_type, conf_file::mapped_type> it;
        list<mapping_string> e;
        synthetic_topology s;
    } best;
    int nm=0;
    best.n = INT_MAX;
    for(auto it : cf) {
        int n;
        list<mapping_string> e;
        synthetic_topology s = stopo;
        if (compare_to_section_title(os, s, it.first, n, e)) {
            nm++;
            if (n < best.n) {
                best.s = s;
                best.n = n;
                best.it = it;
                best.e = e;
            } else if (n == best.n) {
                os << PRE << "Found two matches with same accuracy level\n";
                os << PRE << "First match:\n" << best.it.first << endl;
                os << PRE << "Second match:\n" << it.first << endl;
                os << PRE << "First match wins.\n";
            }

        }
    }
    if (best.n == INT_MAX)
        return false;
    os << PRE << "config match: " << "[" << best.it.first << "]" << endl;
    if (nm > 1) {
        os << PRE << "(note: " << (nm-1) << "other (possibly looser) matches)\n";
    }
    auto jt = best.it.second.find(thr);
    if (jt == best.it.second.end())
        return false;
    mapping = jt->second;
    if (!mapping.empty()) {
        for( ; !best.e.empty() ; best.e.pop_front()) {
            if (mapping.back().object != best.e.front().object)
                break;
            os << PRE << "not appending " << best.e.front() << " since " << mapping.back().object << " is present\n";
        }
        mapping.splice(mapping.end(), best.e);
    } else {
        os << PRE << "Found \"remove\" mapping in config file." << endl;
        set_permissive_binding();
    }
    stopo = best.s;
    return true;
}
/* }}} */

void cpubinder::force(thread_split const& t, const char * desc)/*{{{*/
{
    thr = t;
    stopo = hwloc_synthetic_topology(topology);
    if (strcmp(desc, "remove") == 0 || strlen(desc) == 0) {
        os << PRE << "As per the provided string \""<<desc<<"\","
            << " applying permissive mapping:"
            << " " << mapping << endl;
        set_permissive_binding();
    } else {
        istringstream(desc) >> mapping;
    }
}
/*}}}*/

/* {{{ stage_mapping */
/* This fills the coarse and coarse_slots fields */
void cpubinder::stage()
{
    /* First gather all PUs in an ordered sequence which matches the
     * topology tree. Note that the next_cousin member function is really
     * perfect for that */
    int depth = hwloc_topology_get_depth(topology);
    /*
    int npu = hwloc_get_nbobjs_by_depth(topology, depth-1);
    int g=1;
    for(auto it = stopo.begin() ; it != stopo.end() ; g*=it++->n) ;
    ASSERT_ALWAYS(g == npu);
    */

    vector<pinning_group> slots;
    for(hwloc_obj_t pu = hwloc_get_obj_by_depth(topology, depth-1, 0) ;
            pu != NULL;
            pu = pu->next_cousin) slots.push_back(pu);


    auto rt = stopo.rbegin()  ;
    auto jt = mapping.rbegin();

    /* rtn is the number of not-yet-assigned (groups of) objects at the
     * current level */
    int rtn = rt->n;

    bool stars = true;

    os << PRE << "Reduced topology: " << stopo << endl;
    os << PRE << "Applying mapping: " << mapping << endl;

    for( ;jt != mapping.rend() ; jt++) {
        if (!jt->group) {
            if (stars)
                coarse_slots = pinning_group_matrices(slots);
            stars = false;
        }
        if (!stars) {
            if (jt->group) {
                throw invalid_argument("") << "Wrongly placed * in " << mapping;
            }
            if ((int) jt->t == 1) continue;
        }
        /* If we have a match, that's good, let's keep it, even if
         * the remaining item of this name in the topology has count
         * one. If we don't have a match, then we're allowed to move
         * up until we find one. If we jump over levels of non-trivial
         * arity in that process, we'll act accordingly.
         */
        int hidden_grouping = 1;
        for( ; rt != stopo.rend() && rt->object != jt->object ; ) {
            if (rtn > 1) {
                /* we have no way to silence this warning at the moment.
                 * Maybe add an explicit syntax like Core/2, or something ?
                 */
                if (stars) {
                    os << PRE << "Warning: while applying " << *jt
                        << ": this implicitly merges " << rtn
                        << " " << rt->object << "-level (groups of) objects\n";
                    hidden_grouping *= rtn;
                } else {
                    os << PRE << "Warning: while applying " << *jt
                        << ": we have only " << rtn
                        << " " << rt->object << " scheduled"
                        << " out of " << rt->n << endl;
                    /* well, do it, then ! */
                    coarse_slots.chop_off(rtn);
                }
            }
            if (++rt == stopo.rend())
                break;

            rtn = rt->n;
        }

        if (rt == stopo.rend())
            throw invalid_argument("")
                << "Hit end of hardware description"
                << " while applying " << *jt;
        bool check;

        if (stars) {
            /* we coarsen the thread group */
            coarse *= jt->t;
            check = rtn % jt->group == 0 && thr.is_divisible_by(coarse);
        } else {
            check = rtn % (int) jt->t == 0;
        }

        if (!check) {
            throw invalid_argument("")
                    << *rt << " ("<<rtn<<" left)"
                    << " is not correct while applying " << *jt;
        }

        if (stars) {
            /* we need to coarsen the PU groups by a factor of [group] */
            for(unsigned int i = 0, j = 0 ; j != slots.size() ; i++) {
                slots[i] = slots[j++];
                for(int k = 1 ; k < jt->group * hidden_grouping ; k++) {
                    slots[i] += slots[j++];
                }
            }
            slots.erase(slots.begin() + slots.size() / (jt->group * hidden_grouping), slots.end());
            /* and we reduce the number of items in the topology */
            rtn /= jt->group;
        } else {
            coarse_slots.coarsen(jt->t);
            rtn /= (int) jt->t;
        }
    }
    if (stars)
        coarse_slots = pinning_group_matrices(slots);
    stars = false;
    for( ; ; ) {
        if (rtn > 1) {
            /* we have no way to silence this warning at the moment.
             * Maybe add an explicit syntax like Core/2, or something ?
             */
            os << PRE << "Warning: completed mapping uses only " << (rt->n/rtn) << " " << rt->object << " out of " << rt->n << endl;
            coarse_slots.chop_off(rtn);
        }
        if (++rt == stopo.rend())
            break;
        rtn = rt->n;
    }

    ASSERT_ALWAYS(coarse_slots.outer_dimension() == 1);

    // os << PRE << "Coarse slots now organized in " << coarse_slots.outer_dimension() << " matrices of size " << coarse_slots.t << endl;

    /* We should have a matrix of thread groups whose dimension is
     * thr/coarse, each grouping coarse threads */

    os << PRE << "Threads organized as " << thr/coarse << " blocks of dimensions " << coarse << endl;

    if (coarse_slots.t != thr/coarse) {
        throw invalid_argument("") << "mapping does achieve desired split"
            << " (" << coarse_slots.t*coarse << ", wanted " << thr << ")\n";
    }
    for(int i = 0 ; i < thr[0] ; i+=coarse[0]) {
        for(int j = 0 ; j < thr[1] ; j+=coarse[1]) {
            ostringstream oos;
            if ((int) coarse > 1) {
                oos << "threads ("<<i<<","<<j<<")"
                    << " to ("<<i+coarse[0]-1<<","<<j+coarse[1]-1<<")";
            } else {
                oos << "thread ("<<i<<","<<j<<")";
            }
            pinning_group p = coarse_slots.xs(i/coarse[0], j/coarse[1]);
            os << PRE << "" << oos.str() << " -> " << p << endl;
        }
    }
    if (fake)
        os << PRE << "NOTE: since this is a fictitious hardware description, pinning will not be done for real\n";
    os << PRE << "Done.\n";
}
/* }}} */



/* This returns an opaque pointer to data which will be used to perform
 * the actual cpu binding. This function must be called in
 * single-threaded context.
 * 
 * This returns NULL if cpubinding failed.
 */

void * cpubinding_get_info(char ** messages, param_list_ptr pl, int ttt[2])
{
    const char * conf = param_list_lookup_string(pl, "cpubinding");
    thread_split thr(ttt);

    if (conf && (strcmp(conf, "no") == 0 || strcmp(conf, "none")==0)) {
        if (messages) {
            *messages = strdup("cpubinding disabled by cmdline switch\n");
        }
        return NULL;
    }

#ifdef  CPUBINDING_DEBUG
    ostream& os(cerr);
#else   /* CPUBINDING_DEBUG */
    ostringstream os;
#endif  /* CPUBINDING_DEBUG */

    cpubinder * cb = new cpubinder(os);

    int force = conf && (strstr(conf, "=>") || strcmp(conf, "remove") == 0 || strlen(conf) == 0);

    try {
        cb->read_param_list(pl, !force);
        if (force) {
            cb->force(thr, conf);
            cb->stage();
        } else if (cb->find(thr)) {
            cb->stage();
        } else {
            os << PRE << "no mapping found\n";
            delete cb;
            cb = NULL;
        }
    } catch (invalid_argument const& e) {
        os << PRE << "Failed on error:\n"
            << PRE << "  " << e.what() << "\n";
        delete cb;
        cb = NULL;
    }

    if (messages) {
#ifndef CPUBINDING_DEBUG
        *messages = strdup(os.str().c_str());
#else
        *messages = NULL;
#endif  /* CPUBINDING_DEBUG */
    }
    return static_cast<void*>(cb);
}

void cpubinder::apply(int i, int j) const
{
    ASSERT_ALWAYS(i < thr[0]);
    ASSERT_ALWAYS(j < thr[1]);
    int ti = i / coarse[0];
    int tj = j / coarse[1];
    pinning_group p = coarse_slots.xs(ti, tj);
    // cout << "Pinning thread ("<<i<<","<<j<<") to " << p << "\n";
    int rc = p.pin(topology, HWLOC_CPUBIND_THREAD);
    if (rc < 0) {
        cerr << "Pinning thread ("<<i<<","<<j<<") to " << p << ": " << strerror(errno) << endl;
    }
}

/* perform the actual pinning. This must be called for each thread */
void cpubinding_do_pinning(void * pinning_info_pre, int i, int j)
{
    if (pinning_info_pre == NULL) return;
    cpubinder* cb = static_cast<cpubinder*>(pinning_info_pre);

    cb->apply(i, j);
}

/* free the opaque pointer */
void cpubinding_free_info(void * pinning_info_pre, int[2])
{
    if (pinning_info_pre == NULL) return;
    cpubinder* cb = static_cast<cpubinder*>(pinning_info_pre);
    delete cb;
}
