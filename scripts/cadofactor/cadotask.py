#!/usr/bin/env python3
import re
import os.path
from fractions import gcd
import abc
import random
import time
import datetime
from collections import OrderedDict, defaultdict
from itertools import zip_longest
from math import log, sqrt
import logging
import socket
import gzip
import heapq
import patterns
import wudb
import cadoprograms
import cadoparams
import cadocommand
import wuserver
import workunit
from struct import error as structerror
from shutil import rmtree
from workunit import Workunit
# Patterns for floating-point numbers
# They can be used with the string.format() function, e.g.,
# re.compile("value = {cap_fp}".format(**REGEXES))
# where format() replaces "{cap_fp}" with the string in CAP_FP
RE_FP = r"[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?"
CAP_FP = "(%s)" % RE_FP
REGEXES = {"fp": RE_FP, "cap_fp": CAP_FP}

def re_cap_n_fp(prefix, n, suffix=""):
    """ Generate a regular expression that starts with prefix, then captures
    1 up to n floating-point numbers (possibly in scientific notation)
    separated by whitespace, and ends with suffix.
    
    >>> re.match(re_cap_n_fp("foo", 2), 'foo 1.23').group(1)
    '1.23'
    >>> re.match(re_cap_n_fp("foo", 2), 'foo1.23   4.56').groups()
    ('1.23', '4.56')
    
    # The first fp pattern must match something
    >>> re.match(re_cap_n_fp("foo", 2), 'foo')
    """
    template = prefix
    if n > 0:
        # The first CAP_FP pattern is mandatory, and can have zero or more
        # whitespace in front
        template += "\s*{cap_fp}"
        # The remaining FP_CAPs are optional, and have 1 or more whitespace
        template += "(?:\s+{cap_fp})?" * (n - 1)
    template += suffix
    return template.format(**REGEXES)


class Polynomial(list):
    """
    >>> p = Polynomial()
    >>> p.degree < 0
    True
    >>> p[0] = 1
    >>> p.degree == 0
    True
    >>> p[42] = 1
    >>> p.degree == 42
    True
    >>> p[42] = 0
    >>> p.degree == 0
    True
    >>> p = Polynomial([3,2,1]) # x^2 + 2*x + 3
    >>> p.eval(0)
    3
    >>> p.eval(1)
    6
    >>> p.eval(2)
    11
    >>> p.eval(-3)
    6
    >>> p.eval_h(2,7)
    179
    >>> p.eval_h(-3,5)
    54
    """
    @property
    def degree(self):
        return len(self) - 1 if len(self) > 0 else float("-inf")

    def __setitem__(self, index, value):
        if index >= len(self):
            self.extend([0]*(index + 1 - len(self)))
        list.__setitem__(self, index, value)
        # Remove leading zeroes
        while len(self) > 0 and self[-1] == 0:
            self.pop()

    def __str__(self):
        xpow = ["", "*x"] + ["*x^%d" % i for i in range(2, len(self))]
        arr = ["%+d%s" % (self[idx], xpow[idx]) for idx in range(0, len(self))
               if self[idx]]
        poly = "".join(reversed(arr)).lstrip('+')
        poly = re.sub(r'\b1\*', "", poly)
        return poly

    def eval(self, x):
        """ Evaluate the polynomial at x """
        if len(self) == 0:
            return 0
        deg = self.degree
        value = self[deg]
        for i in range(deg):
            value = value * x + self[deg - i - 1]
        return value

    def eval_h(self, a, b):
        """ Evaluate homogenized bi-variate polynomial at a,b  """
        if len(self) == 0:
            return 0
        powers_a = [a**i for i in range(self.degree + 1)]
        powers_b = [b**i for i in range(self.degree + 1)]
        return sum([coeff * pow_a * pow_b for (coeff, pow_a, pow_b)
                    in zip(self, powers_a, reversed(powers_b))])

    def same_lc(self, other):
        """ Return true if the two polynomials have the same degree
        and leading coefficient
        """
        return self.degree == other.degree and \
                self[self.degree] == other[other.degree]

class PolynomialParseException(Exception):
    """ Exception class for signaling errors during polynomial parsing """
    pass


class Polynomials(object):
    r""" A class that represents a polynomial
    
    >>> Polynomials([""])
    Traceback (most recent call last):
    cadotask.PolynomialParseException: No polynomials found
    >>> t="n: 1021\nc0: 1\nc5: -1\nc5: 1\nY0: 4\nY1: -1\nskew: 1.0\n"
    >>> p=Polynomials(t.splitlines())
    Traceback (most recent call last):
    cadotask.PolynomialParseException: Line 'c5: 1' redefines coefficient of x^5
    >>> t="n: 1021\nc0: 1\nc1: -1\nc5: 1\nY0: 4\nY1: -1\nskew: 1.0\n"
    >>> p=Polynomials(t.splitlines())
    >>> str(p)
    'n: 1021\nskew: 1.0\nc0: 1\nc1: -1\nc5: 1\nY0: 4\nY1: -1\n# f(x) = x^5-x+1\n# g(x) = -x+4\n'
    >>> t="n: 1021\nc0: -1\nc1: 1\nc5: -1\nY0: -4\nY1: 1\nskew: 1.0\n"
    >>> p=Polynomials(t.splitlines())
    >>> str(p)
    'n: 1021\nskew: 1.0\nc0: -1\nc1: 1\nc5: -1\nY0: -4\nY1: 1\n# f(x) = -x^5+x-1\n# g(x) = x-4\n'
    >>> t="n: 1021\npoly0: 1, 2, 3\npoly1: 4, 5, 6\nskew: 1.0\n"
    >>> p=Polynomials(t.splitlines())
    """

    re_pol_f = re.compile(r"c(\d+)\s*:\s*(-?\d+)")
    re_pol_g = re.compile(r"Y(\d+)\s*:\s*(-?\d+)")
    re_polys = re.compile(r"poly(\d+)\s*:") # FIXME: do better?
    re_Murphy = re.compile(re_cap_n_fp(r"\s*#\s*MurphyE\s*\((.*)\)\s*=", 1))
    # the 'lognorm' variable now represents the expected E-value
    re_lognorm = re.compile(re_cap_n_fp(r"\s*#\s*exp_E", 1))
    
    # Keys that can occur in a polynomial file, in their preferred ordering,
    # and whether the key is mandatory or not. The preferred ordering is used
    # when turning a polynomial back into a string.
    keys = OrderedDict(
        (
            ("n", (int, True)),
            ("skew", (float, False)),
            ("type", (str, False))
        ))
    
    def __init__(self, lines):
        """ Parse a polynomial file in the syntax as produced by polyselect
            and polyselect_ropt
        """
        self.MurphyE = 0.
        self.MurphyParams = None
        self.lognorm = 0.
        self.params = {}
        polyf = Polynomial()
        polyg = Polynomial()
        # in case of multiple fields
        tabpoly = {}

        def match_poly(line, poly, regex):
            match = regex.match(line)
            if match:
                (idx, coeff) = map(int, match.groups())
                if idx <= poly.degree and poly[idx]:
                    raise PolynomialParseException(
                        "Line '%s' redefines coefficient of x^%d"
                        % (line, idx))
                poly[idx] = coeff
                return True
            return False

        # line = "poly0: 1, 2, 3" => poly[0] = {1, 2, 3} = 1+2*X+3*X^2
        def match_poly_all(line, regex):
            match = regex.match(line)
            if match:
                line2 = line.split(":")
                # get index of poly
                ip = int(line2[0].split("poly")[1])
                # get coeffs of 1+2*X+3*X^2
                line3=line2[1].split(",")
                pol = Polynomial()
                for idx in range(len(line3)):
                    pol[idx] = int(line3[idx]);
                return ip, pol
            return -1, []

        for line in lines:
            # print ("Parsing line: >%s<" % line.strip())
            # If this is a comment line telling the Murphy E value,
            # extract the value and store it
            match = self.re_Murphy.match(line)
            if match:
                if self.MurphyParams or self.MurphyE:
                    raise PolynomialParseException(
                        "Line '%s' redefines Murphy E value" % line)
                self.MurphyParams = match.group(1)
                self.MurphyE = float(match.group(2))
                continue
            # If this is a comment line telling the expected E-value,
            # extract the value and store it
            match = self.re_lognorm.match(line)
            if match:
                if self.lognorm != 0:
                    raise PolynomialParseException(
                        "Line '%s' redefines exp_E value" % line)
                self.lognorm = float(match.group(1))
                continue
            # Drop comment, strip whitespace
            line2 = line.split('#', 1)[0].strip()
            # If nothing is left, process next line
            if not line2:
                continue
            # Try to parse polynomial coefficients
            if match_poly(line, polyf, self.re_pol_f) or \
                    match_poly(line, polyg, self.re_pol_g):
                continue
            # is it in format "poly*: ..."
            ip,tip=match_poly_all(line, self.re_polys)
            if ip != -1:
                tabpoly[ip] = tip
                continue
            # All remaining lines must be of the form "x: y"
            array = line2.split(":")
            if not len(array) == 2:
                raise PolynomialParseException("Invalid line '%s'" % line)
            key = array[0].strip()
            value = array[1].strip()
            
            if not key in self.keys:
                raise PolynomialParseException("Invalid key '%s' in line '%s'" %
                                               (key, line))
            if key in self.params:
                raise PolynomialParseException("Key %s in line %s has occurred "
                                               "before" % (key, line))
            (_type, isrequired) = self.keys[key]
            self.params[key] = _type(value)

        # If no polynomial was found at all (not even partial data), assume
        # that polyselect simply did not find anything in this search range
        if polyf.degree < 0 and polyg.degree < 0 and self.params == {} and \
                self.MurphyE == 0.:
            raise PolynomialParseException("No polynomials found")

        # Test that all required keys are there
        for (key, (_type, isrequired)) in self.keys.items():
            if isrequired and not key in self.params:
                raise PolynomialParseException("Key %s missing" % key)
        if len(tabpoly) > 0:
            polyg = tabpoly[0]
            polyf = tabpoly[1]
        self.polyf = polyf
        self.polyg = polyg
        self.tabpoly = tabpoly
        return

    def __str__(self):
        arr = ["%s: %s\n" % (key, self.params[key])
               for key in self.keys if key in self.params]
        if len(self.tabpoly) > 0:
            for i in range(len(self.tabpoly)):
                poltmp = self.tabpoly[i]
                arr += ["poly%d: %s" % (i, poltmp[0])]
                arr += [","+str(poltmp[j]) for j in range(1, len(poltmp))]
                arr += "\n"
        else:
            arr += ["c%d: %d\n" % (idx, coeff) for (idx, coeff)
                    in enumerate(self.polyf) if not coeff == 0]
            arr += ["Y%d: %d\n" % (idx, coeff) for (idx, coeff)
                    in enumerate(self.polyg) if not coeff == 0]
        if not self.MurphyE == 0.:
            if self.MurphyParams:
                arr.append("# MurphyE (%s) = %g\n" % (self.MurphyParams, self.MurphyE))
            else:
                arr.append("# MurphyE = %g\n" % self.MurphyE)
        if not self.lognorm == 0.:
            arr.append("# exp_E %g\n" % self.lognorm)
        if len(self.tabpoly) > 0:
            for i in range(len(self.tabpoly)):
                arr.append("# poly%d = %s\n" % (i, str(self.tabpoly[i])))
        else:
            arr.append("# f(x) = %s\n" % str(self.polyf))
            arr.append("# g(x) = %s\n" % str(self.polyg))
        return "".join(arr)

    def __eq__(self, other):
        return self.polyf == other.polyf and self.polyg == other.polyg \
            and self.params == other.params

    def __ne__(self, other):
        return not (self == other)

    def create_file(self, filename):
        # Write polynomial to a file
        with open(str(filename), "w") as poly_file:
            poly_file.write(str(self))

    def getN(self):
        return self.params["n"]

    def same_lc(self, other):
        """ Returns true if both polynomial pairs have same degree and
        leading coefficient
        """
        return self.polyf.same_lc(other.polyf) and \
               self.polyg.same_lc(other.polyg)

    def get_polynomial(self, side):
        """ Returns one of the two polynomial as indexed by side """
        assert side == 0 or side == 1
        # Welp, f is side 1 and g is side 0 :(
        if side == 0:
            return self.polyg
        else:
            return self.polyf


class FilePath(object):
    """ A class that represents a path to a file, where the path should be
    somewhat relocateable.
    
    In particular, we separate the path to the working directory, and the file
    path relative to the working directory. For persistent storage in the DB,
    the path relative to the workdir should be used, whereas for any file
    accesses, the full path needs to be used.
    It also piggy-backs a version information field.
    """

    def __init__(self, workdir, filepath, version=None):
        self.workdir = workdir.rstrip(os.sep)
        self.filepath = filepath
        self.version = version

    def __str__(self):
        return "%s%s%s" % (self.workdir, os.sep, self.filepath)

    def get_wdir_relative(self):
        return self.filepath

    def isfile(self):
        return os.path.isfile(str(self))

    def isdir(self):
        return os.path.isdir(str(self))

    def get_version(self):
        return self.version

    def mkdir(self, *, parent=False, mode=None):
        """ Creates a directory.
        
        parent acts much like the Unix mkdir's '-p' parameter: required parent
        directories are created if they don't exist, and no error is raised
        if the directory to be created already exists.
        If parent==True, a mode for the directory to be created can be specified
        as well.
        """
        if parent:
            # os.makedirs specifies 0o777 as the default value for mode,
            # thus we can't pass None to get the default value. We also
            # want to avoid hard-coding 0x777 as the default in this
            # method's signature, or using **kwargs magic. Thus we use
            # a default of None in this method, and pass the mode value
            # to makedirs only if it is not None.
            if mode is None:
                os.makedirs(str(self), exist_ok=True)
            else:
                os.makedirs(str(self), exist_ok=True, mode=mode)
        else:
            os.mkdir(str(self))
    def realpath(self):
        return os.path.realpath(str(self))
    def open(self, *args, **kwargs):
        return open(str(self), *args, **kwargs)
    def rmtree (self, ignore_errors=False):
        rmtree(str(self), ignore_errors)


class WorkDir(object):
    """ A class that allows generating file and directory names under a
    working directory.
    
    The directory layout is as follows:
    The current project (i.e., the factorization) has a jobname, e.g.,
    "RSA512". Each task may have a name, e.g., "sieving".
    A task can create various files under
    workdir/jobname.taskname.file
    or put them in a subdirectory
    workdir/jobname.taskname/file
    or, for multiple subdirectories,
    workdir/jobname.taskname/subdir/file

    It is also ok for tasks to have no particular name that is
    reflected in the filename hierarchy.
    
    >>> f = WorkDir("/foo/bar", "jobname", "taskname")
    >>> str(f.make_dirname("foo")).replace(os.sep,'/')
    '/foo/bar/jobname.foo/'
    >>> str(f.make_filename('file')).replace(os.sep,'/')
    '/foo/bar/jobname.file'
    >>> str(f.make_filename('file', subdir="foo")).replace(os.sep,'/')
    '/foo/bar/jobname.foo/file'
    >>> str(f.make_filename('file', prefix="bar", subdir='foo')).replace(os.sep,'/')
    '/foo/bar/jobname.foo/jobname.bar.file'
    """
    def __init__(self, workdir, jobname=None, taskname=None):
        self.workdir = str(workdir).rstrip(os.sep)
        self.jobname = jobname
        self.taskname = taskname
    
    def path_in_workdir(self, filename, version=None):
        return FilePath(self.workdir, filename, version=version)
    
    def make_filename2(self, jobname=None, taskname=None, filename=None):
        if jobname is None:
            jobname = self.jobname
        if taskname is None:
            taskname = self.taskname
        filename_arr = [s for s in [jobname, taskname, filename] if s]
        return FilePath(self.workdir, ".".join(filename_arr))
    
    def make_dirname(self, subdir):
        """ Make a directory name of the form workdir/jobname.prefix/ """
        return self.path_in_workdir("".join([self.jobname, ".", subdir, os.sep]))
    
    def make_filename(self, name, prefix=None, subdir=None):
        """ If subdir is None, make a filename of the form
        workdir/jobname.prefix.name or workdir/jobname.name depending on
        whether prefix is None or not.
        If subdir is not None, make a filename of the form
        workdir/jobname.subdir/jobname.prefix.name
        or workdir/jobname.subdir/name
        """
        components=[self.jobname]
        if subdir is not None:
            components += [ ".", subdir, os.sep]
            if prefix is not None:
                components += [ self.jobname, ".", prefix, "." ]
            components += [ name ]
        else:
            if prefix is not None:
                    components += [ ".", prefix ]
            components += [ ".", name ]
        return self.path_in_workdir("".join(components))

    def get_workdir_jobname(self):
        return self.jobname

    def get_workdir_path(self):
        return self.workdir

class Statistics(object):
    """ Class that holds statistics on program execution, and can merge two
    such statistics.
    """
    def __init__(self, conversions, formats):
        self.conversions = conversions
        self.stat_formats = formats
        self.stats = {}
    
    @staticmethod
    def typecast(values, types):
        """ Cast the values in values to the types specified in types """
        if type(types) is type:
            return [types(v) for v in values]
        else:
            return [t(v) for (v, t) in zip(values, types)]
    
    @staticmethod
    def _to_str(stat):
        """ Convert one statistic to a string """
        return " ".join(map(str, stat))
    
    @staticmethod
    def _from_str(string, types):
        """ Convert a string (probably from a state dict) to a statistic """
        return Statistics.typecast(string.split(), types)
    
    def from_dict(self, stats):
        """ Initialise values in self from the strings in the "stats"
        dictionary
        """
        for (key, types, defaults, combine, regex, allow_several) in self.conversions:
            if key in stats:
                if key in self.stats:
                    print("duplicate %s\n" % key)
                assert not key in self.stats
                self.stats[key] = self._from_str(stats.get(key, defaults),
                                                 types)
                assert not self.stats[key] is None
    
    def parse_line(self, line):
        """ Parse one line of program output and look for statistics.
        
        If they are found, they are added to self.stats.
        """
        for (key, types, defaults, combine, regex, allow_several) in self.conversions:
            match = regex.match(line)
            if match:
                # print (pattern.pattern, match.groups())
                # Optional groups that did not match are returned as None.
                # Skip over those so typecast doesn't raise TypeError
                groups = [group for group in match.groups() if not group is None]
                new_val = self.typecast(groups, types)
                if not allow_several:
                    assert not key in self.stats
                    self.stats[key] = new_val
                else:
                    # Some output files inherently have several values.
                    # This is the case of bwc output files if we use
                    # multiple sequences.
                    if key in self.stats:
                        self.stats[key] = combine(self.stats[key], new_val)
                    else:
                        self.stats[key] = new_val
                assert not self.stats[key] is None
    
    def merge_one_stat(self, key, new_val, combine):
        if key in self.stats:
            self.stats[key] = combine(self.stats[key], new_val)
        else:
            self.stats[key] = new_val
        assert not self.stats[key] is None
        # print(self.stats)
    
    def merge_stats(self, new_stats):
        """ Merge the stats currently in self with the Statistics in
        "new_stats"
        """
        
        assert self.conversions == new_stats.conversions
        for (key, types, defaults, combine, regex, allow_several) in self.conversions:
            if key in new_stats.stats:
                self.merge_one_stat(key, new_stats.stats[key], combine)
    
    def as_dict(self):
        return {key: self._to_str(self.stats[key]) for key in self.stats}
    
    def as_strings(self):
        """ Convert statistics to lines of output
        
        The self.stat_formats is an array, with each entry corresponding to
        a line that should be output.
        Each such entry is again an array, containing the format strings that
        should be used for the conversion of statistics. If a conversion
        fails with a KeyError or an IndexError, it is silently skipped over.
        This is to allow producing lines on which some statistics are not
        printed if the value is not known.
        """
        result = []
        for format_arr in self.stat_formats:
            line = []
            for format_str in format_arr:
                try:
                    line.append(format_str.format(**self.stats))
                except (KeyError, IndexError):
                    pass
            if line:
                result.append("".join(line))
        return result
    
    # Helper functions for processing statistics.
    # We can't make them @staticmethod or references are not callable
    def add_list(*lists):
        """ Add zero or more lists elementwise.
        
        Short lists are handled as if padded with zeroes.
        
        >>> Statistics.add_list([])
        []
        >>> Statistics.add_list([1])
        [1]
        >>> Statistics.add_list([1,2], [3,7])
        [4, 9]
        >>> Statistics.add_list([1,2], [3,7], [5], [3,1,4,1,5])
        [12, 10, 4, 1, 5]
        """
        return [sum(items) for items in zip_longest(*lists, fillvalue=0)]
    
    def weigh(samples, weights):
        return [sample * weight for (sample, weight) in zip(samples, weights)]
    
    def combine_mean(means, samples):
        """ From two lists, one containing values and the other containing
        the respective sample sizes (i.e., weights of the values), compute
        the combined mean (i.e. the weighted mean of the values).
        The two lists must have equal length.
        """
        assert len(means) == len(samples)
        total_samples = sum(samples)
        weighted_sum = sum(Statistics.weigh(means, samples))
        return [weighted_sum / total_samples, total_samples]
    
    def zip_combine_mean(*lists):
        """ From a list of 2-tuples, each tuple containing a value and a
        weight, compute the weighted average of the values.
        """
        for l in lists:
            assert len(l) == 2
        (means, samples) = zip(*lists)
        return Statistics.combine_mean(means, samples)
    
    def combine_stats(*stats):
        """ Computes the combined mean and std.dev. for the stats
        
        stats is a list of 3-tuples, each containing number of sample points,
        mean, and std.dev.
        Returns a 3-tuple with the combined number of sample points, mean,
        and std. dev.
        """
        
        # Samples is a list containing the first item (number of samples) of
        # each item of stats, means is list of means, stddevs is list of
        # std. dev.s
        for s in stats:
            assert len(s) == 3
        
        (samples, means, stddevs) = zip(*stats)
        
        (total_mean, total_samples) = Statistics.combine_mean(means, samples)
        # t is the E[X^2] part of V(X)=E(X^2) - (E[X])^2
        t = [mean**2 + stddev**2 for (mean, stddev) in zip(means, stddevs)]
        # Compute combined variance
        total_var = Statistics.combine_mean(t, samples)[0] - total_mean**2
        return [total_samples, total_mean, sqrt(total_var)]
    
    def test_combine_stats():
        """ Test function for combine_stats()
        
        >>> Statistics.test_combine_stats()
        True
        """
        
        from random import randrange
        
        def mean(x):
            return float(sum(x))/float(len(x))
        def var(x):
            E = mean(x)
            return mean([(a-E)**2 for a in x])
        def stddev(x):
            return sqrt(var(x))
        
        # Generate between 1 and 5 random integers in [1,100]
        lengths = [randrange(100) + 1 for i in range(randrange(5) + 1)]
        lengths = [1, 10]
        # Generate lists of random integers in [1,100]
        lists = [[randrange(100) for i in range(l)] for l in lengths]
        stats = [(length, mean(l), stddev(l))
            for (length, l) in zip(lengths, lists)]
        
        combined = []
        for l in lists:
            combined += l
        
        combined1 = Statistics.combine_stats(*stats)
        combined2 = [len(combined), mean(combined), stddev(combined)]
        if abs(combined1[2] - combined2[2]) > 0.2 * combined2[2]:
            print("lists = %r" % lists)
            print("combineds = %r" % combined)
            print("stats = %r" % stats)
            print("combined1 = %r" % combined1)
            print("combined2 = %r" % combined2)
            print(combined1[2], combined2[2])
            print(abs(combined1[2] / combined2[2] - 1))
        return combined1[0] == combined2[0] and \
                abs(combined1[1] / combined2[1] - 1) < 1e-10 and \
                abs(combined1[2] - combined2[2]) <= 1e-10 * combined2[2]
    
    def smallest_n(*lists, n=10):
        concat = []
        for l in lists:
            concat += l
        concat.sort()
        return concat[0:n]

    def parse_stats(self, filename):
        """ Parse statistics from the file with name "filename" and merge them
        into self
        
        Returns the newly parsed stats as a dictionary
        """
        new_stats = Statistics(self.conversions, self.stat_formats)
        with open(str(filename), "r") as inputfile:
            for line in inputfile:
                new_stats.parse_line(line)
        self.merge_stats(new_stats)
        return new_stats.as_dict()


class HasName(object, metaclass=abc.ABCMeta):
    @abc.abstractproperty
    def name(self):
        # The name of the task in a simple form that can be used as
        # a Python dictionary key, a directory name, part of a file name,
        # part of an SQL table name, etc. That pretty much limits it to
        # alphabetic first letter, and alphanumeric rest.
        pass

class HasTitle(object, metaclass=abc.ABCMeta):
    @abc.abstractproperty
    def title(self):
        # A pretty name for the task, will be used in screen output
        pass

class DoesLogging(HasTitle, metaclass=abc.ABCMeta):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.logger = logging.getLogger(self.title)

class MakesTablenames(HasName):
    @property
    def database_state_table_name(self):
        """ Prefix string for table names
        
        By default, the table name prefix is the name attribute, but this can
        be overridden
        """
        return self.name
    
    def make_tablename(self, extra=None):
        """ Return a name for a DB table """
        # Maybe replace SQL-disallowed characters here, like digits and '.' ?
        # Could be tricky to avoid collisions
        name = self.database_state_table_name
        if extra:
            name = name + '_' + extra
        wudb.check_tablename(name)
        return name

class HasState(MakesTablenames, wudb.HasDbConnection):
    """ Declares that the class has a DB-backed dictionary in which the class
    can store state information.
    
    The dictionary is available as an instance attribute "state".
    """
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        name = self.make_tablename()
        self.state = self.make_db_dict(name, connection=self.db_connection)


class FilesCreator(MakesTablenames, wudb.HasDbConnection, metaclass=abc.ABCMeta):
    """ A base class for classes that produce a list of output files, with
    some auxiliary information stored with each file (e.g., nr. of relations).
    This info is stored in the form of a DB-backed dictionary, with the file
    name as the key and the auxiliary data as the value.
    """
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        tablename = self.make_tablename("outputfiles")
        self.output_files = self.make_db_dict(tablename,
                                              connection=self.db_connection)
    
    def add_output_files(self, filenames, *, commit):
        """ Adds a dict of files to the list of existing output files """
        final_files = {}
        for filename in filenames:
            if filename in self.output_files:
                self.logger.warning("%s already in output files table" % filename)
                #raise KeyError("%s already in output files table" % filename)
            else:
                final_files[filename] = filenames[filename]
        self.output_files.update(final_files, commit=commit)
    
    def get_output_filenames(self, condition=None):
        """ Return output file names, optionally those that match a condition
        
        If a condition is given, it must be callable with 1 parameter and
        boolean return type; then only those filenames are returned where
        for the auxiliary data s (i.e., the value stored in the dictionary
        with the file name as key) satisfies condition(s) == True.
        """
        if condition is None:
            return list(self.output_files.keys())
        else:
            return [f for (f, s) in self.output_files.items() if condition(s)]
    
    def forget_output_filenames(self, filenames, *, commit):
        self.output_files.clear(filenames, commit=commit)


class BaseStatistics(object):
    """ Base class for HasStatistics and SimpleStatistics that terminates the
    print_stats() call chain.
    """
    def print_stats(self):
        pass


class HasStatistics(BaseStatistics, HasState, DoesLogging, metaclass=abc.ABCMeta):
    @property
    def stat_conversions(self):
        """ Sub-classes should override """
        return []

    @property
    def stat_formats(self):
        """ Sub-classes should override """
        return []

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.statistics = Statistics(self.stat_conversions, self.stat_formats)
        self.statistics.from_dict(self.state)

    def get_statistics_as_strings(self):
        """ Return the statistics collected so far as a List of strings.
        
        Sub-classes can override to add/remove/change strings.
        """
        return self.statistics.as_strings()

    def print_stats(self):
        stat_msgs = self.get_statistics_as_strings()
        if stat_msgs:
            self.logger.info("Aggregate statistics:")
            for msg in stat_msgs:
                self.logger.info(msg)
        super().print_stats()
    
    def parse_stats(self, filename, *, commit):
        # self.logger.info("Parsing filename %s\n", filename)
        new_stats = self.statistics.parse_stats(filename)
        self.logger.debug("Newly arrived stats: %s", new_stats)
        update = self.statistics.as_dict()
        self.logger.debug("Combined stats: %s", update)
        self.state.update(update, commit=commit)


class SimpleStatistics(BaseStatistics, HasState, DoesLogging, 
        metaclass=abc.ABCMeta):

    @abc.abstractproperty
    def programs(self):
        # A list of classes of Programs which this tasks uses
        pass

    def print_cpu_real_time(self, cputotal, realtotal, program):
        """ Print cpu and/or real time to logger """
        # Uses self only for access to the logger
        pairs = zip((cputotal, realtotal), ("cpu", "real"))
        usepairs = [pair for pair in pairs if pair[0]]
        if usepairs:
            printformat = "/".join(["%g"] * len(usepairs))
            usepairs = tuple(zip(*usepairs))
            timestr = '/'.join(usepairs[1])
            self.logger.info("Total %s time for %s: " + printformat,
                    timestr, program, *usepairs[0])
    
    @staticmethod
    def keyname(is_cpu, programname):
        return "cputime_%s" % programname if is_cpu else "realtime_%s" % programname

    def update_cpu_real_time(self, programname, cpu=None, real=None, commit=True):
        """ Add seconds to the statistics of cpu time spent by program,
        and return the new total.
        """
        assert isinstance(programname, str)
        update = {}
        for (is_cpu, time) in ((True, cpu), (False, real)):
            if time is not None:
                key = self.keyname(is_cpu, programname)
                update[key] = self.state.get(key, 0.) + time
        if update:
            self.state.update(update, commit=commit)

    def get_cpu_real_time(self, program):
        """ Return list of cpu and real time spent by program """
        return [self.state.get(self.keyname(is_cpu, program.name), 0.)
                for is_cpu in (True, False)]

    def get_total_cpu_or_real_time(self, is_cpu):
        """ Return tuple with number of seconds of cpu and real time spent
        by all programs of this Task
        """
        times = [self.get_cpu_real_time(p) for p, o, i in self.programs]
        times = tuple(map(sum, zip(*times)))
        return times[0 if is_cpu else 1]

    def print_stats(self):
        for program, o, i in self.programs:
            cputotal, realtotal  = self.get_cpu_real_time(program)
            self.print_cpu_real_time(cputotal, realtotal, program.name)
        super().print_stats()


class Runnable(object):
    @abc.abstractmethod
    def run(self):
        pass


class DoesImport(DoesLogging, cadoparams.UseParameters, Runnable,
                 metaclass=abc.ABCMeta):
    @abc.abstractproperty
    def paramnames(self):
        return self.join_params(super().paramnames, {"import": None})

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._did_import = False

    def run(self):
        super().run()
        if "import" in self.params and not self._did_import:
            self.import_files(self.params["import"])
            self._did_import = True

    def import_files(self, input_filename):
        if input_filename.startswith('@'):
            self.logger.info("Importing files listed in %s", input_filename[1:])
            with open(input_filename[1:], "r") as f:
                filenames = f.read().splitlines()
        else:
            self.logger.info("Importing file %s", input_filename)
            filenames = [input_filename]
        for filename in filenames:
            self.import_one_file(filename)

    def did_import(self):
        return self._did_import

    @abc.abstractmethod
    def import_one_file(self, filename):
        pass

def chain_dict(d1, d2):
    """ Chain two mappings.

    If d[x] == y and e[y] == z, then chain_dict(d, e)[x] == z.
    >>> chain_dict({1: 17}, {17: 42})
    {1: 42}
    """
    return {key: d2[value] for key, value in d1.items()}

class RealTimeOutputFilter:
    def __init__(self, logger, filename):
        self.stdout = open(filename, "w")
        self.logger = logger
    def filter(self, data):
        self.stdout.write(data)
    def __enter__(self):
        return self
    def __exit__(self, *args):
         self.stdout.close()
        
        

class Task(patterns.Colleague, SimpleStatistics, HasState, DoesLogging,
           cadoparams.UseParameters, Runnable, metaclass=abc.ABCMeta):
    """ A base class that represents one task that needs to be processed.
    
    Sub-classes must define class variables:
    """
    # Properties that subclasses need to define
    @abc.abstractproperty
    def programs(self):
        # A tuple of 3-tuples, with each 3-tuple containing
        # 1. the class of Program which this tasks uses
        # 2. a tuple of parameters to the Program which the Task computes and
        #    which therefore should not be filled in from the Parameters file
        # 3. a dict of parameters which are file names and which should be
        #    filled in by sending Requests to other Tasks. This also enables
        #    testing whether input files have been changed by the other Task.
        pass
    @abc.abstractproperty
    def paramnames(self):
        # Parameters that all tasks use
        return self.join_params(super().paramnames, 
            {"name": str, "workdir": str, "run": True})
    @property
    def param_nodename(self):
        # avoid segregating our parameters, which are user-visible
        # things, underneath tree nodes whose name depends on some
        # implementation detail which is the task name. Except in
        # specific cases, a "task" does not (no longer) define a nesting
        # level in the parameter hierarchy.
        #
        # return self.name
        return None
    def __init__(self, *, mediator, db, parameters, path_prefix):
        ''' Sets up a database connection and a DB-backed dictionary for
        parameters. Reads parameters from DB, and merges with hierarchical
    
        parameters in the parameters argument. Parameters passed in by
        parameters argument do not override values in the DB-backed
        parameter dictionary.
        '''
        
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.logger.debug("Enter Task.__init__(%s)",
                          self.name)
        self.logger.debug("state = %s", self.state)
        # Set default parameters for this task, if any are given
        self.params = self.parameters.myparams(self.paramnames)
        self.logger.debug("self.parameters = %s", self.parameters)
        self.logger.debug("params = %s", self.params)
        # Set default parameters for our programs
        # The progparams entries should not be modified after a class'
        # constuctor (within __init__() is fine tho)
        self.progparams = []
        maindict = self.parameters.parameters
        for prog, override, needed_input in self.programs:
            # Parameters listed in needed_input are assumed to be overridden
            for key in (set(override) & set(needed_input)):
                self.logger.warning("Parameter %s listed in both overridden "
                                    "parameters and in input files for %s, "
                                    "only one is needed", key, prog.name)
            prog_param_path = self.parameters.get_param_path() + [prog.name]
            progparams = self.parameters.myparams(prog.get_accepted_keys(),
                                                  prog.name)
            for c in progparams:
                finergrain = '.'.join(prog_param_path+[c])
                coarsegrain = maindict.locate(finergrain)
                self.logger.debug("%s found from %s" % (finergrain, coarsegrain))
            for param in (set(needed_input)|set(override)) & set(progparams):
                finergrain = '.'.join(prog_param_path+[param])
                coarsegrain = maindict.locate(finergrain)
                # Whenever we see a parameter that is marked as override,
                # we will discard it and let the task level fill data for
                # this parameter. There are cases where this really is a
                # user error and we want to complain:
                #  - when the parameter file *explicitly* sets this
                #    parameter at this level. This does not make sense
                #    and is a troubling no-op. Typical example is
                #    specifying tasks.linalg.bwc.m in dlp mode.
                #  - when the parameter file sets it at a level above,
                #    but the task level does *not* know about this
                #    parameter anyway. This is ignored as well, and the
                #    task level will fill that parameter based on data it
                #    knows. But leaving the user with the feeling that he
                #    might be able to control that parameter is
                #    inelegant. A typical example is
                #    tasks.sieve.makefb.lim (which the tasks.sieve level
                #    sets based on the lim0 and lim1 parameters it knows
                #    about). Likewise, many "out" parameters behave
                #    similarly.
                if finergrain == coarsegrain or param not in set(self.paramnames):
                    self.logger.error('Parameter "%s" for program "%s" is '
                                     'generated at run time and cannot be '
                                     'supplied through the parameter file',
                                     param, prog.name)
                    self.logger.error('Ignoring %s, we rely on %s to compute it '
                                      'based on parameters at level %s only',
                                     '.'.join(path_prefix+[prog.name, param]),
                                     self.__class__,
                                     '.'.join(path_prefix))
                # We'll anyway discard it, but it's normal if we
                # inherited the parameter from a level above.
                del(progparams[param])
            
            self.progparams.append(progparams)
        # FIXME: whether to init workdir or not should not be controlled via
        # presence of a "workdir" parameter, but by class definition
        if "workdir" in self.params:
            self.workdir = WorkDir(self.params["workdir"], self.params["name"],
                               self.name)
        # Request mediator to run this task. It the "run" parameter is set
        # to false, then run() below will abort.
        self.send_notification(Notification.WANT_TO_RUN, None)
        self.logger.debug("Exit Task.__init__(%s)", self.name)
        return

    def run(self):
        if not self.params["run"]:
            self.logger.info("Stopping at %s", self.name)
            raise Exception("Job aborted because of a forcibly disabled task")
        self.logger.info("Starting")
        self.logger.debug("%s.run(): Task state: %s", self.name, self.state)
        super().run()
        # Make set of requests so multiply listed requests are sent only once
        # The input_file dict maps key -> Request. Make set union of requests
        requests = set.union(*[set(i.values()) for p, o, i in self.programs])
        # Make dict mapping Request -> answer (i.e., FileName object)
        answers = self.batch_request(dict(zip(requests, requests)))
        # Make list of dicts mapping key -> answer
        self._input_files = [chain_dict(i, answers) for p, o, i in self.programs]
        # Since merged_args is re-generated in each run(), the subclass can
        # modify it as it pleases (unlike progparams)
        # For each program, merge the progparams and the input_files dicts
        self.merged_args = [dict(p.items() | i.items())
                            for p, i in zip(self.progparams, self._input_files)]

    def translate_input_filename(self, filename):
        return filename

    def test_outputfile_exists(self, filename):
        return filename.isfile()

    def cmp_input_version(self, state_key, version):
        """ Compare the version of the input file with the version we have
        processed before

        Returns None if filename does not include file version information,
        returns -2 if we have never processed the file before,
        returns -1 if the previously processed file is older,
        returns 0 if they are the same version,
        throws an exception if the processed version is newer than the
        current one.
        """
        if version is None:
            return None
        if not state_key in self.state:
            return -2
        if self.state[state_key] < version:
            return -1
        if self.state[state_key] == version:
            return 0
        raise ValueError("Previously processed version is newer than current")


    @staticmethod
    def _input_file_to_state_dict(key, index, filename):
        return ("processed_version_%d_%s" % (index, key), filename.get_version())

    def have_new_input_files(self):
        # Change this to "self.logger.info" for showing each check on screen
        log = self.logger.debug
        result = False
        for index, input_files in enumerate(self._input_files):
            for key, filename in input_files.items():
                (state_key, version) = \
                    self._input_file_to_state_dict(key, index, filename)
                c = self.cmp_input_version(state_key, version)
                if c == -2:
                    log("File %s was not processed before", filename)
                    result = True
                if c == -1:
                    log("File %s is newer than last time", filename)
                    result = True
        # Collapse programs from all dict into one set
        all = set.union(*[set(i.values()) for i in self._input_files])
        if result is False and all:
            (n, v) = (("", "is"), ("s", "are"))[len(all) > 1]
            log("Input file%s %s %s unchanged since last time",
                n, ", ".join(map(str, all)), v)
        return result

    def remember_input_versions(self, commit=True):
        update = {}
        for index, input_files in enumerate(self._input_files):
            for (key, filename) in input_files.items():
                (state_key, version) = \
                    self._input_file_to_state_dict(key, index, filename)
                if version is not None:
                    update[state_key] = version
        self.state.update(update, commit)

    @staticmethod
    def check_files_exist(filenames, filedesc, shouldexist):
        """ Check that the output files in "filenames" exist or don't exist,
        according to shouldexist.
        
        Raise IOError if any check fails, return None
        """
        for filename in filenames:
            if isinstance(filename, FilePath):
                exists = filename.isfile()
            else:
                exists = os.path.isfile(filename)
            if shouldexist and not exists:
                raise IOError("%s file %s does not exist" % (filedesc, filename))
            elif not shouldexist and exists:
                raise IOError("%s file %s already exists" % (filedesc, filename))
        return
    
    # These two function go together, one produces a workunit name from the
    # name of the factorization, the task name, and a task-provided identifier,
    # and the other function splits them again
    wu_paste_char = '_'
    wu_attempt_char = '#'
    def make_wuname(self, identifier, attempt=None):
        """ Generates a wuname from project name, task name, identifier, and
        attempt number.
        """
        assert not self.wu_paste_char in self.name # self.name is task name
        assert not self.wu_paste_char in identifier # identifier is, e.g., range string
        assert not self.wu_attempt_char in identifier
        wuname = self.wu_paste_char.join([self.params["name"], self.name,
                                          identifier])
        if not attempt is None:
            wuname += "%s%d" % (self.wu_attempt_char, attempt)
        return wuname
    
    def split_wuname(self, wuname):
        """ Splits a wuname into project name, task name, identifier, and
        attempt number.
        
        Always returns a list of length 4; if there is not attempt given in
        the wuname, then the last array entry is None

        >>> # Test many possible combinations of "_" and "#" occuring in names
        >>> # where these characters are allowed
        >>> class Klass():
        ...     params = {"name": None}
        ...     wu_paste_char = '_'
        ...     wu_attempt_char = '#'
        >>> inst = Klass()
        >>> from itertools import product
        >>> prod = product(*[["", "_", "#"]] * 4 + [["", "#"]]*2)
        >>> for sep in prod:
        ...     inst.params["name"] = "%s%sprojectname%s%s" % sep[0:4]
        ...     inst.name = "%staskname%s" % sep[4:6]
        ...     for attempt in [None, 2, 3]:
        ...         identifier = "identifier"
        ...         wuname = Task.make_wuname(inst, "identifier", attempt=attempt)
        ...         wu_split = Task.split_wuname(inst, wuname)
        ...         assert wu_split == [inst.params["name"], inst.name, identifier, attempt]
        """
        arr = wuname.rsplit(self.wu_paste_char, 2)
        assert len(arr) == 3
        attempt = None
        # Split off attempt number, if available
        if "#" in arr[2]:
            (arr[2], attempt) = arr[2].split('#')
            attempt = int(attempt)
        arr.append(attempt)
        return arr
    
    class ResultInfo(wudb.WuResultMessage):
        def __init__(self, wuid, rc, stdout, stderr, program, cmd_line, host):
            self.wuid = wuid
            self.rc = rc
            self.stdout = stdout if stdout else None
            self.stdoutfile = program.get_stdout()
            # stdout must be either in a string or in a file, but not both
            assert self.stdout is None or not self.stdoutfile
            self.stderr = stderr if stderr else None
            self.stderrfile = program.get_stderr()
            # stderr must be either in a string or in a file, but not both
            assert self.stderr is None or not self.stderrfile
            self.output_files = program.get_output_files(with_stdio=False)
            self.cmd_line = cmd_line
            self.host = host
        def get_wu_id(self):
            return self.wuid
        def get_output_files(self):
            return self.output_files
        def get_stdout(self, command_nr):
            assert command_nr == 0
            return self.stdout
        def get_stdoutfile(self, command_nr):
            assert command_nr == 0
            return self.stdoutfile
        def get_stderr(self, command_nr):
            assert command_nr == 0
            return self.stderr
        def get_stderrfile(self, command_nr):
            assert command_nr == 0
            return self.stderrfile
        def get_exitcode(self, command_nr):
            assert command_nr == 0
            return self.rc
        def get_command_line(self, command_nr):
            assert command_nr == 0
            return self.cmd_line
        def get_host(self):
            return self.host

    def log_failed_command_error(self, message, command_nr):
        host = message.get_host()
        host_msg = " run on %s" % host if host else ""
        self.logger.error("Program%s failed with exit code %d", 
                          host_msg, message.get_exitcode(command_nr))
        cmd_line = message.get_command_line(command_nr)
        if cmd_line:
            self.logger.error("Command line was: %s", cmd_line)
        stderr = message.read_stderr(command_nr)
        stderrfilename = message.get_stderrfile(command_nr)
        if stderrfilename:
            stderrmsg = " (stored in file %s)" % stderrfilename
        else:
            stderrmsg = ""
        if stderr:
            self.logger.error("Stderr output follows%s:\n%s", stderrmsg, stderr)

    def submit_command(self, command, identifier, commit=True, log_errors=False):
        ''' Run a command.
        Return the result tuple. If the caller is an Observer, also send
        result to updateObserver().
        '''
        wuname = self.make_wuname(identifier)
        process = cadocommand.Command(command)
        cputime_used = os.times()[2] # CPU time of child processes
        realtime_used = time.time()
        (rc, stdout, stderr) = process.wait()
        cputime_used = os.times()[2] - cputime_used
        realtime_used = time.time() - realtime_used
        self.update_cpu_real_time(command.name, cputime_used, realtime_used, commit)
        message = Task.ResultInfo(wuname, rc, stdout, stderr, command, 
                                  command.make_command_line(), "server")
        if rc != 0 and log_errors:
            self.log_failed_command_error(message, 0)

        if isinstance(self, patterns.Observer):
            # pylint: disable=E1101
            self.updateObserver(message)
        return message
    
    def filter_notification(self, message):
        wuid = message.get_wu_id()
        rc = message.get_exitcode(0)
        stdout = message.read_stdout(0)
        stderr = message.read_stderr(0)
        output_files = message.get_output_files()
        self.logger.message("%s: Received notification for wuid=%s, rc=%d, "
                            "output_files=[%s]",
                            self.name, wuid, rc, ", ".join(output_files))
        (name, task, identifier, attempt) = self.split_wuname(wuid)
        if name != self.params["name"] or task != self.name:
            # This notification is not for me
            self.logger.message("Notification %s is not for me", wuid)
            return
        self.logger.message("Notification %s is for me", wuid)
        if rc != 0:
            self.logger.debug("Return code is: %d", rc)
        if stdout:
            self.logger.debug("stdout is: %s", stdout)
        if stderr:
            self.logger.debug("stderr is: %s", stderr)
        if output_files:
            self.logger.message("Output files are: %s", ", ".join(output_files))
        return identifier
    
    def send_notification(self, key, value):
        """ Wrapper around Colleague.send_notification() that instantiates a
        Notification with self as the sender
        """
        notification = Notification(self, key, value)
        super().send_notification(notification)
    
    def send_request(self, key, *args):
        """ Wrapper around Colleague.send_request() that instantiates a
        Request with self as the sender
        """
        request = Request(self, key, *args)
        return super().send_request(request)

    def batch_request(self, requests):
        """ Given a dict from keys to Request objects, return a dict with the
        same keys to the results of the requests.
        """
        return {key: self.send_request(request) for key, request in requests.items()}

    def get_number_outstanding_wus(self):
        return 0
    
    def verification(self, wuid, ok, *, commit):
        pass

    def get_state_filename(self, key, version=None):
        """ Return a file name stored in self.state as a FilePath object
        
        If a version parameter is passed, then this version is set as the
        version field of the FilePath object. If no parameter is passed, but
        our state includes an "output_version" key, then that is used.
        """
        if not key in self.state:
            return None
        if version is None:
            version = self.state.get("output_version", None)
        return self.workdir.path_in_workdir(self.state[key], version)

    def make_std_paths(self, progname, do_increment=True, prefix=None):
        count = self.state.get("stdiocount", 0)
        if do_increment:
            count += 1
        did_increment = do_increment
        while True:
            try:
                stdoutname = "%s.stdout.%d" % (progname, count)
                stderrname = "%s.stderr.%d" % (progname, count)
                self.check_files_exist((stdoutname, stderrname), "stdio",
                                       shouldexist=False)
            except IOError:
                count += 1
                did_increment = True
                self.logger.warning("Stdout or stderr files with index %d "
                                    "already exist", count)
            else:
                break
        stdoutpath = self.workdir.make_filename(stdoutname, prefix=prefix)
        stderrpath = self.workdir.make_filename(stderrname, prefix=prefix)
        if did_increment:
            self.state["stdiocount"] = count
        return (stdoutpath, stderrpath)

    def make_filelist(self, files, prefix=None):
        """ Create file file containing a list of files, one per line """
        filelist_idx = self.state.get("filelist_idx", 0) + 1
        self.state["filelist_idx"] = filelist_idx
        filelistname = self.workdir.make_filename("filelist.%d" % filelist_idx, prefix=prefix)
        with filelistname.open("w") as filelistfile:
            filelistfile.write("\n".join(files) + "\n")
        return filelistname

    def collect_usable_parameters(self, rl):
        message=[]
        message.append("Parameters used by Task %s" % self.name)
        prefix = '.'.join(self.parameters.get_param_path())
        for p in self.paramnames:
            message.append("  %s.%s" % (prefix, p))
            rl[p].append(prefix)
        for prog, override, needed_input in self.programs:
            message.append("  Parameters for program %s (general form %s.%s.*)" % (
                    prog.name, prefix, prog.name))
            for p in sorted(prog.get_accepted_keys()):
                t = "%s.%s.%s" % (prefix, prog.name, p)
                rl[p].append("%s.%s" % (prefix, prog.name))
                if p in set(override):
                    message.append("    [excluding internal parameter %s]" % t)
                elif p in set(needed_input):
                    message.append("    [excluding internal file name %s]" % t)
                else:
                    message.append("    %s" % t)
        message.append("")
        return "\n".join(message)


class ClientServerTask(Task, wudb.UsesWorkunitDb, patterns.Observer):
    @abc.abstractproperty
    def paramnames(self):
        return self.join_params(super().paramnames,  
            {"maxwu": 10, 
             "wutimeout": 10800,  # Default: 3h
             "maxresubmit": 5, 
             "maxtimedout": 100, 
             "maxfailed": 100})
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.state.setdefault("wu_submitted", 0)
        self.state.setdefault("wu_received", 0)
        self.state.setdefault("wu_timedout", 0)
        self.state.setdefault("wu_failed", 0)
        assert self.get_number_outstanding_wus() >= 0
        # start_real_time will be a float giving the number of seconds since
        # Jan 1 1900 at the beginning of the task
        self.state.update({"start_real_time": 0})
        # start_achievement is a variable that tells us how far we were at
        # the beginning of this run (for example if a factorization is
        # restarted in the middle of a polyselect or sieve task.)
        # It should be in [0,1], and if not initialized yet it is -1.
        self.state.update({"start_achievement": -1})
        self.send_notification(Notification.SUBSCRIBE_WU_NOTIFICATIONS, None)
    
    def submit_wu(self, wu, commit=True):
        """ Submit a WU and update wu_submitted counter """
        # at beginning of the task, set "start_real_time" to the number of
        # seconds since Jan 1 1900
        if self.state["start_real_time"] == 0:
           delta = datetime.datetime.now() - datetime.datetime(1900,1,1)
           self.state.update({"start_real_time": delta.total_seconds()})
        key = "wu_submitted"
        self.state.update({key: self.state[key] + 1}, commit=False)
        self.wuar.create(str(wu), commit=commit)
    
    def cancel_wu(self, wuid, commit=True):
        """ Cancel a WU and update wu_timedout counter """
        self.logger.debug("Cancelling: %s", wuid)
        key = "wu_timedout"
        maxtimedout = self.params["maxtimedout"]
        if not self.state[key] < maxtimedout:
            self.logger.error("Exceeded maximum number of timed out "
                              "workunits, maxtimedout=%d ", maxtimedout)
            raise Exception("Too many timed out work units. Please increase tasks.maxtimedout (current value is %d)" % maxtimedout)
        self.state.update({key: self.state[key] + 1}, commit=False)
        self.wuar.cancel(wuid, commit=commit)
    
    def submit_command(self, command, identifier, commit=True, log_errors=False):
        ''' Submit a workunit to the database. '''
        
        while self.get_number_available_wus() >= self.params["maxwu"]:
            self.wait()
        wuid = self.make_wuname(identifier)
        wutext = command.make_wu(wuid)
        for filename in command.get_exec_files() + command.get_input_files():
            basename = os.path.basename(filename)
            self.send_notification(Notification.REGISTER_FILENAME,
                                   {basename:filename})
        
        self.logger.info("Adding workunit %s to database", wuid)
        # self.logger.debug("WU:\n%s" % wutext)
        self.submit_wu(wutext, commit=commit)
        # Write command line to a file
        cmdline = command.make_command_line()
        client_cmd_filename = self.workdir.make_filename2(taskname="",
                                                          filename="wucmd")
        with client_cmd_filename.open("a") as client_cmd_file:
            client_cmd_file.write("# Command for work unit: %s\n%s\n" %
                                  (wuid, cmdline))
    
    def get_eta(self):
        delta = datetime.datetime.now() - datetime.datetime(1900,1,1)
        seconds = delta.total_seconds() - self.state["start_real_time"]
        a = self.get_achievement()
        a0 = self.state["start_achievement"]
        if a0 == -1:
            self.state["start_achievement"] = a
            a0 = a
        try:
           remaining_time = seconds / (a - a0) * (1.0 - a)
           now = datetime.datetime.now()
           arrival = now + datetime.timedelta(seconds=remaining_time)
           return arrival.ctime()
        except (OverflowError,ZeroDivisionError):
           return "Unknown"

    def verification(self, wuid, ok, *, commit):
        """ Mark a workunit as verified ok or verified with error and update
        wu_received counter """
        ok_str = "ok" if ok else "not ok"
        assert self.get_number_outstanding_wus() >= 1
        key = "wu_received"
        self.state.update({key: self.state[key] + 1}, commit=False)
        # only print ETA when achievement > 0 to avoid division by zero
        a = self.get_achievement()
        if a > 0:
            self.logger.info("Marking workunit %s as %s (%.1f%% => ETA %s)",
                              wuid, ok_str, 100.0 * a, self.get_eta())
        self.wuar.verification(wuid, ok, commit=commit)
    
    def cancel_available_wus(self):
        self.logger.info("Cancelling remaining workunits")
        self.wuar.cancel_all_available()
    
    def get_number_outstanding_wus(self):
        return self.state["wu_submitted"] - self.state["wu_received"] \
                - self.state["wu_timedout"]

    def get_number_available_wus(self):
        return self.wuar.count_available()

    def test_outputfile_exists(self, filename):
        # Can't test
        return False
    
    def wait(self):
        # Ask the mediator to check for workunits of status Received,
        # and if there are any, to send WU result notifications to the
        # subscribed listeners.
        # If we get notification on new results reliably from the HTTP server,
        # we might not need this poll. But they probably won't be totally
        # reliable
        if not self.send_request(Request.GET_WU_RESULT):
            self.resubmit_timed_out_wus()
            time.sleep(1)
    
    def resubmit_one_wu(self, wu, commit=True, maxresubmit=None):
        """ Takes a Workunit instance and adds it to workunits table under
        a modified name.
        """
        wuid = wu.get_id()
        (name, task, identifier, attempt) = self.split_wuname(wuid)
        attempt = 2 if attempt is None else attempt + 1
        # Don't do "if not maxresubmit:" as 0 is legit value
        if maxresubmit is None:
            maxresubmit = self.params["maxresubmit"]
        if attempt > maxresubmit:
            self.logger.info("Not resubmitting workunit %s, failed %d times",
                             wuid, attempt - 1)
            self.wuar.commit(commit)
            return
        new_wuid = self.make_wuname(identifier, attempt)
        wu.set_id(new_wuid)
        self.logger.info("Resubmitting workunit %s as %s", wuid, new_wuid)
        self.submit_wu(wu, commit=commit)
        
    def resubmit_timed_out_wus(self):
        """ Check for any timed out workunits and resubmit them """
        # We don't store the lastcheck in state as we do *not* want to check
        # instantly when we start up - clients should get a chance to upload
        # results first
        now = time.time()
        if not hasattr(self, "last_timeout_check"):
            self.logger.debug("Setting last timeout check to %f", now)
            self.last_timeout_check = now
            return
        
        check_every = 60 # Check every xx seconds
        if self.last_timeout_check + check_every >= now:
            # self.logger.info("It's not time to check yet, now = %f", now)
            return
        self.last_timeout_check = now
        
        timeout = self.params["wutimeout"]
        delta = datetime.timedelta(seconds=timeout)
        cutoff = str(datetime.datetime.utcnow() - delta)
        # self.logger.debug("Doing timeout check, cutoff=%s, and setting last check to %f",
        #                   cutoff, now)
        results = self.wuar.query(eq={"status": wudb.WuStatus.ASSIGNED},
                                  lt={"timeassigned": cutoff})
        results += self.wuar.query(eq={"status": wudb.WuStatus.NEED_RESUBMIT})
        if not results:
            # self.logger.debug("Found no timed-out workunits")
            pass
        for entry in results:
            self.cancel_wu(entry["wuid"], commit=False)
            self.resubmit_one_wu(Workunit(entry["wu"]), commit=True)

    def handle_error_result(self, message):
        """ Handle workunit with non-zero exit code
        
        If the result message indicates a failed command, log an error
        message, set the workunit to VERIFIED_ERROR in the DB, resubmit
        the work unit (but no more than once) and return True.
        If it indicates no error, return False. """
        if message.get_exitcode(0) == 0:
            return False
        self.log_failed_command_error(message, 0)
        key = "wu_failed"
        maxfailed = self.params["maxfailed"]
        if not self.state[key] < maxfailed:
            self.logger.error("Exceeded maximum number of failed "
                              "workunits, maxfailed=%d ", maxfailed)
            raise Exception("Too many failed work units")
        results = self.wuar.query(eq={"wuid":message.get_wu_id()})
        assert len(results) == 1 # There must be exactly 1 WU
        assert results[0]["status"] == wudb.WuStatus.RECEIVED_ERROR
        wu = workunit.Workunit(results[0]["wu"])
        self.state.update({key: self.state[key] + 1}, commit=False)
        self.verification(message.get_wu_id(), False, commit=False)
        self.resubmit_one_wu(wu, commit=True, maxresubmit=2)
        return True


class Polysel1Task(ClientServerTask, DoesImport, HasStatistics, patterns.Observer):
    """ Finds a number of size-optimized polynomial, uses client/server """
    @property
    def name(self):
        return "polyselect1"
    @property
    def title(self):
        return "Polynomial Selection (size optimized)"
    @property
    def programs(self):
        # admin and admax are special, which is a bit ugly: these parameters
        # to the Polyselect constructor are supplied by the task, but the
        # task has itself admin, admax parameters, which specify the total
        # size of the search range. Thus we don't include admin, admax here,
        # or PolyselTask would incorrectly warn about them not being used.
        return ((cadoprograms.Polyselect, ("out"), {}),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, {
            "N": int, "adrange": int, "admin": 0, "admax": int,
            "nrkeep": 20, "import_sopt": [str]})
    @staticmethod
    def update_lognorms(old_lognorm, new_lognorm):
        lognorm = [0, 0, 0, 0, 0]
        # print("update_lognorms: old_lognorm: %s" % old_lognorm)
        # print("update_lognorms: new_lognorm: %s" % new_lognorm)
        # New minimum. Don't use default value of 0 for minimum
        lognorm[1] = min(old_lognorm[1] or new_lognorm[1], new_lognorm[1])
        # New maximum
        lognorm[3] = max(old_lognorm[3], new_lognorm[3])
        # Rest is done by combine_stats(). [0::2] selects indices 0,2,4
        lognorm[0::2] = Statistics.combine_stats(old_lognorm[0::2],
                                                 new_lognorm[0::2])
        return lognorm
    
    # Stat: potential collisions=124.92 (2.25e+00/s)
    # Stat: raw lognorm (nr/min/av/max/std): 132/18.87/21.83/24.31/0.48
    # Stat: optimized lognorm (nr/min/av/max/std): 125/20.10/22.73/24.42/0.69
    # Stat: total phase took 55.47s
    @property
    def stat_conversions(self):
        return (
        (
            "stats_collisions",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp("# Stat: potential collisions=", 1)),
            False
        ),
        (
            "stats_rawlognorm",
            (int, float, float, float, float),
            "0 0 0 0 0",
            self.update_lognorms,
            re.compile(r"# Stat: raw lognorm \(nr/min/av/max/std\): (\d+)/{cap_fp}/{cap_fp}/{cap_fp}/{cap_fp}".format(**REGEXES)),
            False
        ),
        (
            "stats_optlognorm",
            (int, float, float, float, float),
            "0 0 0 0 0",
            self.update_lognorms,
            re.compile(r"# Stat: optimized lognorm \(nr/min/av/max/std\): (\d+)/{cap_fp}/{cap_fp}/{cap_fp}/{cap_fp}".format(**REGEXES)),
            False
        ),
        (
            "stats_tries",
            int,
            "0 0 0",
            Statistics.add_list,
            re.compile(r"# Stat: tried (\d+) ad-value\(s\), found (\d+) polynomial\(s\), (\d+) below maxnorm"),
            False
        ),
        (
            "stats_total_time",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp("# Stat: total phase took", 1, "s")),
            False
        ),
    )
    @property
    def stat_formats(self):
        return (
            ["potential collisions: {stats_collisions[0]:g}"],
            ["raw lognorm (nr/min/av/max/std): {stats_rawlognorm[0]:d}"] + 
                ["/{stats_rawlognorm[%d]:.3f}" % i for i in range(1, 5)],
            ["optimized lognorm (nr/min/av/max/std): {stats_optlognorm[0]:d}"] +
                ["/{stats_optlognorm[%d]:.3f}" % i for i in range(1, 5)],
            ["Total time: {stats_total_time[0]:g}"],
            )
    
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        assert self.params["nrkeep"] > 0
        self.state["adnext"] = \
            max(self.state.get("adnext", 0), self.params["admin"])
        # Remove admin and admax from the parameter-file-supplied program
        # parameters as those would conflict with the computed values
        self.progparams[0].pop("admin", None)
        self.progparams[0].pop("admax", None)

        tablename = self.make_tablename("bestpolynomials")
        self.best_polynomials = self.make_db_dict(
                tablename, connection=self.db_connection)
        self._check_best_polynomials()

        self.poly_heap = []
        # If we have "import", discard any existing polynomials
        if "import" in self.params and self.best_polynomials:
            self.logger.warning('Have "import" parameter, discarding '
                                'previously found polynomials')
            self.best_polynomials.clear()
        self.import_existing_polynomials()
        self._check_best_polynomials()
        self._compare_heap_db()
    
    def _check_best_polynomials(self):
        # Check that the keys form a sequence of consecutive non-negative
        # integers
        oldkeys = list(self.best_polynomials.keys())
        oldkeys.sort(key=int)
        assert oldkeys == list(map(str, range(len(self.best_polynomials))))

    def _compare_heap_db(self):
        """ Compare that the polynomials in the heap and in the DB agree
        
        They must contain an equal number of entries, and each polynomial
        stored in the heap must be at the specified index in the DB.
        """
        assert len(self.poly_heap) == len(self.best_polynomials)
        for lognorm, (key, poly) in self.poly_heap:
            assert self.best_polynomials[key] == str(poly)

    def import_existing_polynomials(self):
        debug = False
        oldkeys = list(self.best_polynomials.keys())
        oldkeys.sort(key=int) # Sort by numerical value
        for oldkey in oldkeys:
            if debug:
                print("Adding old polynomial at DB index %s: %s" %
                      (oldkey, self.best_polynomials[oldkey]))
            poly = Polynomials(self.best_polynomials[oldkey].splitlines())
            if not poly.lognorm:
                self.logger.error("Polynomial at DB index %s has no lognorm", oldkey)
                continue
            newkey = self._add_poly_heap(poly)
            if newkey is None:
                # Heap is full, and the poly was worse than the worst one on
                # the heap. Thus it did not get added and must be removed from
                # the DB
                if debug:
                    print("Deleting polynomial lognorm=%f, key=%s" % 
                          (poly.lognorm, oldkey))
                del(self.best_polynomials[oldkey])
            elif newkey != oldkey:
                # Heap is full, worst one in heap (with key=newkey) was
                # overwritten and its DB entry gets replaced with poly from
                # key=oldkey
                if debug:
                    print("Overwriting poly lognorm=%f, key=%s with poly "
                          "lognorm=%f, key=%s" %
                          (self.poly_heap[0][0], newkey, poly, oldkey))
                self.best_polynomials.clear(oldkey, commit=False)
                self.best_polynomials.update({newkey: poly}, commit=True)
            else:
                # Last case newkey == oldkey: nothing to do
                if debug:
                    print("Adding lognorm=%f, key=%s" % (poly.lognorm, oldkey))

    def run(self):
        if self.send_request(Request.GET_WILL_IMPORT_FINAL_POLYNOMIAL):
            self.logger.info("Skipping this phase, as we will import the final polynomial")
            return True

        super().run()

        if self.did_import() and "import_sopt" in self.params:
            self.logger.critical("The import and import_sopt parameters "
                                 "are mutually exclusive")
            return False

        if self.did_import():
            self.logger.info("Imported polynomial(s), skipping this phase")
            return True

        if "import_sopt" in self.params:
            self.import_files(self.params["import_sopt"])

        worstmsg = ", worst lognorm %f" % -self.poly_heap[0][0] \
                if self.poly_heap else ""
        self.logger.info("%d polynomials in queue from previous run%s", 
                         len(self.poly_heap), worstmsg)
        
        if self.is_done():
            self.logger.info("Already finished - nothing to do")
            return True
        
        # Submit all the WUs we need to reach admax
        while self.need_more_wus():
            self.submit_one_wu()
        
        # Wait for all the WUs to finish
        while self.get_number_outstanding_wus() > 0:
            self.wait()
        
        self._compare_heap_db()
        self.logger.info("Finished")
        return True
    
    def is_done(self):
        return not self.need_more_wus() and \
            self.get_number_outstanding_wus() == 0
    
    def get_achievement(self):
        return self.state["wu_received"] * self.params["adrange"] / (self.params["admax"] - self.params["admin"])

    def get_total_cpu_or_real_time(self, is_cpu):
        """ Return number of seconds of cpu time spent by polyselect_ropt """
        return float(self.state.get("stats_total_time", 0.)) if is_cpu else 0.

    def updateObserver(self, message):
        identifier = self.filter_notification(message)
        if not identifier:
            # This notification was not for me
            return False
        if self.handle_error_result(message):
            return True
        (filename, ) = message.get_output_files()
        self.process_polyfile(filename, commit=False)
        self.parse_stats(filename, commit=False)
        # Always mark ok to avoid warning messages about WUs that did not
        # find a poly
        self.verification(message.get_wu_id(), True, commit=True)
        return True
    
    @staticmethod
    def read_blocks(input):
        """ Return blocks of consecutive non-empty lines from input
        
        Whitespace is stripped; a line containing only whitespace is
        considered empty. An empty block is never returned.
        
        >>> list(Polysel1Task.read_blocks(['', 'a', 'b', '', 'c', '', '', 'd', 'e', '']))
        [['a', 'b'], ['c'], ['d', 'e']]
        """
        block = []
        for line in input:
            line = line.strip()
            if line:
                block.append(line)
            else:
                if block:
                    yield block
                block = []
        if block:
            yield block

    def import_one_file(self, filename):
        self.process_polyfile(filename)

    def process_polyfile(self, filename, commit=True):
        """ Read all size-optimized polynomials in a file and add them to the
        DB and priority queue if worthwhile.
        
        Different polynomials must be separated by a blank line.
        """
        try:
            polyfile = self.read_log_warning(filename)
        except (OSError, IOError) as e:
            if e.errno == 2: # No such file or directory
                self.logger.error("File '%s' does not exist", filename)
                return None
            else:
                raise
        totalparsed, totaladded = 0, 0
        for block in self.read_blocks(polyfile):
            parsed, added = self.parse_and_add_poly(block, filename)
            totalparsed += parsed
            totaladded += added
        have = len(self.poly_heap)
        nrkeep = self.params["nrkeep"]
        fullmsg = ("%d/%d" % (have, nrkeep)) if have < nrkeep else "%d" % nrkeep
        self.logger.info("Parsed %d polynomials, added %d to priority queue (has %s)",
                         totalparsed, totaladded, fullmsg)
        if totaladded:
            self.logger.info("Worst polynomial in queue now has lognorm %f",
                             -self.poly_heap[0][0])
                                     
    
    def read_log_warning(self, filename):
        """ Read lines from file. If a "# WARNING" line occurs, log it.
        """
        re_warning = re.compile("# WARNING")
        with open(filename, "r") as inputfile:
            for line in inputfile:
                if re_warning.match(line):
                    self.logger.warn("File %s contains: %s",
                                     filename, line.strip())
                yield line

    def parse_and_add_poly(self, text, filename):
        """ Parse a polynomial from an iterable of lines and add it to the
        priority queue and DB. Return a two-element list with the number of
        polynomials parsed and added, i.e., (0,0) or (1,0) or (1,1).
        """
        poly = self.parse_poly(text, filename)
        if poly is None:
            return (0, 0)
        if poly.getN() != self.params["N"]:
            self.logger.error("Polynomial is for the wrong number to be factored:\n%s",
                              poly)
            return (0, 0)
        if not poly.lognorm:
            self.logger.warn("Polynomial in file %s has no lognorm, skipping it",
                             filename)
            return (0, 0)
        if self._add_poly_heap_db(poly):
            return (1, 1)
        else:
            return (1, 0)

    def _add_poly_heap_db(self, poly):
        """ Add a polynomial to the heap and DB, if it's good enough.
        
        Returns True if the poly was added, False if not. """
        key = self._add_poly_heap(poly)
        if key is None:
            return False
        self.best_polynomials[key] = str(poly)
        return True

    def _add_poly_heap(self, poly):
        """ Add a polynomial to the heap
        
        If the heap is full (nrkeep), the worst polynomial (i.e., with the
        largest lognorm) is replaced if the new one is better.
        Returns the key (as a str) under which the polynomial was added,
        or None if it was not added.
        """
        assert len(self.poly_heap) <= self.params["nrkeep"]
        debug = False

        # Find DB index under which to store this new poly. If the heap
        # is not full, use the next bigger index.
        key = len(self.poly_heap)
        # Is the heap full?
        if key == self.params["nrkeep"]:
            # Should we store this poly at all, i.e., is it better than
            # the worst one in the heap?
            worstnorm = -self.poly_heap[0][0]
            if worstnorm <= poly.lognorm:
                if debug:
                    self.logger.debug("_add_poly_heap(): new poly lognorm %f, "
                          "worst in heap has %f. Not adding",
                          poly.lognorm, worstnorm)
                return None
            # Pop the worst poly from heap and re-use its DB index
            key = heapq.heappop(self.poly_heap)[1][0]
            if debug:
                self.logger.debug("_add_poly_heap(): new poly lognorm %f, "
                    "worst in heap has %f. Replacing DB index %s",
                     poly.lognorm, worstnorm, key)
        else:
            # Heap was not full
            if debug:
                self.logger.debug("_add_poly_heap(): heap was not full, adding "
                    "poly with lognorm %f at DB index %s", poly.lognorm, key)

        # The DB requires the key to be a string. In order to have
        # identical data in DB and heap, we store key as str everywhere.
        key = str(key)

        # Python heapq stores a minheap, so in order to have the worst
        # polynomial (with largest norm) easily accessible, we use
        # -lognorm as the heap key
        new_entry = (-poly.lognorm, (key, poly))
        heapq.heappush(self.poly_heap, new_entry)
        return key

    def parse_poly(self, text, filename):
        poly = None
        try:
            poly = Polynomials(text)
        except PolynomialParseException as e:
            if str(e) != "No polynomials found":
                self.logger.warn("Invalid polyselect file '%s': %s",
                                  filename, e)
                return None
        except UnicodeDecodeError as e:
            self.logger.error("Error reading '%s' (corrupted?): %s", filename, e)
            return None
        
        if not poly:
            return None
        return poly
    
    def get_raw_polynomials(self):
        # Extract polynomials from heap and return as list
        return [entry[1][1] for entry in self.poly_heap]

    def get_poly_rank(self, search_poly):
        """ Return how many polynomnials with lognorm less than the lognorm
        of the size-optimized version of search_poly there are in the
        priority queue.
        
        The size-optimized version of search_poly is identified by comparing
        the leading coefficients of both polynomials.
        """
        df = search_poly.polyf.degree
        dg = search_poly.polyg.degree
        # Search for the raw polynomial pair by comparing the leading
        # coefficients of both polynomials
        found = None
        for (index, (lognorm, (key, poly))) in enumerate(self.poly_heap):
            if search_poly.polyg.same_lc(poly.polyg):
               if not found is None:
                   self.logger.warning("Found more than one match for:\n%s", search_poly)
               else:
                   found = index
        if found is None:
            self.logger.warning("Could not find polynomial rank for %s", search_poly)
            return None
        # print("search_poly: %s" % search_poly)
        # print("Poly found in heap: %s" % self.poly_heap[found][1][1])
        search_lognorm = -self.poly_heap[found][0]
        rank = 0
        for (lognorm, (key, poly)) in self.poly_heap:
            if -lognorm < search_lognorm:
                rank += 1
        return rank

    def need_more_wus(self):
        return self.state["adnext"] < self.params["admax"]
    
    def submit_one_wu(self):
        adstart = self.state["adnext"]
        adend = adstart + self.params["adrange"]
        adend = adend - (adend % self.params["adrange"])
        assert adend > adstart
        adend = min(adend, self.params["admax"])
        outputfile = self.workdir.make_filename("%d-%d" % (adstart, adend), prefix=self.name)
        if self.test_outputfile_exists(outputfile):
            self.logger.info("%s already exists, won't generate again",
                             outputfile)
        else:
            p = cadoprograms.Polyselect(admin=adstart, admax=adend,
                                        stdout=str(outputfile),
                                        **self.progparams[0])
            self.submit_command(p, "%d-%d" % (adstart, adend), commit=False)
        self.state.update({"adnext": adend}, commit=True)

    def get_total_cpu_or_real_time(self, is_cpu):
        """ Return number of seconds of cpu time spent by polyselect """
        return float(self.state.get("stats_total_time", 0.)) if is_cpu else 0.


class Polysel2Task(ClientServerTask, HasStatistics, DoesImport, patterns.Observer):
    """ Finds a polynomial, uses client/server """
    @property
    def name(self):
        return "polyselect2"
    @property
    def title(self):
        return "Polynomial Selection (root optimized)"
    @property
    def programs(self):
        return ((cadoprograms.PolyselectRopt, (), {}),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, {
            "N": int, "I": int, "lim1": int, "lim0": int, "batch": [int],
            "import_ropt": [str]})
    @property
    def stat_conversions(self):
        return (
        (
            "stats_total_time",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp("# Stat: total phase took", 1, "s")),
            False
        ),
        (
            "stats_rootsieve_time",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp("# Stat: rootsieve took", 1, "s")),
            False
        )
    )
    @property
    def stat_formats(self):
        return (
            ["Total time: {stats_total_time[0]:g}"],
            ["Rootsieve time: {stats_rootsieve_time[0]:g}"],
            )

    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.bestpoly = None
        if "import" in self.params and "bestpoly" in self.state:
            self.logger.warning('Have "import" parameter, discarding '
                                'previously found best polynomial')
            self.state.clear(["bestpoly", "bestfile"])
        if "bestpoly" in self.state:
            self.bestpoly = Polynomials(self.state["bestpoly"].splitlines())
        self.state.setdefault("nr_poly_submitted", 0)
        # I don't understand why the area is based on one particular side.
        self.progparams[0].setdefault("area", 2.**(2*self.params["I"]-1) \
                * self.params["lim1"])
        self.progparams[0].setdefault("Bf", float(self.params["lim1"]))
        self.progparams[0].setdefault("Bg", float(self.params["lim0"]))
        if not "batch" in self.params:
            t = self.progparams[0].get("threads", 1)
            # batch = 5 rounded up to a multiple of t
            self.params["batch"] = (4 // t + 1) * t
        self.poly_to_submit = None

    def run(self):
        super().run()
        
        if self.bestpoly is None:
            self.logger.info("No polynomial was previously found")
        else:
            self.logger.info("Best polynomial previously found in %s has "
                             "Murphy_E = %g",
                             self.state["bestfile"], self.bestpoly.MurphyE)
        
        if self.did_import() and "import_ropt" in self.params:
            self.logger.critical("The import and import_ropt parameters "
                                 "are mutually exclusive")
            return False

        if self.did_import():
            self.logger.info("Imported polynomial, skipping this phase")
            return True

        if "import_ropt" in self.params:
            self.import_files(self.params["import_ropt"])

        # Get the list of polynomials to submit
        self.poly_to_submit = self.send_request(Request.GET_RAW_POLYNOMIALS)
        
        if self.is_done():
            self.logger.info("Already finished - nothing to do")
            self.print_rank()
            # If the poly file got lost somehow, write it again
            filename = self.get_state_filename("polyfilename")
            if filename is None or not filename.isfile():
                self.logger.warn("Polynomial file disappeared, writing again")
                self.write_poly_file()
            return True
        
        # Submit all the WUs we need
        while self.need_more_wus():
            self.submit_one_wu()
        
        # Wait for all the WUs to finish
        while self.get_number_outstanding_wus() > 0:
            self.wait()
        
        if self.bestpoly is None:
            self.logger.error ("No polynomial found. Consider increasing the "
                               "search range bound admax, or maxnorm")
            return False
        self.logger.info("Finished, best polynomial from file %s has Murphy_E "
                         "= %g", self.state["bestfile"] , self.bestpoly.MurphyE)
        self.print_rank()
        self.write_poly_file()
        return True

    def is_done(self):
        return not self.bestpoly is None and not self.need_more_wus() and \
            self.get_number_outstanding_wus() == 0
    
    def need_more_wus(self):
        return self.state["nr_poly_submitted"] < len(self.poly_to_submit)
    
    def get_achievement(self):
        return (self.state["nr_poly_submitted"] - self.params["batch"] * self.get_number_outstanding_wus()) / len(self.poly_to_submit)

    def updateObserver(self, message):
        identifier = self.filter_notification(message)
        if not identifier:
            # This notification was not for me
            return False
        if self.handle_error_result(message):
            return True
        (filename, ) = message.get_output_files()
        self.process_polyfile(filename, commit=False)
        self.parse_stats(filename, commit=False)
        # Always mark ok to avoid warning messages about WUs that did not
        # find a poly
        # FIXME: wrong, we should always get an optimized poly for a raw one
        self.verification(message.get_wu_id(), True, commit=True)
        return True
    
    def import_one_file(self, filename):
        old_bestpoly = self.bestpoly
        self.process_polyfile(filename)
        if not self.bestpoly is old_bestpoly:
            self.write_poly_file()

    def process_polyfile(self, filename, commit=True):
        poly = self.parse_poly(filename)
        if not poly is None:
            self.bestpoly = poly
            update = {"bestpoly": str(poly), "bestfile": filename}
            self.state.update(update, commit=commit)
    
    def read_log_warning(self, filename):
        """ Read lines from file. If a "# WARNING" line occurs, log it.
        """
        re_warning = re.compile("# WARNING")
        with open(filename, "r") as inputfile:
            for line in inputfile:
                if re_warning.match(line):
                    self.logger.warn("File %s contains: %s",
                                     filename, line.strip())
                yield line

    def parse_poly(self, filename):
        poly = None
        try:
            poly = Polynomials(self.read_log_warning(filename))
        except (OSError, IOError) as e:
            if e.errno == 2: # No such file or directory
                self.logger.error("File '%s' does not exist", filename)
                return None
            else:
                raise
        except PolynomialParseException as e:
            if str(e) != "No polynomials found":
                self.logger.warn("Invalid polyselect file '%s': %s",
                                  filename, e)
                return None
        except UnicodeDecodeError as e:
            self.logger.error("Error reading '%s' (corrupted?): %s", filename, e)
            return None
        
        if not poly:
            self.logger.info('No polynomial found in %s', filename)
            return None
        if poly.getN() != self.params["N"]:
            self.logger.error("Polynomial is for the wrong number to be factored:\n%s",
                              poly)
            return None
        if not poly.MurphyE:
            self.logger.warn("Polynomial in file %s has no Murphy E value",
                             filename)
        if self.bestpoly is None or poly.MurphyE > self.bestpoly.MurphyE:
            self.logger.info("New best polynomial from file %s:"
                             " Murphy E = %g" % (filename, poly.MurphyE))
            self.logger.debug("New best polynomial is:\n%s", poly)
            return poly
        else:
            self.logger.info("Best polynomial from file %s with E=%g is "
                             "no better than current best with E=%g",
                             filename, poly.MurphyE, self.bestpoly.MurphyE)
        return None
    
    def write_poly_file(self):
        filename = self.workdir.make_filename("poly")
        self.bestpoly.create_file(filename)
        self.state["polyfilename"] = filename.get_wdir_relative()
    
    def get_poly(self):
        if not "bestpoly" in self.state:
            return None
        return Polynomials(self.state["bestpoly"].splitlines())
    
    def get_poly_filename(self):
        return self.get_state_filename("polyfilename")

    def get_have_two_alg_sides(self):
        P = Polynomials(self.state["bestpoly"].splitlines())
        if (P.polyg.degree > 1):
            return True
        else:
            return False

    def submit_one_wu(self):
        assert self.need_more_wus()
        to_submit = len(self.poly_to_submit)
        nr = self.state["nr_poly_submitted"]
        inputfilename = self.workdir.make_filename("raw_%d" % nr, prefix=self.name)
        # Write one raw polynomial to inputfile
        batchsize = min(to_submit - nr, self.params["batch"])
        with inputfilename.open("w") as inputfile:
            for i in range(batchsize):
                inputfile.write(str(self.poly_to_submit[nr + i]))
                inputfile.write("\n")
        outputfile = self.workdir.make_filename("opt_%d" % nr, prefix=self.name)
        if self.test_outputfile_exists(outputfile):
            self.logger.info("%s already exists, won't generate again",
                             outputfile)
        else:
            p = cadoprograms.PolyselectRopt(inputpolys=str(inputfilename),
                                            stdout=str(outputfile),
                                            **self.progparams[0])
            self.submit_command(p, "%d" % nr, commit=False)
        self.state.update({"nr_poly_submitted": nr + batchsize}, commit=True)

    def get_total_cpu_or_real_time(self, is_cpu):
        """ Return number of seconds of cpu time spent by polyselect_ropt """
        return float(self.state.get("stats_total_time", 0.)) if is_cpu else 0.

    def print_rank(self):
        if not self.did_import():
            rank = self.send_request(Request.GET_POLY_RANK, self.bestpoly)
            if not rank is None:
                self.logger.info("Best overall polynomial was %d-th in list "
                                 "after size optimization", rank)
    def get_will_import(self):
        return "import" in self.params

class PolyselGFpnTask(Task, DoesImport):
    """ Polynomial selection for DL in extension fields """
    @property
    def name(self):
        return "polyselgfpn"
    @property
    def title(self):
        return "Polynomial Selection (for GF(p^n))"
    @property
    def programs(self):
        override = ("p", "n", "out")
        return ((cadoprograms.PolyselectGFpn, (override), {}),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames,
                {"N": int, "gfpext": int, "import": None})

    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
    
    def run(self):
        super().run()

        if not "polyfilename" in self.state:
            polyfilename = self.workdir.make_filename("poly")
            # Import mode
            if self.did_import():
                if not self.state["imported_poly"]:
                    raise Exception("Import failed?")
                with open(str(polyfilename), "w") as outfile:
                    outfile.write(self.state["imported_poly"])
                update = {
                        "poly": self.state["imported_poly"],
                        "polyfilename": polyfilename.get_wdir_relative()
                        }
                self.state.update(update)
                return True

            # Check that user does not ask for degree > 2
            if self.params["gfpext"] != 2:
                raise Exception("Polynomial selection not implemented " 
                    + "for extension degree > 2")

            # Call binary
            (stdoutpath, stderrpath) = \
                    self.make_std_paths(cadoprograms.PolyselectGFpn.name)
            p = cadoprograms.PolyselectGFpn(p=self.params["N"],
                    n=self.params["gfpext"], out=polyfilename,
                    stdout=str(stdoutpath),
                    stderr=str(stderrpath),
                    **self.merged_args[0])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            with open(str(polyfilename), "r") as inputfile:
                poly = Polynomials(list(inputfile))
            update = {
                    "poly": str(poly),
                    "polyfilename": polyfilename.get_wdir_relative()
                    }
            self.state.update(update)
        return True

    def import_one_file(self, filename):
        with open(filename, "r") as inputfile:
            poly = Polynomials(list(inputfile))
        update = {"imported_poly": str(poly)}
        self.state.update(update)

    def get_poly(self):
        return self.state["poly"];

    def get_poly_filename(self):
        return self.get_state_filename("polyfilename")

    def get_have_two_alg_sides(self):
        P = Polynomials(self.state["poly"].splitlines())
        return (P.polyf.degree > 1 and P.polyg.degree > 1)


class FactorBaseTask(Task):
    """ Generates the factor base for the polynomial(s) """
    @property
    def name(self):
        return "factorbase"
    @property
    def title(self):
        return "Generate Factor Base"
    @property
    def programs(self):
        return ((cadoprograms.MakeFB,
            ("out", "side", "lim"),
            {"poly": Request.GET_POLYNOMIAL_FILENAME}),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames,
                {"gzip": True, "I": int, "lim0": int, "lim1": int})

    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        # Invariant: if we have a result (in self.state["outputfile"]) then we
        # must also have a polynomial (in self.state["poly"] ) and the
        # lim1 value used in self.state["lim1"]
        if "outputfile" in self.state:
            assert "poly" in self.state
            assert "lim1" in self.state
            # The target file must correspond to the polynomial "poly"
        self.progparams[0].setdefault("maxbits", self.params["I"])
    
    def run(self):
        super().run()

        # Get best polynomial found by polyselect
        poly = self.send_request(Request.GET_POLYNOMIAL)
        if not poly:
            raise Exception("FactorBaseTask(): no polynomial "
                            "received from PolyselTask")
        twoalgsides = self.send_request(Request.GET_HAVE_TWO_ALG_SIDES)
        check_params = {key: self.params[key]
                        for key in ["lim1", "lim0"][0:1 + twoalgsides]}
        
        # Check if we have already computed the outputfile for this polynomial
        # and fbb. If any of the inputs mismatch, we remove outputfile from
        # state
        if "outputfile" in self.state:
            prevpoly = Polynomials(self.state["poly"].splitlines())
            if poly != prevpoly:
                self.logger.warn("Received different polynomial, "
                                 "discarding old factor base file")
                del(self.state["outputfile"])
            else:
                for key in check_params:
                    if self.state[key] != check_params[key]:
                        self.logger.warn("Parameter %s changed, discarding old "
                                         "factor base file", key)
                        del(self.state["outputfile"])
                    break
        # If outputfile is not in state, because we never produced it or because
        # input parameters changed, we remember our current input parameters
        if not "outputfile" in self.state:
            check_params["poly"] = str(poly)
            self.state.update(check_params)
        
        if not "outputfile" in self.state or self.have_new_input_files():
            
            # Make file name for factor base/free relations file
            # We use .gzip by default, unless set to no in parameters
            use_gz = ".gz" if self.params["gzip"] else ""
            if not twoalgsides:
                outputfilename = self.workdir.make_filename("roots" + use_gz)
            else:
                outputfilename0 = self.workdir.make_filename("roots0" + use_gz)
                outputfilename1 = self.workdir.make_filename("roots1" + use_gz)

            # Run command to generate factor base file
            (stdoutpath, stderrpath) = \
                    self.make_std_paths(cadoprograms.MakeFB.name)
            if not twoalgsides:
                p = cadoprograms.MakeFB(out=str(outputfilename),
                                    lim=self.params["lim1"],
                                    stdout=str(stdoutpath),
                                    stderr=str(stderrpath),
                                    **self.merged_args[0])
                message = self.submit_command(p, "", log_errors=True)
                if message.get_exitcode(0) != 0:
                    raise Exception("Program failed")
            else:
                p = cadoprograms.MakeFB(out=str(outputfilename0),
                                    side=0,
                                    lim=self.params["lim0"],
                                    stdout=str(stdoutpath),
                                    stderr=str(stderrpath),
                                    **self.merged_args[0])
                message = self.submit_command(p, "", log_errors=True)
                if message.get_exitcode(0) != 0:
                    raise Exception("Program failed")
                p = cadoprograms.MakeFB(out=str(outputfilename1),
                                    side=1,
                                    lim=self.params["lim1"],
                                    stdout=str(stdoutpath),
                                    stderr=str(stderrpath),
                                    **self.merged_args[0])
                message = self.submit_command(p, "", log_errors=True)
                if message.get_exitcode(0) != 0:
                    raise Exception("Program failed")
            
            if not twoalgsides:
                self.state["outputfile"] = outputfilename.get_wdir_relative()
            else:
                self.state["outputfile0"] = outputfilename0.get_wdir_relative()
                self.state["outputfile1"] = outputfilename1.get_wdir_relative()
                self.state["outputfile"] = outputfilename1.get_wdir_relative()
            self.logger.info("Finished")

        self.check_files_exist([self.get_filename()], "output",
                               shouldexist=True)
        return True
    
    def get_filename(self):
        return self.get_state_filename("outputfile")

    def get_filename0(self):
        assert self.send_request(Request.GET_HAVE_TWO_ALG_SIDES)
        return self.get_state_filename("outputfile0")

    def get_filename1(self):
        assert self.send_request(Request.GET_HAVE_TWO_ALG_SIDES)
        return self.get_state_filename("outputfile1")

class FreeRelTask(Task):
    """ Generates free relations for the polynomial(s) """
    @property
    def name(self):
        return "freerel"
    @property
    def title(self):
        return "Generate Free Relations"
    @property
    def programs(self):
        input = {"poly": Request.GET_POLYNOMIAL_FILENAME}
        if self.params["dlp"]:
            input["badideals"] = Request.GET_BADIDEALS_FILENAME
        return ((cadoprograms.FreeRel, ("renumber", "out"), input),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames,
                {"dlp": False, "gzip": True, "lcideals": None})

    wanted_regex = {
        'nfree': (r'# Free relations: (\d+)', int),
        'nprimes': (r'Renumbering struct: nprimes=(\d+)', int)
    }
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        if self.params["dlp"]:
            # default for dlp is lcideals
            self.progparams[0].setdefault("lcideals", True)
        # Invariant: if we have a result (in self.state["freerelfilename"])
        # then we must also have a polynomial (in self.state["poly"]) and
        # the lpb0/lpb1 values used in self.state["lpb1"] / ["lpb0"]
        if "freerelfilename" in self.state:
            assert "poly" in self.state
            assert "lpb1" in self.state
            assert "lpb0" in self.state
            # The target file must correspond to the polynomial "poly"
    
    def run(self):
        super().run()

        # Get best polynomial found by polyselect
        poly = self.send_request(Request.GET_POLYNOMIAL)
        if not poly:
            raise Exception("FreerelTask(): no polynomial "
                            "received from PolyselTask")

        # Check if we have already computed the freerelfile for this polynomial
        # and lpb. If any of the inputs mismatch, we remove freerelfilename
        # from state
        if "freerelfilename" in self.state:
            discard = False
            prevpoly = Polynomials(self.state["poly"].splitlines())
            if poly != prevpoly:
                self.logger.warn("Received different polynomial, discarding "
                                 "old free relations file")
                discard = True
            elif self.state["lpb1"] != self.progparams[0]["lpb1"] or \
                 self.state["lpb0"] != self.progparams[0]["lpb0"]:
                self.logger.warn("Parameter lpb1/lpb0 changed, discarding old "
                                 "free relations file")
                discard = True
            if discard:
                del(self.state["freerelfilename"])
                del(self.state["renumberfilename"])
        # If outputfile is not in state, because we never produced it or because
        # input parameters changed, we remember our current input parameters
        if not "freerelfilename" in self.state:
            self.state.update({"poly": str(poly), "lpb1": self.progparams[0]["lpb1"],
                               "lpb0": self.progparams[0]["lpb0"]})

        if not "freerelfilename" in self.state or self.have_new_input_files():
            # Make file name for factor base/free relations file
            # We use .gzip by default, unless set to no in parameters
            use_gz = ".gz" if self.params["gzip"] else ""
            freerelfilename = self.workdir.make_filename("freerel" + use_gz)
            renumberfilename = self.workdir.make_filename("renumber" + use_gz)
            (stdoutpath, stderrpath) = \
                    self.make_std_paths(cadoprograms.FreeRel.name)
            # Run command to generate factor base/free relations file
            p = cadoprograms.FreeRel(renumber=renumberfilename,
                                     out=str(freerelfilename),
                                     stdout=str(stdoutpath),
                                     stderr=str(stderrpath),
                                     **self.merged_args[0])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            stderr = message.read_stderr(0).decode("utf-8")
            update = self.parse_file(stderr.splitlines())
            update["freerelfilename"] = freerelfilename.get_wdir_relative()
            update["renumberfilename"] = renumberfilename.get_wdir_relative()
            self.state.update(update) 
            self.logger.info("Found %d free relations" % self.state["nfree"])
            self.logger.info("Finished")

        self.check_files_exist([self.get_freerel_filename(),
                                self.get_renumber_filename()], "output",
                               shouldexist=True)
        return True

    def parse_file(self, text):
        found = {}
        for line in text:
            for (key, (regex, datatype)) in self.wanted_regex.items():
                match = re.match(regex, line)
                if match:
                    if key in found:
                        raise Exception("Received two values for %s" % key)
                    found[key] = datatype(match.group(1))
        
        for key in self.wanted_regex:
            if not key in found:
                raise Exception("Received no value for %s" % key)
        return found
    
    def get_freerel_filename(self):
        return self.get_state_filename("freerelfilename")
    
    def get_renumber_filename(self):
        return self.get_state_filename("renumberfilename")
    
    def get_nrels(self):
        return self.state.get("nfree", None)
    
    def get_nprimes(self):
        return self.state.get("nprimes", None)


class SievingTask(ClientServerTask, DoesImport, FilesCreator, HasStatistics,
                  patterns.Observer):
    """ Does the sieving, uses client/server """
    @property
    def name(self):
        return "sieving"
    @property
    def title(self):
        return "Lattice Sieving"
    @property
    def programs(self):
        override = ("q0", "q1", "factorbase", "out", "stats_stderr")
        input = {"poly": Request.GET_POLYNOMIAL_FILENAME}
        return ((cadoprograms.Las, override, input),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, {
            "qmin": 0, "qrange": int, "rels_wanted": 0, "lim0": int,
            "lim1": int, "gzip": True, "sqside": 1})

    @property
    def stat_conversions(self):
        # Average J=1017 for 168 special-q's, max bucket fill 0.737035
        # Total cpu time 7.0s [precise timings available only for mono-thread]
        # Total 26198 reports [0.000267s/r, 155.9r/sq]
        return (
        (
            "stats_avg_J",
            (float, int),
            "0 0",
            Statistics.zip_combine_mean,
            re.compile(re_cap_n_fp("# Average J=", 1, r"\s*for (\d+) special-q's")),
            False
        ),
        (
            "stats_max_bucket_fill",
            float,
            "0",
            max,
            re.compile(re_cap_n_fp("#.*max bucket fill", 1)),
            False
        ),
        (
            "stats_total_cpu_time",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp("# Total cpu time", 1, "s")),
            False
        ),
        (
            "stats_total_time",
            (float, ),
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp("# Total time", 1, "s")),
            False
        )
    )
    @property
    def stat_formats(self):
        return (
            ["Average J: {stats_avg_J[0]:g} for {stats_avg_J[1]:d} special-q",
                ", max bucket fill: {stats_max_bucket_fill[0]:g}"],
            ["Total CPU time: {stats_total_cpu_time[0]:g}s"],
            ["Total time: {stats_total_time[0]:g}s"],
        )
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        qmin = self.params["qmin"]
        if "qnext" in self.state:
            self.state["qnext"] = max(self.state["qnext"], qmin)
        else:
            # qmin = 0 is a magic value (undefined)
            self.state["qnext"] = qmin if qmin > 0 else int(self.params["lim1"]/2) if self.params["sqside"] == 1 else int(self.params["lim0"]/2)
        
        self.state.setdefault("rels_found", 0)
        self.state["rels_wanted"] = self.params["rels_wanted"]
        if self.state["rels_wanted"] == 0:
            # taking into account duplicates, the initial value
            # 0.91 * (pi(2^lpb0) + pi(2^lpb1)) should be good
            n0 = 2 ** self.progparams[0]["lpb0"]
            n1 =  2 ** self.progparams[0]["lpb1"]
            n01 = int(0.91 * n0 / log (n0) + 0.91 * n1 / log (n1))
            self.state["rels_wanted"] = n01
    
    def run(self):
        super().run()
        have_two_alg = self.send_request(Request.GET_HAVE_TWO_ALG_SIDES)
        if have_two_alg:
            fb0 = self.send_request(Request.GET_FACTORBASE0_FILENAME)
            fb1 = self.send_request(Request.GET_FACTORBASE1_FILENAME)
        else:
            factorbase = self.send_request(Request.GET_FACTORBASE_FILENAME)

        self.logger.info("We want %d relations", self.state["rels_wanted"])
        while self.get_nrels() < self.state["rels_wanted"]:
            q0 = self.state["qnext"]
            q1 = q0 + self.params["qrange"]
            q1 = q1 - (q1 % self.params["qrange"])
            assert q1 > q0
            # We use .gzip by default, unless set to no in parameters
            use_gz = ".gz" if self.params["gzip"] else ""
            outputfilename = \
                self.workdir.make_filename("%d-%d%s" % (q0, q1, use_gz))
            self.check_files_exist([outputfilename], "output",
                                   shouldexist=False)
            if not have_two_alg:
                p = cadoprograms.Las(q0=q0, q1=q1,
                                     factorbase=factorbase,
                                     out=outputfilename, stats_stderr=True,
                                     **self.merged_args[0])
            else:
                p = cadoprograms.Las(q0=q0, q1=q1,
                                     factorbase0=fb0,
                                     factorbase1=fb1,
                                     out=outputfilename, stats_stderr=True,
                                     **self.merged_args[0])
            self.submit_command(p, "%d-%d" % (q0, q1), commit=False)
            self.state.update({"qnext": q1}, commit=True)
        self.logger.info("Reached target of %d relations, now have %d",
                         self.state["rels_wanted"], self.get_nrels())
        self.logger.debug("Exit SievingTask.run(" + self.name + ")")
        return True
    
    def get_achievement(self):
        return self.state["rels_found"] / self.state["rels_wanted"]

    def updateObserver(self, message):
        identifier = self.filter_notification(message)
        if not identifier:
            # This notification was not for me
            return False
        if self.handle_error_result(message):
            return True
        output_files = message.get_output_files()
        if len(output_files) != 1:
            self.logger.warn("Received output with %d files: %s" % (len(output_files), ", ".join(output_files)))
            return False
        stderrfilename = message.get_stderrfile(0)
        ok = self.add_file(output_files[0], stderrfilename, commit=False)
        self.verification(message.get_wu_id(), ok, commit=True)
        return True

    def add_file(self, filename, stats_filename=None, commit=True):
        use_stats_filename = stats_filename
        if stats_filename is None:
            self.logger.info("No statistics output received for file '%s', "
                             "have to scan file", filename)
            use_stats_filename = filename
        rels = self.parse_rel_count(use_stats_filename)
        if rels is None:
            return False
        update = {"rels_found": self.get_nrels() + rels}
        self.state.update(update, commit=False)
        self.add_output_files({filename: rels}, commit=commit)
        if stats_filename:
            self.parse_stats(stats_filename, commit=commit)
        self.logger.info("Found %d relations in '%s', total is now %d/%d",
                         rels, filename, self.get_nrels(),
                         self.state["rels_wanted"])
        return True

    re_rel = re.compile(b"(-?\d*),(\d*):(.*)")
    def verify_relation(self, line, poly):
        """ Check that the primes listed for a relation divide the value of
            the polynomials """
        match = self.re_rel.match(line)
        if match:
            a, b, rest = match.groups()
            a, b = int(a), int(b)
            sides = rest.split(b":")
            assert len(sides) == 2
            for side, primes_as_str in enumerate(sides):
                value = poly.get_polynomial(side).eval_h(a, b)
                primes = [int(s, 16) for s in primes_as_str.split(b",")]
                for prime in primes:
                    # self.logger.debug("Checking if %d divides %d for a=%d, b=%d", prime, value, a, b)
                    if value % prime != 0:
                        self.logger.error("Relation %d,%d invalid: %d does not divide %d",
                                          a, b, prime, value)
                        return False
            return True
        return None

    def parse_rel_count(self, filename):
        (name, ext) = os.path.splitext(filename)
        try:
            if ext == ".gz":
                f = gzip.open(filename, "rb")
            else:
                size = os.path.getsize(filename)
                f = open(filename, "rb")
        except (OSError, IOError) as e:
            if e.errno == 2: # No such file or directory
                self.logger.error("File '%s' does not exist", filename)
                return None
            else:
                raise
        relations_to_check = 10
        poly = self.send_request(Request.GET_POLYNOMIAL)
        try:
            for line in f:
                if relations_to_check > 0:
                    result =  self.verify_relation(line, poly)
                    if result is True:
                        relations_to_check -= 1
                    elif result is False:
                        f.close()
                        return None
                    else: # Did not match: try again
                        pass
                match = re.match(br"# Total (\d+) reports ", line)
                if match:
                    rels = int(match.group(1))
                    f.close()
                    return rels
        except (IOError, TypeError, structerror, EOFError) as e:
            if isinstance(e, IOError) and str(e) == "Not a gzipped file" or \
                    isinstance(e, TypeError) and str(e).startswith("ord() expected a character") or \
                    isinstance(e, structerror) and str(e).startswith("unpack requires a bytes object") or \
                    isinstance(e, EOFError) and str(e).startswith("Compressed file ended before"):
                self.logger.error("Error reading '%s' (corrupted?): %s", filename, e)
                return None
            else:
                raise
        f.close()
        self.logger.error("Number of relations not found in file '%s'", 
                          filename)
        return None
    
    def import_one_file(self, filename):
        if filename in self.get_output_filenames():
            self.logger.info("Re-scanning file %s", filename)
            nrels = self.get_nrels() - self.get_nrels(filename)
            self.state.update({"rels_found": nrels}, commit=False)
            self.forget_output_filenames([filename], commit=True)
        filename_with_stats_extension = filename + ".stats.txt"
        if os.path.isfile(filename_with_stats_extension):
            self.add_file(filename, filename_with_stats_extension)
        else:
            self.add_file(filename)

    def get_statistics_as_strings(self):
        strings = ["Total number of relations: %d" % self.get_nrels()]
        strings += super().get_statistics_as_strings()
        return strings
    
    def get_nrels(self, filename=None):
        """ Return the number of relations found, either the total so far or
        for a given file
        """
        if filename is None:
            return self.state["rels_found"]
        else:
            # Fixme: don't access self.output_files directly
            return self.output_files[filename]
    
    def request_more_relations(self, target):
        wanted = self.state["rels_wanted"]
        if target > wanted:
            self.state["rels_wanted"] = target
            wanted = target
        nrels = self.get_nrels()
        if wanted > nrels:
            self.send_notification(Notification.WANT_TO_RUN, None)
            self.logger.info("New goal for number of relations is %d, "
                             "currently have %d. Need to sieve more",
                             wanted, nrels)
        else:
            self.logger.info("New goal for number of relations is %d, but "
                             "already have %d. No need to sieve more",
                             wanted, nrels)

    def get_total_cpu_or_real_time(self, is_cpu):
        """ Return number of seconds of cpu time spent by las """
        if is_cpu:
            return float(self.statistics.stats.get("stats_total_cpu_time", [0.])[0])
        else:
            return float(self.statistics.stats.get("stats_total_time", [0.])[0])


class Duplicates1Task(Task, FilesCreator, HasStatistics):
    """ Removes duplicate relations """
    @property
    def name(self):
        return "duplicates1"
    @property
    def title(self):
        return "Filtering - Duplicate Removal, splitting pass"
    @property
    def programs(self):
        return ((cadoprograms.Duplicates1,
            ("filelist", "prefix", "out", "nslices_log"),
            {}),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, {"nslices_log": 1})
    @property
    def stat_conversions(self):
        # "End of read: 229176 relations in 0.9s -- 21.0 MB/s -- 253905.7 rels/s"
        # Without leading "# " !
        return (
        (
            "stats_dup1_time",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"# Done: Read \d+ relations in", 1, "s")),
            False
        ),
    )
    @property
    def stat_formats(self):
        return (
            ["CPU time for dup1: {stats_dup1_time[0]}s"],
        )
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.nr_slices = 2**self.params["nslices_log"]
        # Enforce the fact that our children *MUST* use the same
        # nslices_log value as the one we have.
        self.progparams[0]["nslices_log"]=self.params["nslices_log"]
        tablename = self.make_tablename("infiles")
        self.already_split_input = self.make_db_dict(tablename,
                                                     connection=self.db_connection)
        self.slice_relcounts = self.make_db_dict(self.make_tablename("counts"),
                                                 connection=self.db_connection)
        # Default slice counts to 0, in single DB commit
        self.slice_relcounts.setdefault(
            None, {str(i): 0 for i in range(0, self.nr_slices)})
    
    def run(self):
        super().run()

        # Check that previously split files were split into the same number
        # of pieces that we want now
        for (infile, parts) in self.already_split_input.items():
            if parts != self.nr_slices:
                # TODO: ask interactively (or by -recover) whether to delete
                # old output files and generate new ones, if input file is
                # still available
                # If input file is not available but the previously split
                # parts are, we could join them again... not sure if want
                raise Exception("%s was previously split into %d parts, "
                                "now %d parts requested",
                                infile, parts, self.nr_slices)
        
        # Check that previously split files do, in fact, exist.
        # FIXME: Do we want this? It may be slow when there are many files.
        # Reading the directory and comparing the lists would probably be
        # faster than individual lookups.
        self.check_files_exist(self.get_output_filenames(), "output",
                               shouldexist=True)
        
        siever_files = self.send_request(Request.GET_SIEVER_FILENAMES)
        newfiles = [f for f in siever_files
                    if not f in self.already_split_input]
        self.logger.debug ("new files to split are: %s", newfiles)
        
        if not newfiles:
            self.logger.info("No new files to split")
        else:
            self.logger.info("Splitting %d new files", len(newfiles))
            # TODO: can we recover from missing input files? Ask Sieving to
            # generate them again? Just ignore the missing ones?
            self.check_files_exist(newfiles, "input", shouldexist=True)
            # Split the new files
            if self.nr_slices == 1:
                # If we should split into only 1 part, we don't actually
                # split at all. We simply write the input file name
                # to the table of output files, so the next stages will
                # read the original siever output file, thus avoiding
                # having another copy of the data on disk. Since we don't
                # process the file at all, we need to ask the Siever task
                # for the relation count in the files
                # TODO: pass a list or generator expression in the request
                # here?
                total = self.slice_relcounts["0"]
                for f in newfiles:
                    total += self.send_request(Request.GET_SIEVER_RELCOUNT, f)
                self.slice_relcounts.update({"0": total}, commit=False)
                update1 = dict.fromkeys(newfiles, self.nr_slices)
                self.already_split_input.update(update1, commit=False)
                update2 = dict.fromkeys(newfiles, 0)
                self.add_output_files(update2, commit=True)
            else:
                # Make a task-specific subdirectory name under out working
                # directory
                outputdir = self.workdir.make_dirname(subdir="dup1")
                # Create this directory if it does not exist
                # self.logger.info("Creating directory %s", outputdir)
                outputdir.mkdir(parent=True)
                # Create a WorkDir instance for this subdirectory
                dup1_subdir = WorkDir(outputdir)
                # For each slice index [0, ..., nr_slices-1], create under the
                # subdirectory another directory for that slice's output files
                for i in range(0, self.nr_slices):
                    subdir = dup1_subdir.make_filename2(filename=str(i))
                    # self.logger.info("Creating directory %s", subdir)
                    subdir.mkdir(parent=True)
                run_counter = self.state.get("run_counter", 0)
                prefix = "dup1.%s" % run_counter
                (stdoutpath, stderrpath) = \
                        self.make_std_paths(cadoprograms.Duplicates1.name)
                if len(newfiles) <= 10:
                    p = cadoprograms.Duplicates1(*newfiles,
                                                 prefix=prefix,
                                                 out=outputdir,
                                                 stdout=str(stdoutpath),
                                                 stderr=str(stderrpath),
                                                 **self.progparams[0])
                else:
                    filelistname = self.make_filelist(newfiles, prefix="dup1")
                    p = cadoprograms.Duplicates1(filelist=filelistname,
                                                 prefix=prefix,
                                                 out=outputdir,
                                                 stdout=str(stdoutpath),
                                                 stderr=str(stderrpath),
                                                 **self.progparams[0])
                message = self.submit_command(p, "", log_errors=True)
                if message.get_exitcode(0) != 0:
                    raise Exception("Program failed")
                    # Check that the output files exist now
                    # TODO: How to recover from error? Presumably a dup1
                    # process failed, but that should raise a return code
                    # exception
                with stderrpath.open("r") as stderrfile:
                    stderr = stderrfile.read()
                outfilenames = self.parse_output_files(stderr)
                if not outfilenames:
                    self.logger.critical("No output files produced by %s",
                                         p.name)
                    return False
                self.logger.debug("Output file names: %s", outfilenames)
                self.check_files_exist(outfilenames.keys(), "output",
                                       shouldexist=True)
                self.state.update({"run_counter": run_counter + 1},
                                  commit=False)
                current_counts = self.parse_slice_counts(stderr)
                self.parse_stats(stdoutpath, commit=False)
                # Add relation count from the newly processed files to the
                # relations-per-slice dict
                update1 = {str(idx): self.slice_relcounts[str(idx)] + 
                                     current_counts[idx]
                           for idx in range(self.nr_slices)}
                self.slice_relcounts.update(update1, commit=False)
                # Add the newly processed input files to the list of already
                # processed input files
                update2 = dict.fromkeys(newfiles, self.nr_slices)
                self.already_split_input.update(update2, commit=False)
                # Add the newly produced output files and commit everything
                self.add_output_files(outfilenames, commit=True)
        totals = ["%d: %d" % (i, self.slice_relcounts[str(i)])
                  for i in range(0, self.nr_slices)]
        self.logger.info("Relations per slice: %s", ", ".join(totals))
        self.logger.debug("Exit Duplicates1Task.run(" + self.name + ")")
        return True
    
    @staticmethod
    def parse_output_files(stderr):
        files = {}
        for line in stderr.splitlines():
            match = re.match(r'# Opening output file for slice (\d+) : (.+)$',
                             line)
            if match:
                (slicenr, filename) = match.groups()
                files[filename] = int(slicenr)
        return files
    
    def parse_slice_counts(self, stderr):
        """ Takes lines of text and looks for slice counts as printed by dup1
        """
        counts = [None] * self.nr_slices
        for line in stderr.splitlines():
            match = re.match(r'# slice (\d+) received (\d+) relations', line)
            if match:
                (slicenr, nrrels) = map(int, match.groups())
                if not counts[slicenr] is None:
                    raise Exception("Received two values for relation count "
                                    "in slice %d" % slicenr)
                counts[slicenr] = nrrels
        for (slicenr, nrrels) in enumerate(counts):
            if nrrels is None:
                raise Exception("Received no value for relation count in "
                                "slice %d" % slicenr)
        return counts
    
    def get_nr_slices(self):
        return self.nr_slices
    
    def get_nrels(self, idx):
        return self.slice_relcounts[str(idx)]
    
    def request_more_relations(self, target):
        self.send_notification(Notification.WANT_MORE_RELATIONS, target)
        self.send_notification(Notification.WANT_TO_RUN, None)

class Duplicates2Task(Task, FilesCreator, HasStatistics):
    """ Removes duplicate relations """
    @property
    def name(self):
        return "duplicates2"
    @property
    def title(self):
        return "Filtering - Duplicate Removal, removal pass"
    @property
    def programs(self):
        input = { "renumber": Request.GET_RENUMBER_FILENAME}
        if self.params["dlp"]:
            input["badidealinfo"] = Request.GET_BADIDEALINFO_FILENAME
        return ((cadoprograms.Duplicates2, ("rel_count", "filelist"), input),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, 
            {"dlp": False, "nslices_log": 1})

    @property
    def stat_conversions(self):
        # "End of read: 229176 relations in 0.9s -- 21.0 MB/s -- 253905.7 rels/s"
        # Without leading "# " !
        return (
        (
            "stats_dup2_time",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"# Done: Read \d+ relations in", 1, "s")),
            False
        ),
    )
    @property
    def stat_formats(self):
        return (
            ["CPU time for dup2: {stats_dup2_time[0]}s"],
        )
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.nr_slices = 2**self.params["nslices_log"]
        tablename = self.make_tablename("infiles")
        self.already_done_input = self.make_db_dict(tablename, connection=self.db_connection)
        self.slice_relcounts = self.make_db_dict(self.make_tablename("counts"), connection=self.db_connection)
        self.slice_relcounts.setdefault(
            None, {str(i): 0 for i in range(0, self.nr_slices)})
    
    def run(self):
        super().run()

        input_nrel = 0
        for i in range(0, self.nr_slices):
            files = self.send_request(Request.GET_DUP1_FILENAMES, i.__eq__)
            rel_count = self.send_request(Request.GET_DUP1_RELCOUNT, i)
            input_nrel += rel_count
            newfiles = [f for f in files if not f in self.already_done_input]
            if not newfiles:
                self.logger.info("No new files for slice %d, nothing to do", i)
                continue
            # If there are any new files in a slice, we remove duplicates on
            # the whole file set of the slice, as we currently cannot store
            # the duplicate removal state to be able to add more relations
            # in another pass
            # Forget about the previous output filenames of this slice
            # FIXME: Should we delete the files, too?
            self.forget_output_filenames(self.get_output_filenames(i.__eq__),
                                         commit=True)
            del(self.slice_relcounts[str(i)])
            name = "%s.slice%d" % (cadoprograms.Duplicates2.name, i)
            (stdoutpath, stderrpath) = \
                self.make_std_paths(name, do_increment=(i == 0))
             

            if len(files) <= 10:
                p = cadoprograms.Duplicates2(*files,
                                             rel_count=rel_count,
                                             stdout=str(stdoutpath),
                                             stderr=str(stderrpath),
                                             **self.merged_args[0])
            else:
                filelistname = self.make_filelist(files, prefix="dup1")
                p = cadoprograms.Duplicates2(rel_count=rel_count,
                                             filelist=filelistname,
                                             stdout=str(stdoutpath),
                                             stderr=str(stderrpath),
                                             **self.merged_args[0])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            with stderrpath.open("r") as stderrfile:
                nr_rels = self.parse_remaining(stderrfile)
            # Mark input file names and output file names
            for f in files:
                self.already_done_input[f] = True
            outfilenames = {f:i for f in files}
            self.add_output_files(outfilenames, commit=False)
            # XXX How do we add the timings ?
            # self.parse_stats(stdoutpath, commit=True)
            self.logger.info("%d unique relations remain on slice %d",
                             nr_rels, i)
            self.slice_relcounts[str(i)] = nr_rels
        self.update_ratio(input_nrel, self.get_nrels())
        self.logger.info("%d unique relations remain in total",
                         self.get_nrels())
        self.logger.debug("Exit Duplicates2Task.run(" + self.name + ")")
        return True
    
    @staticmethod
    def parse_remaining(text):
        # "     112889 remaining relations"
        for line in text:
            match = re.match(r'At the end:\s*(\d+) remaining relations', line)
            if match:
                remaining = int(match.group(1))
                return remaining
        raise Exception("Received no value for remaining relation count")

    def get_nrels(self):
        nrels = 0
        for i in range(0, self.nr_slices):
            nrels += self.slice_relcounts[str(i)]
        return nrels
    
    def update_ratio(self, input_nrel, output_nrel):
        last_input_nrel = self.state.get("last_input_nrel", 0)
        last_output_nrel = self.state.get("last_output_nrel", 0)
        new_in = input_nrel - last_input_nrel
        new_out = output_nrel - last_output_nrel
        if new_in < 0:
            self.logger.error("Negative number %d of new relations?", new_in)
            return
        if new_in == 0:
            return
        if new_out > new_in:
            self.logger.error("More new output relations (%d) than input (%d)?",
                              new_out, new_in)
            return
        ratio = new_out / new_in
        self.logger.info("Of %d newly added relations %d were unique "
                         "(ratio %f)", new_in, new_out, ratio)
        self.state.update({"last_input_nrel": input_nrel,
            "last_output_nrel": output_nrel, "unique_ratio": ratio})
    
    def request_more_relations(self, target):
        nrels = self.get_nrels()
        if target <= nrels:
            return
        additional_out = target - nrels
        ratio = self.state.get("unique_ratio", 1.)
        ratio = max(0.5, ratio) # avoid stupidly large values of rels_wanted
        additional_in = int(additional_out / ratio)
        newtarget = self.state["last_input_nrel"] + additional_in
        self.logger.info("Got request for %d (%d additional) output relations, "
                         "estimate %d (%d additional) needed in input",
                         target, additional_out, newtarget, additional_in)
        self.send_notification(Notification.WANT_MORE_RELATIONS, newtarget)
        self.send_notification(Notification.WANT_TO_RUN, None)


class PurgeTask(Task):
    """ Removes singletons and computes excess """
    @property
    def name(self):
        return "purge"
    @property
    def title(self):
        return "Filtering - Singleton removal"
    @property
    def programs(self):
        override = ("nrels", "out", "outdel", "nprimes", "filelist", "required_excess")
        return ((cadoprograms.Purge, override, {"_freerel": Request.GET_FREEREL_FILENAME}),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, 
            {"dlp": False, "galois": "none", "gzip": True, "add_ratio": 0.01,
                "required_excess": 0.0})

    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.state.setdefault("input_nrels", 0)
        # We use a computed keep value for DLP
        self.keep = self.progparams[0].pop("keep", None)
    
    def run(self):
        super().run()

        # Enforce the fact that our children *MUST* use the same
        # required_excess value as the one we have.
        self.progparams[0]["required_excess"]=self.params["required_excess"]

        if not (self.params["galois"] in ["1/y", "_y", "autom3.1g", "autom3.2g"]):
            nfree = self.send_request(Request.GET_FREEREL_RELCOUNT)
            nunique = self.send_request(Request.GET_UNIQUE_RELCOUNT)
            if not nunique:
                self.logger.critical("No unique relation count received")
                return False
            input_nrels = nfree + nunique
        else:
            # Freerels and Galois are not yet fully compatible.
            input_nrels = self.send_request(Request.GET_GAL_UNIQUE_RELCOUNT)
            if not input_nrels:
                self.logger.critical("No Galois unique relation count received")
                return False

        nprimes = self.send_request(Request.GET_RENUMBER_PRIMECOUNT)
        # If the user didn't give col_minindex, let's compute it.
        col_minindex = int(self.progparams[0].get("col_minindex", -1))
        if col_minindex == -1:
            # Note: on RSA-120, reducing col_minindex from nprimes/20 to
            # nprimes/40 decreases the matrix size after purge by 3.1%,
            # and the final matrix by 1.4%, while not increasing the memory
            # usage of purge, and increasing the cpu time of purge by only 13%.
            col_minindex = int(nprimes / 40.0)
            # For small cases, we want to avoid degenerated cases, so let's
            # keep most of the ideals: memory is not an issue in that case.
            if (col_minindex < 10000):
                col_minindex = 500
            self.progparams[0].setdefault("col_minindex", col_minindex)
        
        if "purgedfile" in self.state and not self.have_new_input_files() and \
                input_nrels == self.state["input_nrels"]:
            self.logger.info("Already have a purged file, and no new input "
                             "relations available. Nothing to do")
            self.send_notification(Notification.HAVE_ENOUGH_RELATIONS, None)
            return True
        
        self.state.pop("purgedfile", None)
        self.state.pop("input_nrels", None)
        
        if self.params["galois"] == "none":
            self.logger.info("Reading %d unique and %d free relations, total %d"
                             % (nunique, nfree, input_nrels))
        else:
            self.logger.info("Reading %d Galois unique" % input_nrels)

        use_gz = ".gz" if self.params["gzip"] else ""
        purgedfile = self.workdir.make_filename("purged" + use_gz)
        if self.params["dlp"]:
            relsdelfile = self.workdir.make_filename("relsdel" + use_gz)
            nmaps = self.send_request(Request.GET_NMAPS)
            keep = sum(nmaps)
        else:
            relsdelfile = None
            keep = self.keep
        freerel_filename = self.merged_args[0].pop("_freerel", None)
        # Remark: "Galois unique" and "unique" are in the same files
        # because filter_galois works in place. Same request.
        unique_filenames = self.send_request(Request.GET_UNIQUE_FILENAMES)
        if self.params["galois"] == "none" and freerel_filename is not None:
            files = unique_filenames + [str(freerel_filename)]
        else:
            files = unique_filenames
        (stdoutpath, stderrpath) = self.make_std_paths(cadoprograms.Purge.name)
        
        if len(files) <= 10:
            p = cadoprograms.Purge(*files,
                                   nrels=input_nrels, out=purgedfile,
                                   outdel=relsdelfile, keep=keep,
                                   nprimes=nprimes,
                                   stdout=str(stdoutpath),
                                   stderr=str(stderrpath),
                                   **self.progparams[0])
        else:
            filelistname = self.make_filelist(files, prefix=self.name)
            p = cadoprograms.Purge(nrels=input_nrels,
                                   out=purgedfile,
                                   outdel=relsdelfile, keep=keep,
                                   nprimes=nprimes,
                                   filelist=filelistname,
                                   stdout=str(stdoutpath),
                                   stderr=str(stderrpath),
                                   **self.progparams[0])
        message = self.submit_command(p, "")
        stdout = message.read_stdout(0).decode('utf-8')
        stderr = message.read_stderr(0).decode('utf-8')
        if self.parse_output(stdout, input_nrels):
            stats = self.parse_stdout(stdout)
            self.logger.info("After purge, %d relations with %d primes remain "
                             "with weight %s and excess %s", *stats)
            output_version = self.state.get("output_version", 0) + 1
            update = {"purgedfile": purgedfile.get_wdir_relative(),
                      "input_nrels": input_nrels,
                      "output_version": output_version}
            if self.params["dlp"]:
                update["relsdelfile"] = relsdelfile.get_wdir_relative()
            self.state.update(update)
            self.logger.info("Have enough relations")
            self.send_notification(Notification.HAVE_ENOUGH_RELATIONS, None)
        else:
            stats = self.parse_stdout(stdout)
            self.logger.info("After purge, %d relations with %d primes remain "
                             "with excess %d", stats[0], stats[1], stats[3])
            excess = stats[3]
            self.logger.info("Not enough relations")
            if self.params["galois"] == "none":
                self.request_more_relations(nunique, excess)
            else:
                self.request_more_relations(input_nrels, excess)
        self.logger.debug("Exit PurgeTask.run(" + self.name + ")")
        return True
    
    def request_more_relations(self, nunique, excess):
        r"""
        Given 'nunique' relations and an excess of 'excess',
        estimate how many new (unique) relations we need.
        """

        additional = nunique * self.params["add_ratio"]
        # if the excess is negative, we need at least -excess new relations
        if excess < 0:
           additional = max(additional, -excess)
        required_excess = self.params["required_excess"]
        if required_excess > 0.0:
           # we might need more relations due to required_excess
           nprimes = nunique - excess # correct whatever the sign of excess
           # we have nprimes ideals, and we want an excess of at least
           # required_excess * nprimes
           target_excess = int(required_excess * nprimes)
           additional = max(additional, target_excess - excess)
        # Always request at least 10k more
        additional = max(additional, 10000)
        
        self.logger.info("Requesting %d additional relations", additional)
        self.send_notification(Notification.WANT_MORE_RELATIONS,
                               nunique + additional)
        self.send_notification(Notification.WANT_TO_RUN, None)
    
    def get_purged_filename(self):
        return self.get_state_filename("purgedfile")
    
    def get_relsdel_filename(self):
        return self.get_state_filename("relsdelfile")
    
    def parse_output(self, stdout, input_nrels):
        # If stdout ends with
        # (excess / ncols) = ... < .... See -required_excess argument.'
        # then we need more relations from filtering and return False
        input_nprimes = None
        have_enough = True
        # not_enough1 = re.compile(r"excess < (\d+.\d+) \* #primes")
        not_enough1 = re.compile(r"\(excess / ncols\) = \d+.?\d* < \d+.?\d*. "
                                 r"See -required_excess argument.")
        not_enough2 = re.compile(r"number of rows < number of columns \+ keep")
        nrels_nprimes = re.compile(r"\s*nrows=(\d+), ncols=(\d+); "
                                   r"excess=(-?\d+)")
        for line in stdout.splitlines():
            match = not_enough1.match(line)
            if match:
                have_enough = False
                break
            if not_enough2.match(line):
                have_enough = False
                break
            match = nrels_nprimes.match(line)
            if not match is None:
                (nrels, nprimes, excess) = map(int, match.groups())
                assert nrels - nprimes == excess
                # The first occurrence of the message counts input relations
                if input_nprimes is None:
                    assert input_nrels == nrels
                    input_nprimes = nprimes
        
        # At this point we should have:
        # input_nrels, input_nprimes: rels and primes among input
        # nrels, nprimes, excess: rels and primes when purging stopped
        if not input_nprimes is None:
            self.update_excess_per_input(input_nrels, nrels, nprimes)
        return have_enough
    
    def update_excess_per_input(self, input_nrels, nrels, nprimes):
        if input_nrels == 0:
            return # Nothing sensible that we can do
        last_input_nrels = self.state.get("last_input_nrels", 0)
        last_nrels = self.state.get("last_output_nrels", 0)
        last_nprimes = self.state.get("last_output_nprimes", 0)
        if last_input_nrels >= input_nrels:
            self.logger.warn("Previously stored input nrels (%d) is no "
                             "smaller than value from new run (%d)",
                             last_input_nrels, input_nrels)
            return
        if nrels <= last_nrels:
            self.logger.warn("Previously stored nrels (%d) is no "
                             "smaller than value from new run (%d)",
                             last_nrels, nrels)
            return
        if nprimes <= last_nprimes:
            self.logger.warn("Previously stored nprimes (%d) is no "
                             "smaller than value from new run (%d)",
                             last_nprimes, nprimes)
            return
        self.logger.info("Previous run had %d input relations and ended with "
                         "%d relations and %d primes, new run had %d input "
                         "relations and ended with %d relations and %d primes",
                         last_input_nrels, last_nrels, last_nprimes,
                         input_nrels, nrels, nprimes)
        update = {"last_output_nrels": nrels, "last_output_nprimes": nprimes,
                  "last_input_nrels": input_nrels}
        self.state.update(update)
    
    def parse_stdout(self, stdout):
        # Program stdout is expected in the form:
        #   Final values:
        #   nrows=23105 ncols=22945 excess=160
        #   weight=382433 weight*nows=8.84e+09
        # but we allow some extra whitespace
        r = {}
        keys = ("nrows", "ncols", "weight", "excess")
        final_values_line = "Final values:"
        had_final_values = False
        for line in stdout.splitlines():
            # Look for values only after we saw "Final values:" line
            if not had_final_values:
                if re.match(final_values_line, line):
                    had_final_values = True
                else:
                    continue
            for key in keys:
                # Match the key at the start of a line, or after a whitespace
                # Note: (?:) is non-capturing group
                match = re.search(r"(?:^|\s)%s\s*=\s*([-]?\d+)" % key, line)
                if match:
                    if key in r:
                        raise Exception("Found multiple values for %s" % key)
                    r[key] = int(match.group(1))
        if not had_final_values:
            raise Exception("%s: output of %s did not contain '%s'" %
                            (self.title, self.programs[0][0].name, final_values_line))
        for key in keys:
            if not key in r:
                raise Exception("%s: output of %s did not contain value for %s: %s"
                                % (self.title, self.programs[0][0].name, key, stdout))
        return [r[key] for key in keys]

class FilterGaloisTask(Task):
    """ Galois Filtering """
    @property
    def name(self):
        return "filtergalois"
    @property
    def title(self):
        return "Filtering - Galois"
    @property
    def programs(self):
        input = {"poly": Request.GET_POLYNOMIAL_FILENAME,
                 "renumber": Request.GET_RENUMBER_FILENAME}
        return ((cadoprograms.GaloisFilter, ("nrels",), input),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, {"galois": "none"})

    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)

    def run(self):
        # This task must be run only if galois is recognized by filter_galois
        if not (self.params["galois"] in ["1/y", "_y", "autom3.1g", "autom3.2g"]):
            return True

        super().run()

        files = self.send_request(Request.GET_UNIQUE_FILENAMES)
        nrels = self.send_request(Request.GET_UNIQUE_RELCOUNT)

        (stdoutpath, stderrpath) = self.make_std_paths(cadoprograms.GaloisFilter.name)
        # TODO: if there are too many files, pass a filelist (cf PurgeTask)
        p = cadoprograms.GaloisFilter(*files,
                nrels=nrels,
                stdout=str(stdoutpath),
                stderr=str(stderrpath),
                **self.merged_args[0])
        message = self.submit_command(p, "", log_errors=True)
        if message.get_exitcode(0) != 0:
            raise Exception("Program failed")
        with stderrpath.open("r") as stderrfile:
            noutrels = self.parse_remaining(stderrfile)
        self.state["noutrels"] = noutrels

        self.logger.debug("Exit FilterGaloisTask.run(" + self.name + ")")
        return True

    def get_nrels(self):
        return self.state["noutrels"]

    def parse_remaining(self, text):
        # "Number of output relations: 303571"
        for line in text:
            match = re.match(r'Number of output relations:\s*(\d+)', line)
            if match:
                remaining = int(match.group(1))
                return remaining
        raise Exception("Received no value for output relation count")

    def request_more_relations(self, target):
        self.send_notification(Notification.WANT_TO_RUN, None)

class MergeDLPTask(Task):
    """ Merges relations """
    @property
    def name(self):
        return "mergedlp"
    @property
    def title(self):
        return "Filtering - Merging"
    @property
    def programs(self):
        input = {"purged": Request.GET_PURGED_FILENAME}
        return ((cadoprograms.MergeDLP, ("out", "keep"), input),
                (cadoprograms.ReplayDLP, ("ideals", "history", "index", "out"), input))
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, {"gzip": True})
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.progparams[0]["skip"] = 0

    def run(self):
        super().run()

        if not "mergedfile" in self.state or self.have_new_input_files():

            if "idealfile" in self.state:
                del(self.state["idealfile"])
            if "indexfile" in self.state:
                del(self.state["indexfile"])
            if "mergedfile" in self.state:
                del(self.state["mergedfile"])
            if "densefile" in self.state:
                del(self.state["densefile"])
            
            nmaps = self.send_request(Request.GET_NMAPS)
            keep = nmaps[0] + nmaps[1]
            # We use .gzip by default, unless set to no in parameters
            use_gz = ".gz" if self.params["gzip"] else ""
            historyfile = self.workdir.make_filename("history" + use_gz)
            (stdoutpath, stderrpath) = self.make_std_paths(cadoprograms.MergeDLP.name)
            p = cadoprograms.MergeDLP(out=historyfile,
                                   keep=keep,
                                   stdout=str(stdoutpath),
                                   stderr=str(stderrpath),
                                   **self.merged_args[0])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            stdout = message.read_stdout(0).decode("utf-8")
            matsize = 0
            matweight = 0
            for line in stdout.splitlines():
                match = re.match(r'Final matrix has N=(\d+) nc=\d+ \(\d+\) W=(\d+)', line)
                if match:
                    matsize=int(match.group(1))
                    matweight=int(match.group(2))
            if (matsize == 0) or (matweight == 0):
                raise Exception("Could not read matrix size and weight")
            self.logger.info("Merged matrix has %d rows and total weight %d (%.1f entries per row on average)"
                    % (matsize, matweight, float(matweight)/float(matsize)))

            indexfile = self.workdir.make_filename("index" + use_gz)
            mergedfile = self.workdir.make_filename("sparse.bin")
            (stdoutpath, stderrpath) = self.make_std_paths(cadoprograms.Replay.name)
            idealfile = self.workdir.make_filename("ideal")
            p = cadoprograms.ReplayDLP(ideals=idealfile,
                                    history=historyfile, index=indexfile,
                                    out=mergedfile, stdout=str(stdoutpath),
                                    stderr=str(stderrpath),
                                    **self.merged_args[1])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            
            if not idealfile.isfile():
                raise Exception("Output file %s does not exist" % idealfile)
            if not indexfile.isfile():
                raise Exception("Output file %s does not exist" % indexfile)
            if not mergedfile.isfile():
                raise Exception("Output file %s does not exist" % mergedfile)
            output_version = self.state.get("output_version", 0) + 1
            self.remember_input_versions(commit=False)
            update = {"indexfile": indexfile.get_wdir_relative(),
                      "mergedfile": mergedfile.get_wdir_relative(),
                      "idealfile": idealfile.get_wdir_relative(),
                      "output_version": output_version}
            densefilename = self.workdir.make_filename("dense.bin")
            if densefilename.isfile():
                update["densefile"] = densefilename.get_wdir_relative()
            self.state.update(update)
            
        self.logger.debug("Exit MergeDLPTask.run(" + self.name + ")")
        return True
    
    def get_index_filename(self):
        return self.get_state_filename("indexfile")
    
    def get_ideal_filename(self):
        return self.get_state_filename("idealfile")
    
    def get_merged_filename(self):
        return self.get_state_filename("mergedfile")
    
    def get_dense_filename(self):
        return self.get_state_filename("densefile")


class MergeTask(Task):
    """ Merges relations """
    @property
    def name(self):
        return "merge"
    @property
    def title(self):
        return "Filtering - Merging"
    @property
    def programs(self):
        input = {"purged": Request.GET_PURGED_FILENAME}
        return ((cadoprograms.Merge, ("out",), input),
                (cadoprograms.Replay, ("out", "history", "index"), input))
    @property
    def paramnames(self):
        return self.join_params(super().paramnames,  \
            {"skip": None, "keep": None, "gzip": True})
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        skip = int(self.progparams[0].get("skip", 32))
        self.progparams[0].setdefault("skip", skip)
        self.progparams[0].setdefault("keep", skip + 128)

    def run(self):
        super().run()

        if "mergedfile" in self.state:
            fn = self.workdir.path_in_workdir(self.state["mergedfile"])
            if not fn.isfile():
                self.logger.warning("Output file %s disappeared, generating it again", fn)
                del(self.state["mergedfile"])
        if not "mergedfile" in self.state or self.have_new_input_files():
            if "indexfile" in self.state:
                del(self.state["indexfile"])
            if "densefile" in self.state:
                del(self.state["densefile"])
            
            # We use .gzip by default, unless set to no in parameters
            use_gz = ".gz" if self.params["gzip"] else ""
            historyfile = self.workdir.make_filename("history" + use_gz)
            (stdoutpath, stderrpath) = self.make_std_paths(cadoprograms.Merge.name)
            p = cadoprograms.Merge(out=historyfile,
                                   stdout=str(stdoutpath),
                                   stderr=str(stderrpath),
                                   **self.merged_args[0])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            stdout = message.read_stdout(0).decode("utf-8")
            matsize = 0
            matweight = 0
            for line in stdout.splitlines():
                match = re.match(r'Final matrix has N=(\d+) nc=\d+ \(\d+\) W=(\d+)', line)
                if match:
                    matsize=int(match.group(1))
                    matweight=int(match.group(2))
            if (matsize == 0) or (matweight == 0):
                raise Exception("Could not read matrix size and weight")
            self.logger.info("Merged matrix has %d rows and total weight %d (%.1f entries per row on average)"
                    % (matsize, matweight, float(matweight)/float(matsize)))

            indexfile = self.workdir.make_filename("index" + use_gz)
            mergedfile = self.workdir.make_filename("sparse.bin")
            (stdoutpath, stderrpath) = self.make_std_paths(cadoprograms.Replay.name)
            p = cadoprograms.Replay(history=historyfile, index=indexfile,
                                    out=mergedfile, stdout=str(stdoutpath),
                                    stderr=str(stderrpath),
                                    **self.merged_args[1])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            
            if not indexfile.isfile():
                raise Exception("Output file %s does not exist" % indexfile)
            if not mergedfile.isfile():
                raise Exception("Output file %s does not exist" % mergedfile)
            self.remember_input_versions(commit=False)
            output_version = self.state.get("output_version", 0) + 1
            update = {"indexfile": indexfile.get_wdir_relative(),
                      "mergedfile": mergedfile.get_wdir_relative(),
                      "output_version": output_version}
            densefilename = self.workdir.make_filename("dense.bin")
            if densefilename.isfile():
                update["densefile"] = densefilename.get_wdir_relative()
            self.state.update(update)
            
        self.logger.debug("Exit MergeTask.run(" + self.name + ")")
        return True
    
    def get_index_filename(self):
        return self.get_state_filename("indexfile")
    
    def get_merged_filename(self):
        return self.get_state_filename("mergedfile")
    
    def get_dense_filename(self):
        return self.get_state_filename("densefile")


class NumberTheoryTask(Task):
    """ Number theory tasks for dlp"""
    @property
    def name(self):
        return "numbertheory"
    @property
    def title(self):
        return "Number Theory for DLP"
    @property
    def programs(self):
        return ((cadoprograms.NumberTheory, ("badidealinfo", "badideals"),
                 {"poly": Request.GET_POLYNOMIAL_FILENAME}),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, {"nsm0": -1, "nsm1": -1})
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)

    def run(self):
        super().run()

        # Check if we already compute the bad ideals (we check only
        # one of the files, assuming everything was correct during the
        # first run).
        if "badidealsfile" in self.state:
            self.logger.info("NumberTheory task has already run, reusing the result.");
            return True

        # Create output files and start the computation
        badidealsfile = self.workdir.make_filename("badideals")
        badidealinfofile = self.workdir.make_filename("badidealinfo")
        (stdoutpath, stderrpath) = self.make_std_paths(cadoprograms.NumberTheory.name)
        p = cadoprograms.NumberTheory(badidealinfo=badidealinfofile,
                               badideals=badidealsfile,
                               stdout=str(stdoutpath),
                               stderr=str(stderrpath),
                               **self.merged_args[0])
        message = self.submit_command(p, "", log_errors=True)
        if message.get_exitcode(0) != 0:
            raise Exception("Program failed")

        stdout = message.read_stdout(0).decode("utf-8")
        update = {}
        for line in stdout.splitlines():
            match = re.match(r'# nmaps0 (\d+)', line)
            if match:
                update["nmaps0"] = int(match.group(1))
            match = re.match(r'# nmaps1 (\d+)', line)
            if match:
                update["nmaps1"] = int(match.group(1))
        # Allow user-given parameter to override what we compute:
        if self.params["nsm0"] != -1:
            update["nmaps0"] = self.params["nsm0"]
        if self.params["nsm1"] != -1:
            update["nmaps1"] = self.params["nsm1"]
        update["badidealinfofile"] = badidealinfofile.get_wdir_relative()
        update["badidealsfile"] = badidealsfile.get_wdir_relative()
        
        if not "nmaps0" in update:
            raise Exception("Stdout does not give nmaps0")
        if not "nmaps1" in update:
            raise Exception("Stdout does not give nmaps1")
        if not badidealsfile.isfile():
            raise Exception("Output file %s does not exist" % badidealsfile)
        if not badidealinfofile.isfile():
            raise Exception("Output file %s does not exist" % badidealinfofile)
        # Update the state entries atomically
        self.state.update(update)

        self.logger.debug("Exit NumberTheoryTask.run(" + self.name + ")")
        return True

    def get_badidealinfo_filename(self):
        return self.get_state_filename("badidealinfofile")
    
    def get_badideals_filename(self):
        return self.get_state_filename("badidealsfile")
    
    def get_nmaps(self):
        return (self.state["nmaps0"], self.state["nmaps1"])

class bwc_output_filter(RealTimeOutputFilter):
    def filter(self, data):
        super().filter(data)
        if ("ETA" or "Timings") in data:
            self.logger.info(data.rstrip())
            

# I've just ditched the statistics bit, cause I don't know to make its
# despair cry a little bit more useful.
class LinAlgDLPTask(Task):
    """ Runs the linear algebra step for DLP """
    @property
    def name(self):
        return "linalgdlp"
    @property
    def title(self):
        return "Linear Algebra"
    @property
    def programs(self):
        override = ("complete", "rhs", "matrix",  "wdir",
                "nullspace", "m", "n")
        return ((cadoprograms.BWC, override,
                 {"merged": Request.GET_MERGED_FILENAME,
                  "sm": Request.GET_SM_FILENAME}),)
    @property
    def paramnames(self):
        # the default value for m and n is to use the number of SMs for
        # n, and then m=2*n
        return self.join_params(super().paramnames,
                {"m": 0, "n": 0, "ell": int, "force_wipeout": False})
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.state.setdefault("ran_already", False)
    
    def run(self):
        super().run()

        if self.state["ran_already"] and self.params["force_wipeout"]:
                self.logger.warn("Ran before, but force_wipeout is set. "
                                 "Wiping out working directory.")
                self.workdir.make_dirname(subdir="bwc").rmtree()
                self.state["ran_already"] = False
                self.state.pop("virtual_logs", None)

        if not "virtual_logs" in self.state or self.have_new_input_files():
            workdir = self.workdir.make_dirname(subdir="bwc")
            workdir.mkdir(parent=True)
            mergedfile = self.merged_args[0].pop("merged")
            smfile = self.merged_args[0].pop("sm")
            if mergedfile is None:
                self.logger.critical("No merged file received.")
                return False
            (stdoutpath, stderrpath) = self.make_std_paths(cadoprograms.BWC.name)
            matrix = mergedfile.realpath()
            wdir = workdir.realpath()
            self.state["ran_already"] = True
            nmaps = self.send_request(Request.GET_NMAPS)
            nsm = nmaps[0] + nmaps[1]
            if self.params["n"] == 0:
                self.logger.info("Using %d as default value for n to account for Schirokauer maps"
                        % nsm)
                n = nsm
            else:
                n = self.params["n"]
                if n < nsm:
                    self.logger.critical("n must be greater than or equal to the number of Schirokauer maps, which is %d (got n=%d)" % (nsm,n))
                    raise Exception("Program failed")
            if self.params["m"] == 0:
                m = 2*n
                self.logger.info("Using 2*n=%d as default value for m" % m)
            else:
                m = self.params["m"]
            if n == 0:
                self.logger.error("Error: homogeneous Linalg is not implemented")
                raise Exception("Program failed")

            p = cadoprograms.BWC(complete=True,
                                 matrix=matrix,  wdir=wdir,
                                 rhs=smfile,
                                 prime=self.params["ell"],
                                 nullspace="right",
                                 stdout=str(stdoutpath),
                                 stderr=str(stderrpath),
                                 m=m,
                                 n=n,
                                 **self.progparams[0])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            virtual_logs_filename = self.workdir.make_filename("K.sols0-1.0.txt", subdir="bwc")
            if not virtual_logs_filename.isfile():
                raise Exception("Kernel file %s does not exist" % virtual_logs_filename)
            self.remember_input_versions(commit=False)
            output_version = self.state.get("output_version", 0) + 1
            update = {"virtual_logs": virtual_logs_filename.get_wdir_relative(),
                      "output_version": output_version}
            self.state.update(update, commit=True)
        self.logger.debug("Exit LinAlgTask.run(" + self.name + ")")
        return True

    def get_virtual_logs_filename(self):
        return self.get_state_filename("virtual_logs")
    
    def get_prefix(self):
        return "%s%s%s.%s" % (self.params["workdir"].rstrip(os.sep), os.sep,
                              self.params["name"], "dep")


class LinAlgTask(Task, HasStatistics):
    """ Runs the linear algebra step """
    @property
    def name(self):
        return "linalg"
    @property
    def title(self):
        return "Linear Algebra"
    @property
    def programs(self):
        return ((cadoprograms.BWC, ("complete", "matrix",  "wdir", "nullspace"),
                 {"merged": Request.GET_MERGED_FILENAME}),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, {"force_wipeout": False})

    @property
    def stat_conversions(self):
        return (
        (
            "krylov_wct",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"Timings for krylov: .wct.", 1)),
            True
        ),
        (
            "krylov_cpu",
            (int, float),
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"krylov done N=(\d+) ; CPU:", 1)),
            True
        ),
        (
            "krylov_cpu_wait",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"krylov done N=\d+ ; cpu-wait:", 1)),
            True
        ),
        (
            "krylov_comm",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"krylov done N=\d+ ; COMM:", 1)),
            True
        ),
        (
            "krylov_comm_wait",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"krylov done N=\d+ ; comm-wait:", 1)),
            True
        ),
        (
            "lingen_wct",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp("Timings for lingen: .wct.", 1)),
            False
        ),
        (
            "lingen_cpu",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp("Timings for lingen: .cpu.", 1)),
            False
        ),
        (
            "mksol_wct",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"Timings for mksol: .wct.", 1)),
            True
        ),
        (
            "mksol_cpu",
            (int, float),
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"mksol done N=(\d+) ; CPU:", 1)),
            True
        ),
        (
            "mksol_cpu_wait",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"mksol done N=\d+ ; cpu-wait:", 1)),
            True
        ),
        (
            "mksol_comm",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"mksol done N=\d+ ; COMM:", 1)),
            True
        ),
        (
            "mksol_comm_wait",
            float,
            "0",
            Statistics.add_list,
            re.compile(re_cap_n_fp(r"mksol done N=\d+ ; comm-wait:", 1)),
            True
        ),
    )
    @property
    def stat_formats(self):
        return (
            ["Krylov: WCT time {krylov_wct[0]}",
                ", iteration CPU time {krylov_cpu[1]:g}",
                ", COMM {krylov_comm[0]}",
                ", cpu-wait {krylov_cpu_wait[0]}",
                ", comm-wait {krylov_comm_wait[0]}",
                " ({krylov_cpu[0]:d} iterations)"
                ],
            ["Lingen CPU time {lingen_cpu[0]}", ", WCT time {lingen_wct[0]}"],
            ["Mksol: WCT time {mksol_wct[0]}",
                ", iteration CPU time {mksol_cpu[1]:g}",
                ", COMM {mksol_comm[0]}",
                ", cpu-wait {mksol_cpu_wait[0]}",
                ", comm-wait {mksol_comm_wait[0]}",
                " ({mksol_cpu[0]:d} iterations)"
                ],
        )
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.state.setdefault("ran_already", False)
    
    def run(self):
        super().run()

        if self.state["ran_already"] and self.params["force_wipeout"]:
                self.logger.warn("Ran before, but force_wipeout is set. "
                                 "Wiping out working directory.")
                self.workdir.make_dirname(subdir="bwc").rmtree()
                self.state["ran_already"] = False
                self.state.pop("dependency", None)

        if not "dependency" in self.state or self.have_new_input_files():
            workdir = self.workdir.make_dirname(subdir="bwc")
            workdir.mkdir(parent=True)
            mergedfile = self.merged_args[0].pop("merged")
            if mergedfile is None:
                self.logger.critical("No merged file received.")
                return False
            (stdoutpath, stderrpath) = self.make_std_paths(cadoprograms.BWC.name)
            matrix = mergedfile.realpath()
            wdir = workdir.realpath()
            self.state["ran_already"] = True
            self.remember_input_versions(commit=True)
            with bwc_output_filter(self.logger, str(stdoutpath)) as outfilter:
                p = cadoprograms.BWC(complete=True,
                                     matrix=matrix,  wdir=wdir, nullspace="left",
                                     stdout=outfilter,
                                     stderr=str(stderrpath),
                                     **self.progparams[0])
                message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            dependencyfilename = self.workdir.make_filename("W", subdir="bwc")
            if not dependencyfilename.isfile():
                raise Exception("Kernel file %s does not exist" % dependencyfilename)
            self.logger.debug("Parsing stats from %s" % stdoutpath)
            self.parse_stats(stdoutpath, commit=False)
            output_version = self.state.get("output_version", 0) + 1
            update = {"dependency": dependencyfilename.get_wdir_relative(),
                      "output_version": output_version}
            self.state.update(update, commit=True)
        self.logger.debug("Exit LinAlgTask.run(" + self.name + ")")
        return True

    def get_dependency_filename(self):
        return self.get_state_filename("dependency")
    
    def get_prefix(self):
        return "%s%s%s.%s" % (self.params["workdir"].rstrip(os.sep), os.sep,
                              self.params["name"], "dep")


class CharactersTask(Task):
    """ Computes Quadratic Characters """
    @property
    def name(self):
        return "characters"
    @property
    def title(self):
        return "Quadratic Characters"
    @property
    def programs(self):
        input = {"poly": Request.GET_POLYNOMIAL_FILENAME,
                 "wfile": Request.GET_DEPENDENCY_FILENAME,
                 "purged": Request.GET_PURGED_FILENAME,
                 "index": Request.GET_INDEX_FILENAME,
                 "heavyblock": Request.GET_DENSE_FILENAME}
        return ((cadoprograms.Characters, ("out",), input),)
    @property
    def paramnames(self):
        return super().paramnames

    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
    
    def run(self):
        super().run()

        if not "kernel" in self.state or self.have_new_input_files():
            kernelfilename = self.workdir.make_filename("kernel")
            (stdoutpath, stderrpath) = \
                    self.make_std_paths(cadoprograms.Characters.name)
            p = cadoprograms.Characters(out=kernelfilename,
                    stdout=str(stdoutpath),
                    stderr=str(stderrpath),
                    **self.merged_args[0])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            if not kernelfilename.isfile():
                raise Exception("Output file %s does not exist" % kernelfilename)
            self.remember_input_versions(commit=False)
            update = {"kernel": kernelfilename.get_wdir_relative()}
            self.state.update(update)
        self.logger.debug("Exit CharactersTask.run(" + self.name + ")")
        return True
    
    def get_kernel_filename(self):
        return self.get_state_filename("kernel")


class SqrtTask(Task):
    """ Runs the square root """
    @property
    def name(self):
        return "sqrt"
    @property
    def title(self):
        return "Square Root"
    @property
    def programs(self):
        input = {"poly": Request.GET_POLYNOMIAL_FILENAME,
                 "purged": Request.GET_PURGED_FILENAME,
                 "index": Request.GET_INDEX_FILENAME,
                 "kernel": Request.GET_KERNEL_FILENAME}
        return ((cadoprograms.Sqrt, ("ab", "prefix", "side0", "side1", "gcd", "dep"),
                 input), )
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, {"N": int, "gzip": True, "first_dep": [int]})
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.factors = self.make_db_dict(self.make_tablename("factors"), connection=self.db_connection)
        self.add_factor(self.params["N"])
        if "first_dep" in self.params:
            self.state["next_dep"] = self.params["first_dep"]

    def run(self):
        super().run()

        if not self.is_done() or self.have_new_input_files():
            prefix = self.send_request(Request.GET_LINALG_PREFIX)
            if self.params["gzip"]:
                prefix += ".gz"
            (stdoutpath, stderrpath) = \
                self.make_std_paths(cadoprograms.Sqrt.name)
            self.logger.info("Creating file of (a,b) values")
            p = cadoprograms.Sqrt(ab=True,
                    prefix=prefix, stdout=str(stdoutpath),
                    stderr=str(stderrpath), **self.merged_args[0])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            
            t = self.progparams[0].get("threads", 1)
            while not self.is_done():
                dep = self.state.get("next_dep", 0)
                #if t == 1:
                   #self.logger.info("Trying dependency %d", dep)
                #else:
                   #self.logger.info("Trying dependencies %d to %d",
                                    #dep, dep+t-1)
                (stdoutpath, stderrpath) = \
                    self.make_std_paths(cadoprograms.Sqrt.name)
                p = cadoprograms.Sqrt(ab=False, side1=True,
                        side0=True, gcd=True, dep=dep, prefix=prefix,
                        stdout=str(stdoutpath), stderr=str(stderrpath), 
                        **self.merged_args[0])
                message = self.submit_command(p, "dep%d" % dep, log_errors=True)
                if message.get_exitcode(0) != 0:
                    raise Exception("Program failed")
                with stdoutpath.open("r") as stdoutfile:
                    stdout = stdoutfile.read()
                lines = stdout.splitlines()
                for line in lines:
                    if line == "Failed":
                        continue # try next lines (if any) in multi-thread mode
                    self.add_factor(int(line))
                self.state.update({"next_dep": dep+t})
            self.remember_input_versions(commit=True)
            self.logger.info("finished")
        self.logger.info("Factors: %s" % " ".join(self.get_factors()))
        self.logger.debug("Exit SqrtTask.run(" + self.name + ")")
        return True
    
    def is_done(self):
        for (factor, isprime) in self.factors.items():
            if not isprime:
                return False
        return True
    
    def add_factor(self, factor):
        assert factor > 0
        if str(factor) in self.factors:
            return
        for oldfac in list(map(int, self.factors.keys())):
            g = gcd(factor, oldfac)
            if 1 < g and g < factor:
                self.add_factor(g)
                self.add_factor(factor // g)
                break
            if 1 < g and g < oldfac:
                # We get here only if newfac is a proper factor of oldfac
                assert factor == g
                del(self.factors[str(oldfac)])
                self.add_factor(g)
                self.add_factor(oldfac // g)
                break
        else:
            # We get here if the new factor is coprime to all previously
            # known factors
            isprime = SqrtTask.miller_rabin_tests(factor, 10)
            self.factors[str(factor)] = isprime
    
    def get_factors(self):
        return self.factors.keys()
    
    @staticmethod
    def miller_rabin_pass(number, base):
        """
        >>> SqrtTask.miller_rabin_pass(3, 2)
        True
        >>> SqrtTask.miller_rabin_pass(9, 2)
        False
        >>> SqrtTask.miller_rabin_pass(91, 2)
        False
        >>> SqrtTask.miller_rabin_pass(1009, 2)
        True
        >>> SqrtTask.miller_rabin_pass(10000000019, 2)
        True
        >>> SqrtTask.miller_rabin_pass(10000000019*10000000021, 2)
        False
        
        # Check some pseudoprimes. First a few Fermat pseudoprimes which
        # Miller-Rabin should recognize as composite
        >>> SqrtTask.miller_rabin_pass(341, 2)
        False
        >>> SqrtTask.miller_rabin_pass(561, 2)
        False
        >>> SqrtTask.miller_rabin_pass(645, 2)
        False
        
        # Now some strong pseudo-primes
        >>> SqrtTask.miller_rabin_pass(2047, 2)
        True
        >>> SqrtTask.miller_rabin_pass(703, 3)
        True
        >>> SqrtTask.miller_rabin_pass(781, 5)
        True
        """
        po2 = 0
        exponent = number - 1
        while exponent % 2 == 0:
            exponent >>= 1
            po2 += 1
        
        result = pow(base, exponent, number)
        if result == 1:
            return True
        for i in range(0, po2 - 1):
            if result == number - 1:
                return True
            result = pow(result, 2, number)
        return result == number - 1
    
    @staticmethod
    def miller_rabin_tests(number, passes):
        if number <= 3:
            return number >= 2
        if number % 2 == 0:
            return False
        for i in range(0, passes):
            # random.randrange(n) produces random integer in [0, n-1].
            # We want [2, n-2]
            base = random.randrange(number - 3) + 2
            if not SqrtTask.miller_rabin_pass(number, base):
                return False
        return True

    @staticmethod
    def nextprime(N):
        """ Return the smallest strong probable prime no smaller than N
        >>> prps = [SqrtTask.nextprime(i) for i in range(30)]
        >>> prps == [2, 2, 2, 3, 5, 5, 7, 7, 11, 11, 11, 11, 13, 13, 17, 17, \
                     17, 17, 19, 19, 23, 23, 23, 23, 29, 29, 29, 29, 29, 29]
        True
        """
        if N <= 2:
            return 2
        if N % 2 == 0:
            N += 1
        while not SqrtTask.miller_rabin_tests(N, 5):
            N += 2     
        return N

class SMTask(Task):
    """ Computes Schirokauer Maps """
    @property
    def name(self):
        return "sm"
    @property
    def title(self):
        return "Schirokauer Maps"
    @property
    def programs(self):
        override = ("nsm", "out")
        input = {"poly": Request.GET_POLYNOMIAL_FILENAME,
                 "purged": Request.GET_PURGED_FILENAME,
                 "index": Request.GET_INDEX_FILENAME}
        return ((cadoprograms.SM, override, input),)
    @property
    def paramnames(self):
        return super().paramnames

    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
    
    def run(self):
        super().run()

        if not "sm" in self.state or self.have_new_input_files():
            nmaps = self.send_request(Request.GET_NMAPS)
            if nmaps[0]+nmaps[1] == 0:
                self.logger.info("Number of SM is 0: skipping this part.")
                return True
            smfilename = self.workdir.make_filename("sm")

            (stdoutpath, stderrpath) = \
                    self.make_std_paths(cadoprograms.SM.name)
            p = cadoprograms.SM(nsm=str(nmaps[0])+","+str(nmaps[1]),
                    out=smfilename,
                    stdout=str(stdoutpath),
                    stderr=str(stderrpath),
                    **self.merged_args[0])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            if not smfilename.isfile():
                raise Exception("Output file %s does not exist" % smfilename)
            self.state["sm"] = smfilename.get_wdir_relative()
        self.logger.debug("Exit SMTask.run(" + self.name + ")")
        return True
    
    def get_sm_filename(self):
        return self.get_state_filename("sm")

class ReconstructLogTask(Task):
    """ Logarithms Reconstruction Task """
    @property
    def name(self):
        return "reconstructlog"
    @property
    def title(self):
        return "Logarithms Reconstruction"
    @property
    def programs(self):
        input = {
                "ker": Request.GET_KERNEL_FILENAME,
                "poly": Request.GET_POLYNOMIAL_FILENAME,
                "renumber": Request.GET_RENUMBER_FILENAME,
                "purged": Request.GET_PURGED_FILENAME,
                "ideals": Request.GET_IDEAL_FILENAME,
                "relsdel": Request.GET_RELSDEL_FILENAME,
                }
        override = ("dlog", "nrels")
        return ((cadoprograms.ReconstructLog, override, input),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames, {"checkdlp": True})

    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
    
    def run(self):
        super().run()

        if (not "dlog" in self.state) or self.have_new_input_files():
            dlogfilename = self.workdir.make_filename("dlog")
            nmaps = self.send_request(Request.GET_NMAPS)

            nfree = self.send_request(Request.GET_FREEREL_RELCOUNT)
            nunique = self.send_request(Request.GET_UNIQUE_RELCOUNT)

            (stdoutpath, stderrpath) = \
                    self.make_std_paths(cadoprograms.ReconstructLog.name)
            p = cadoprograms.ReconstructLog(
                    dlog=dlogfilename,
                    nsm=str(nmaps[0])+","+str(nmaps[1]),
                    nrels=nfree+nunique,
                    stdout=str(stdoutpath),
                    stderr=str(stderrpath),
                    **self.merged_args[0])
            message = self.submit_command(p, "", log_errors=True)
            if message.get_exitcode(0) != 0:
                raise Exception("Program failed")
            if not dlogfilename.isfile():
                raise Exception("Output file %s does not exist" % dlogfilename)
            self.state["dlog"] = dlogfilename.get_wdir_relative()
            self.remember_input_versions()
        self.logger.debug("Exit ReconstructLogTask.run(" + self.name + ")")
        return True
    
    def get_dlog_filename(self):
        return self.get_state_filename("dlog")
    
    def get_log2log3(self):
        if self.params["checkdlp"]:
            filename = self.get_state_filename("dlog").get_wdir_relative()
            fullfile = self.params["workdir"].rstrip(os.sep) + os.sep + filename
            log2 = None
            log3 = None
            myfile = open(fullfile, "rb")
            data = myfile.read()
            for line in data.splitlines():
                match = re.match(br'(\w+) 2 0 rat (\d+)', line)
                if match:
                    log2 = match.group(2)
                match = re.match(br'(\w+) 3 0 rat (\d+)', line)
                if match:
                    log3 = match.group(2)
                if log2 != None and log3 != None:
                    myfile.close()
                    return [ log2, log3 ]
            raise Exception("Could not find log2 and log3 in %s" % filename)
        else:
            return [ 0, 0 ]

# TODO: This is a bit ugly. We're leaning on the functionality that
# descent.py infers the complete set of file names from the prefix (or
# from the database, if it so wishes). However, the cadofactor way would
# be to pass each and every needed file name as provided by the mediator.
class DescentTask(Task):
    """ Individual logarithm Task """
    @property
    def name(self):
        return "descent"
    @property
    def title(self):
        return "Individual logarithm"
    @property
    def programs(self):
        input = {
                "prefix": Request.GET_WORKDIR_JOBNAME,
                "datadir": Request.GET_WORKDIR_PATH,
                }
        override = ("cadobindir",)
        return ((cadoprograms.Descent, override, input),)
    @property
    def paramnames(self):
        return self.join_params(super().paramnames,
                {"target": int, "execpath": str})

    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
    
    def run(self):
        super().run()

        (stdoutpath, stderrpath) = \
                self.make_std_paths(cadoprograms.Descent.name)
        p = cadoprograms.Descent(
                cadobindir=self.params["execpath"],
                stdout=str(stdoutpath),
                stderr=str(stderrpath),
                **self.merged_args[0])
        message = self.submit_command(p, "", log_errors=True)
        if message.get_exitcode(0) != 0:
            raise Exception("Program failed")

        stdout = message.read_stdout(0).decode("utf-8")
        for line in stdout.splitlines():
            match = re.match(r'log\(target\)=(\d+)', line)
            if match:
                self.state["logtarget"] = match.group(1)
                break
        return True

    # XXX I'm not sure that self.state really is the place to store
    # logtarget. Especially given that we're storing it detached from the
    # target, which surely looks odd.
    def get_logtarget(self):
        return self.state["logtarget"]
    
class StartServerTask(DoesLogging, cadoparams.UseParameters, HasState):
    """ Starts HTTP server """
    @property
    def name(self):
        return "server"
    @property
    def title(self):
        return "Server Launcher"
    @property
    def paramnames(self):
        return {"name": str, "workdir": None, "address": None, "port": 0,
                "threaded": False, "ssl": True, "whitelist": None,
                "only_registered": True, "forgetport": False,
                "timeout_hint": None}
    @property
    def param_nodename(self):
        return self.name

    # The whitelist parameter here is an iterable of strings in CIDR notation.
    # The whitelist parameter we get from params (i.e., from the parameter file)
    # is a string with comma-separated CIDR strings. The two are concatenated
    # to form the server whitelist.
    def __init__(self, *, default_workdir, parameters, path_prefix, db, whitelist=None):
        super().__init__(db=db, parameters=parameters, path_prefix=path_prefix)
        # self.logger.info("path_prefix = %s, parameters = %s", path_prefix, parameters)
        self.params = self.parameters.myparams(self.paramnames)
        serveraddress = self.params.get("address", None)
        serverport = self.params["port"]
        basedir = self.params.get("workdir", default_workdir).rstrip(os.sep) + os.sep
        uploaddir = basedir + self.params["name"] + ".upload/"
        threaded = self.params["threaded"]
        # By default, allow access only to files explicitly registered by tasks,
        # i.e., those files required by clients when downloading input files for
        # their workunits. By setting only_registered=False, access to all files
        # under the server working directory is allowed.
        only_registered = self.params["only_registered"]
        if self.params["ssl"]:
            cafilename = basedir + self.params["name"] + ".server.cert"
        else:
            cafilename = None

        servertimeout_hint = self.params.get("timeout_hint")

        server_whitelist = []
        if not whitelist is None:
            server_whitelist += whitelist
        if "whitelist" in self.params:
            server_whitelist += [h.strip() for h in self.params["whitelist"].split(",")]

        # If we should auto-assign an available port, try to use the same one
        # as last time, if we had run before. This can be overridden with
        # server.forgetport=yes
        if self.params["forgetport"] and "port" in self.state:
            del(self.state["port"])

        if serverport == 0 and "port" in self.state:
            serverport = self.state["port"]

        # If (1) any clients are to be started on localhost, but (2) the server
        # is listening on a network-visible address, then we need to whitelist
        # the network-visible address(es) of the current host as well, because
        # the client's connection will come from (one of) the network-visible
        # addresses of the current host.
        # For test (1), it should suffice to look for "localhost" or
        # "127.0.0.1" in the existing whitelist, as the host names on which to
        # start clients are inserted verbatim.
        # For (2), we check that serveraddress is either None (i.e., the
        # wildcard address which is network-visible), or anything other than
        # "localhost"
        if (serveraddress is None or socket.getfqdn(serveraddress) != "localhost") \
                and set(server_whitelist) & {"localhost", "127.0.0.1"}:
            hostname = socket.gethostname()
            if not hostname in server_whitelist:
                try:
                    foo=socket.gethostbyname(hostname)
                    self.logger.info("Adding %s to whitelist to allow clients on localhost to connect", hostname)
                    server_whitelist.append(hostname)
                except socket.gaierror as e:
                    self.logger.info("Not adding %s to whitelist (cannot be resolved), clients will only be allowed to connect on 127.0.0.1", hostname)
        if not server_whitelist:
            server_whitelist = None

        self.registered_filenames = self.make_db_dict('server_registered_filenames')
        self.server = wuserver.ServerLauncher(serveraddress, serverport,
            threaded, db, self.registered_filenames,
            uploaddir, bg=True, only_registered=only_registered, cafile=cafilename,
            whitelist=server_whitelist,
            timeout_hint=servertimeout_hint)
        self.state["port"] = self.server.get_port()

    def run(self):
        self.server.serve()

    def shutdown(self):
        self.server.shutdown()        

    def stop_serving_wus(self):
        self.server.stop_serving_wus()

    def get_url(self, **kwargs):
        return self.server.get_url(**kwargs)

    def get_cert_sha1(self):
        return self.server.get_cert_sha1()

    def register_filename(self, d):
        for key in d:
            if not key in self.registered_filenames:
                self.logger.debug("Registering file name %s with target %s",
                                  key, d[key])
                self.registered_filenames[key] = d[key]
            elif d[key] != self.registered_filenames[key]:
                # It was already registered with a different target. This will
                # happen if, e.g., the user chooses a different build directory
                # between runs. It's still fragile, as the server will try to
                # serve the old file(s) until a new workunit is generated and
                # overrides the target.
                # The proper solution would be to make Program classes
                # Templates, so Tasks can instantiate them at __init__() and
                # register the resolved paths once. The registered_filenames
                # dict could then be memory-backed. Tasks would also have to
                # register their own files which may need to be served in
                # __init__().
                self.logger.warning("Filename %s, to be registered for "
                                    "target %s, was previously registered for "
                                    "target %s. Overriding with new target.",
                                    key, d[key], self.registered_filenames[key])
                self.registered_filenames[key] = d[key]
            else:
                # Was already registered with the same target. Nothing to do
                pass

class StartClientsTask(Task):
    """ Starts clients on slave machines """
    @property
    def name(self):
        return "slaves"
    @property
    def title(self):
        return "Client Launcher"
    @property
    def programs(self):
        return ((cadoprograms.CadoNFSClient, ("clientid", "certsha1"), {}),)
    @property
    def paramnames(self):
        return {'hostnames': str, 'scriptpath': None, "nrclients": [int], "run": True}
    @property
    def param_nodename(self):
        return None
    
    def __init__(self, *, mediator, db, parameters, path_prefix):
        super().__init__(mediator=mediator, db=db, parameters=parameters,
                         path_prefix=path_prefix)
        self.used_ids = {}
        self.pids = self.make_db_dict(self.make_tablename("client_pids"), connection=self.db_connection)
        self.hosts = self.make_db_dict(self.make_tablename("client_hosts"), connection=self.db_connection)
        assert set(self.pids) == set(self.hosts)
        # Invariants: the keys of self.pids and of self.hosts are the same set.
        # The keys of self.used_ids are a subset of the keys of self.pids.
        # A clientid is in self.used_ids if we know that clientid to be
        # currently running.
        
        if 'scriptpath' in self.params:
            self.progparams[0]['execpath'] = self.params['scriptpath']
        
        # If hostnames are of the form @file, read host names from file,
        # one host name per line
        match = re.match(r"@(.*)", self.params["hostnames"])
        if match:
            with open(match.group(1)) as f:
                self.hosts_to_launch = [line.strip() for line in f]
        else:
            self.hosts_to_launch = [host.strip() for host in
                    self.params["hostnames"].split(",")]

        if "nrclients" in self.params:
            self.hosts_to_launch = self.make_multiplicity(self.hosts_to_launch,
                   self.params["nrclients"])

    @staticmethod
    def make_multiplicity(names, multi):
        """ Produce a list in which each unique entry of the list "names"
        occurs "multi" times. The order of elements in names is preserved.
        
        >>> names = ['a', 'b', 'a', 'c', 'c', 'a', 'a']
        >>> StartClientsTask.make_multiplicity(names, 1)
        ['a', 'b', 'c']
        >>> StartClientsTask.make_multiplicity(names, 2)
        ['a', 'a', 'b', 'b', 'c', 'c']
        """
        result = []
        # Use OrderedDict to get unique names, preserving order
        for name in OrderedDict.fromkeys(names, None):
            result.extend([name] * multi)
        return result

    def get_hosts_to_launch(self):
        """ Get list host names on which clients should run """
        return self.hosts_to_launch

    def is_alive(self, clientid):
        # Simplistic: just test if process with that pid exists and accepts
        # signals from us. TODO: better testing here, probably with ps|grep
        # or some such
        (rc, stdout, stderr) = self.kill_client(clientid, signal=0)
        return (rc == 0)
    
    def _add_cid(self, clientid, pid, host):
        """ Add a client id atomically to both the "pids" and "hosts"
        dictionaries
        """
        self.pids.update({clientid: pid}, commit=False)
        self.hosts.update({clientid: host}, commit=True)

    def _del_cid(self, clientid):
        """ Remove a client id atomically from both the "pids" and "hosts"
        dictionaries
        """
        self.pids.clear([clientid], commit=False)
        self.hosts.clear([clientid], commit=True)
    
    def launch_clients(self, servertask):
        """ This now takes server as a servertask object, so that we can
        get an URL which is special-cased for localhost """
        url = servertask.get_url()
        url_loc = servertask.get_url(origin="localhost")
        certsha1 = servertask.get_cert_sha1()
        for host in self.hosts_to_launch:
            if host == "localhost":
                self.launch_one_client(host.strip(), url_loc, certsha1=certsha1)
            else:
                self.launch_one_client(host.strip(), url, certsha1=certsha1)
        running_clients = [(cid, self.hosts[cid], pid) for (cid, pid) in
            self.pids.items()]
        s = ", ".join(["%s (Host %s, PID %d)" % t for t in running_clients])
        self.logger.info("Running clients: %s" % s)
        # Check for old clients which we did not mean to start this run
        for cid in set(self.pids) - set(self.used_ids):
            if self.is_alive(cid):
                self.logger.warn("Client id %s (Host %s, PID %d), launched "
                                 "in a previous run and not meant to be "
                                 "launched this time, is still running",
                                 cid, self.hosts[cid], self.pids[cid])
            else:
                self.logger.warn("Client id %s (Host %s, PID %d), launched "
                                 "in a previous run and not meant to be "
                                 "launched this time, seems to have died. "
                                 "I'll forget about this client.",
                                 cid, self.hosts[cid], self.pids[cid])
                self._del_cid(cid)
    
    def make_unique_id(self, host):
        # Make a unique client id for host
        clientid = host
        i = 1
        while clientid in self.used_ids:
            assert clientid in self.pids
            assert clientid in self.hosts
            i += 1
            clientid = "%s+%d" % (host, i)
        return clientid
    
    # Cases:
    # Client was never started. Start it, add to state
    # Client was started, but does not exist any more. Remove from state,
    #   then start and add again
    # Client was started, and does still exists. Nothing to do.
    
    def launch_one_client(self, host, server, *, clientid=None, certsha1=None):
        if clientid is None:
            clientid = self.make_unique_id(host)
        # Check if client is already running
        if clientid in self.pids:
            assert self.hosts[clientid] == host
            if self.is_alive(clientid):
                self.logger.info("Client %s on host %s with PID %d already "
                                 "running",
                                 clientid, host, self.pids[clientid])
                self.used_ids[clientid] = True
                return
            else:
                self.logger.info("Client %s on host %s with PID %d seems to have died",
                                 clientid, host, self.pids[clientid])
                self._del_cid(clientid)
        
        self.logger.info("Starting client id %s on host %s", clientid, host)
        cado_nfs_client = cadoprograms.CadoNFSClient(server=server,
                                         clientid=clientid, daemon=True,
                                         certsha1=certsha1,
                                         **self.progparams[0])
        if host == "localhost":
            process = cadocommand.Command(cado_nfs_client)
        else:
            process = cadocommand.RemoteCommand(cado_nfs_client, host, self.parameters)
        (rc, stdout, stderr) = process.wait()
        if rc != 0:
            self.logger.warning("Starting client on host %s failed.", host)
            if stdout:
                self.logger.warning("Stdout: %s", stdout.decode("utf-8").strip())
            if stderr:
                self.logger.warning("Stderr: %s", stderr.decode("utf-8").strip())
            return
        match = None
        if not stdout is None:
            match = re.match(r"PID: (\d+)", stdout.decode("utf-8"))
        if not match:
            self.logger.warning("Client did not print PID")
            if not stdout is None:
                self.logger.warning("Stdout: %s", stdout.decode("utf-8").strip())
            if not stderr is None:
                self.logger.warning("Stderr: %s", stderr.decode("utf-8").strip())
            return
        self.used_ids[clientid] = True
        self._add_cid(clientid, int(match.group(1)), host)

    def kill_all_clients(self):
        # Need the list() to make a copy as dict will change in loop body
        for clientid in list(self.pids):
            (rc, stdout, stderr) = self.kill_client(clientid)
            if rc == 0:
                self.logger.info("Stopped client %s (Host %s, PID %d)",
                                 clientid, self.hosts[clientid], self.pids[clientid])
                self._del_cid(clientid)
            else:
                self.logger.warning("Stopping client %s (Host %s, PID %d) failed",
                                    clientid, self.hosts[clientid], self.pids[clientid])
                if stdout:
                    self.logger.warning("Stdout: %s", stdout.decode("utf-8").strip())
                if stderr:
                    self.logger.warning("Stderr: %s", stderr.decode("utf-8").strip())
                # Assume that the client is already dead and remove it from
                # the list of running clients
                self._del_cid(clientid)
    
    def kill_client(self, clientid, signal=None):
        pid = self.pids[clientid]
        host = self.hosts[clientid]
        kill = cadoprograms.Kill(pid, signal=signal)
        if host == "localhost":
            process = cadocommand.Command(kill)
        else:
            process = cadocommand.RemoteCommand(kill, host, self.parameters)
        return process.wait()

class Message(object):
    def __init__(self, sender, key, value=None):
        self.sender = sender
        self.key = key
        self.value = value
    def get_sender(self):
        return self.sender
    def get_key(self):
        return self.key
    def get_value(self):
        return self.value
    @classmethod
    def reverse_lookup(cls, reference):
        for key in dir(cls):
            if getattr(cls, key) == reference:
                return key


class Notification(Message):
    FINISHED_POLYNOMIAL_SELECTION = object()
    WANT_MORE_RELATIONS = object()
    HAVE_ENOUGH_RELATIONS = object()
    REGISTER_FILENAME = object()
    UNREGISTER_FILENAME = object()
    WANT_TO_RUN = object()
    SUBSCRIBE_WU_NOTIFICATIONS = object()

class Request(Message):
    # Lacking a proper enum before Python 3.4, we generate dummy objects
    # which have separate identity and can be used as dict keys
    GET_RAW_POLYNOMIALS = object()
    GET_POLYNOMIAL = object()
    GET_POLYNOMIAL_FILENAME = object()
    GET_HAVE_TWO_ALG_SIDES = object()
    GET_WILL_IMPORT_FINAL_POLYNOMIAL = object()
    GET_POLY_RANK = object()
    GET_FACTORBASE_FILENAME = object()
    GET_FACTORBASE0_FILENAME = object()
    GET_FACTORBASE1_FILENAME = object()
    GET_FREEREL_FILENAME = object()
    GET_RENUMBER_FILENAME = object()
    GET_FREEREL_RELCOUNT = object()
    GET_RENUMBER_PRIMECOUNT = object()
    GET_SIEVER_FILENAMES = object()
    GET_SIEVER_RELCOUNT = object()
    GET_DUP1_FILENAMES = object()
    GET_DUP1_RELCOUNT = object()
    GET_GAL_UNIQUE_RELCOUNT = object()
    GET_UNIQUE_RELCOUNT = object()
    GET_UNIQUE_FILENAMES = object()
    GET_PURGED_FILENAME = object()
    GET_MERGED_FILENAME = object()
    GET_INDEX_FILENAME = object()
    GET_IDEAL_FILENAME = object()
    GET_DENSE_FILENAME = object()
    GET_DEPENDENCY_FILENAME = object()
    GET_LINALG_PREFIX = object()
    GET_KERNEL_FILENAME = object()
    GET_VIRTUAL_LOGS_FILENAME = object()
    GET_RELSDEL_FILENAME = object()
    GET_SM_FILENAME = object()
    GET_UNITS_DIRNAME = object()
    GET_BADIDEALS_FILENAME = object()
    GET_BADIDEALINFO_FILENAME = object()
    GET_SMEXP = object()
    GET_NMAPS = object()
    GET_WU_RESULT = object()
    GET_WORKDIR_JOBNAME = object()
    GET_WORKDIR_PATH = object()
    GET_DLOG_FILENAME = object()

class CompleteFactorization(HasState, wudb.DbAccess, 
        DoesLogging, cadoparams.UseParameters, patterns.Mediator):
    """ The complete factorization, aggregate of the individual tasks """
    @property
    def name(self):
        return "tasks"
    @property
    def param_nodename(self):
        return self.name
    @property
    def paramnames(self):
        # This isn't a Task subclass so we don't really need to define
        # paramnames, but we do it out of habit
        return {"name": str, "workdir": str, "N": int, "ell": 0, "dlp": False,
                "gfpext": 1, "trybadwu": False, "target": 0}
    @property
    def title(self):
        return "Complete Factorization"
    @property
    def programs(self):
        return []
    
    def __init__(self, db, parameters, path_prefix):
        self.db=db
        super().__init__(db=db, parameters=parameters, path_prefix=path_prefix)
        self.params = self.parameters.myparams(self.paramnames)
        self.db_listener = self.make_db_listener()

        # Init WU BD
        self.wuar = self.make_wu_access()
        self.wuar.create_tables()
        if self.params["trybadwu"]:
            # Test behaviour when a WU is in the DB that does not belong to
            # any task. It should get cancelled with an error message.
            self.wuar.create(["WORKUNIT FAKE_WU_%s\nCOMMAND true" % time.time()])

        # Start with an empty list of tasks that want to run. Tasks will add
        # themselves during __init__().
        self.tasks_that_want_to_run = list()

        # Init client lists
        self.clients = []
        whitelist = set()
        for (path, key) in self.parameters.get_parameters().find(['slaves'], 'hostnames'):
            self.clients.append(StartClientsTask(mediator=self,
                                                 db=db,
                                                 parameters=self.parameters,
                                                 path_prefix=path))
            hostnames = self.clients[-1].get_hosts_to_launch()
            whitelist |= set(hostnames)

        whitelist = list(whitelist) if whitelist else None
        # Init server task
        self.servertask = StartServerTask(default_workdir=self.params["workdir"],
                parameters=parameters, path_prefix=path_prefix, db=db,
                whitelist=whitelist)

        parampath = self.parameters.get_param_path()
        polyselpath = parampath + ['polyselect']
        sievepath = parampath + ['sieve']
        filterpath = parampath + ['filter']
        linalgpath = parampath + ['linalg']
        reconstructlogpath = parampath + ['reconstructlog']
        descentpath = parampath + ['descent']
        sqrtpath = parampath + ['sqrt']
        numbertheorypath = parampath + ['numbertheory']
        
        ## tasks that are common to factorization and dlp
        self.fb = FactorBaseTask(mediator=self,
                                 db=db,
                                 parameters=self.parameters,
                                 path_prefix=sievepath)
        self.freerel = FreeRelTask(mediator=self,
                                   db=db,
                                   parameters=self.parameters,
                                   path_prefix=sievepath)
        self.sieving = SievingTask(mediator=self,
                                   db=db,
                                   parameters=self.parameters,
                                   path_prefix=sievepath)
        self.dup1 = Duplicates1Task(mediator=self,
                                    db=db,
                                    parameters=self.parameters,
                                    path_prefix=filterpath)
        self.dup2 = Duplicates2Task(mediator=self,
                                    db=db,
                                    parameters=self.parameters,
                                    path_prefix=filterpath)
        self.purge = PurgeTask(mediator=self,
                               db=db,
                               parameters=self.parameters,
                               path_prefix=filterpath)
        ## For DLP in extension fields, we can not use the classical
        ## polynomial selection, but otherwise we do:
        if self.params["gfpext"] == 1:
            self.polysel1 = Polysel1Task(mediator=self,
                                       db=db,
                                       parameters=self.parameters,
                                       path_prefix=polyselpath)
            self.polysel2 = Polysel2Task(mediator=self,
                                       db=db,
                                       parameters=self.parameters,
                                       path_prefix=polyselpath)
        else:
            self.polyselgfpn = PolyselGFpnTask(mediator=self,
                    db=db,
                    parameters=self.parameters,
                    path_prefix=polyselpath)

        if self.params["dlp"]:
            ## Tasks specific to dlp
            self.numbertheory = NumberTheoryTask(mediator=self,
                             db=db,
                             parameters=self.parameters,
                             path_prefix=numbertheorypath)
            self.filtergalois = FilterGaloisTask(mediator=self,
                             db=db,
                             parameters=self.parameters,
                             path_prefix=filterpath)
            self.sm = SMTask(mediator=self,
                             db=db,
                             parameters=self.parameters,
                             path_prefix=filterpath)
            self.merge = MergeDLPTask(mediator=self,
                                   db=db,
                                   parameters=self.parameters,
                                   path_prefix=filterpath)
            self.linalg = LinAlgDLPTask(mediator=self,
                                     db=db,
                                     parameters=self.parameters,
                                     path_prefix=linalgpath)
            self.reconstructlog = ReconstructLogTask(mediator=self,
                                     db=db,
                                     parameters=self.parameters,
                                     path_prefix=reconstructlogpath)
            if self.params["target"]:
                self.descent = DescentTask(mediator=self,
                                         db=db,
                                         parameters=self.parameters,
                                         path_prefix=descentpath)
        else:
            ## Tasks specific to factorization
            self.merge = MergeTask(mediator=self,
                                   db=db,
                                   parameters=self.parameters,
                                   path_prefix=filterpath)
            self.linalg = LinAlgTask(mediator=self,
                                     db=db,
                                     parameters=self.parameters,
                                     path_prefix=linalgpath)
            self.characters = CharactersTask(mediator=self,
                                             db=db,
                                             parameters=self.parameters,
                                             path_prefix=linalgpath)
            self.sqrt = SqrtTask(mediator=self,
                                 db=db,
                                 parameters=self.parameters,
                                 path_prefix=sqrtpath)
        
        # Defines an order on tasks in which tasks that want to run should be
        # run
        if self.params["dlp"]:
            if self.params["gfpext"] == 1:
                self.tasks = (self.polysel1, self.polysel2)
            else:
                self.tasks = (self.polyselgfpn,)
            self.tasks = self.tasks + (self.numbertheory, self.fb,
                          self.freerel, self.sieving,
                          self.dup1, self.dup2,
                          self.filtergalois, self.purge, self.merge,
                          self.sm, self.linalg, self.reconstructlog)
            if self.params["target"]:
                self.tasks = self.tasks + (self.descent,)
        else:
            self.tasks = (self.polysel1, self.polysel2, self.fb, self.freerel,
                          self.sieving, self.dup1, self.dup2, self.purge,
                          self.merge, self.linalg, self.characters, self.sqrt)

        reverse_lookup=defaultdict(list)
        self.parameter_help=""
        for t in self.tasks:
            self.parameter_help += t.collect_usable_parameters(reverse_lookup)

        for (path, key, value) in parameters.get_unused_parameters():
            self.logger.warning("Parameter %s = %s was not used anywhere",
                                ".".join(path + [key]), value)
            if key in reverse_lookup.keys():
                l = reverse_lookup[key]
                if len(l) == 1:
                    self.logger.warning("Perhaps you meant %s.%s ?" % (l[0], key))
                else:
                    self.logger.warning("Perhaps you meant one of the following ?")
                    for x in l:
                        self.logger.warning("  %s.%s ?" % (x, key))
                    prefix = ".".join(os.path.commonprefix([x.split(".") for x in l]))
                    self.logger.warning("(If you wish to set all of these consistently, you may set %s.%s)" % (prefix, key))


        self.request_map = {
            Request.GET_FACTORBASE_FILENAME: self.fb.get_filename,
            Request.GET_FACTORBASE0_FILENAME: self.fb.get_filename0,
            Request.GET_FACTORBASE1_FILENAME: self.fb.get_filename1,
            Request.GET_FREEREL_FILENAME: self.freerel.get_freerel_filename,
            Request.GET_RENUMBER_FILENAME: self.freerel.get_renumber_filename,
            Request.GET_FREEREL_RELCOUNT: self.freerel.get_nrels,
            Request.GET_RENUMBER_PRIMECOUNT: self.freerel.get_nprimes,
            Request.GET_SIEVER_FILENAMES: self.sieving.get_output_filenames,
            Request.GET_SIEVER_RELCOUNT: self.sieving.get_nrels,
            Request.GET_DUP1_FILENAMES: self.dup1.get_output_filenames,
            Request.GET_DUP1_RELCOUNT: self.dup1.get_nrels,
            Request.GET_UNIQUE_RELCOUNT: self.dup2.get_nrels,
            Request.GET_UNIQUE_FILENAMES: self.dup2.get_output_filenames,
            Request.GET_PURGED_FILENAME: self.purge.get_purged_filename,
            Request.GET_MERGED_FILENAME: self.merge.get_merged_filename,
            Request.GET_INDEX_FILENAME: self.merge.get_index_filename,
            Request.GET_DENSE_FILENAME: self.merge.get_dense_filename,
            Request.GET_WU_RESULT: self.db_listener.send_result,
            Request.GET_WORKDIR_JOBNAME: self.fb.workdir.get_workdir_jobname,
            Request.GET_WORKDIR_PATH: self.fb.workdir.get_workdir_path,
        }

        ## Set requests related to polynomial selection
        if self.params["gfpext"] == 1:
            self.request_map[Request.GET_RAW_POLYNOMIALS] = self.polysel1.get_raw_polynomials
            self.request_map[Request.GET_POLY_RANK] = self.polysel1.get_poly_rank
            self.request_map[Request.GET_POLYNOMIAL] = self.polysel2.get_poly
            self.request_map[Request.GET_POLYNOMIAL_FILENAME] = self.polysel2.get_poly_filename
            self.request_map[Request.GET_HAVE_TWO_ALG_SIDES] = self.polysel2.get_have_two_alg_sides
            self.request_map[Request.GET_WILL_IMPORT_FINAL_POLYNOMIAL] = self.polysel2.get_will_import
        else:
            self.request_map[Request.GET_POLYNOMIAL] = self.polyselgfpn.get_poly
            self.request_map[Request.GET_POLYNOMIAL_FILENAME] = self.polyselgfpn.get_poly_filename
            self.request_map[Request.GET_HAVE_TWO_ALG_SIDES] = self.polyselgfpn.get_have_two_alg_sides

        ## add requests specific to dlp or factoring
        if self.params["dlp"]:
            self.request_map[Request.GET_IDEAL_FILENAME] = self.merge.get_ideal_filename
            self.request_map[Request.GET_GAL_UNIQUE_RELCOUNT] = self.filtergalois.get_nrels
            self.request_map[Request.GET_BADIDEALS_FILENAME] = self.numbertheory.get_badideals_filename
            self.request_map[Request.GET_BADIDEALINFO_FILENAME] = self.numbertheory.get_badidealinfo_filename
            self.request_map[Request.GET_NMAPS] = self.numbertheory.get_nmaps
            self.request_map[Request.GET_SM_FILENAME] = self.sm.get_sm_filename
            self.request_map[Request.GET_RELSDEL_FILENAME] = self.purge.get_relsdel_filename
            self.request_map[Request.GET_DLOG_FILENAME] = self.reconstructlog.get_dlog_filename
            self.request_map[Request.GET_KERNEL_FILENAME] = self.linalg.get_virtual_logs_filename
            self.request_map[Request.GET_VIRTUAL_LOGS_FILENAME] = self.linalg.get_virtual_logs_filename
        else:
            self.request_map[Request.GET_KERNEL_FILENAME] = self.characters.get_kernel_filename
            self.request_map[Request.GET_DEPENDENCY_FILENAME] = self.linalg.get_dependency_filename
            self.request_map[Request.GET_LINALG_PREFIX] = self.linalg.get_prefix

    def run(self):
        had_interrupt = False
        if self.params["dlp"]:
            self.logger.info("Computing Discrete Logs in GF(%s)", self.params["N"])
        else:
            self.logger.info("Factoring %s", self.params["N"])
        self.start_elapsed_time()

        self.servertask.run()
        last_task = None
        last_status = True
        try:
            tasks=[]
            for i in range(len(self.tasks)):
                tasks.append(self.tasks[i])
            self.start_all_clients()
            i=0
            while last_status:
                last_status, last_task = self.run_next_task()
                if i<len(self.tasks):
                    self.tasks[i].print_stats()
                    i+=1
            for task in self.tasks:
                task.print_stats()

        except KeyboardInterrupt:
            self.logger.fatal("Received KeyboardInterrupt. Terminating")
            had_interrupt = True

        self.stop_all_clients()
        self.servertask.shutdown()
        elapsed = self.end_elapsed_time()

        if had_interrupt:
            return None

        cputotal = self.get_sum_of_cpu_or_real_time(True)
        # Do we want the sum of real times over all sub-processes for
        # something?
        # realtotal = self.get_sum_of_cpu_or_real_time(False)
        if self.params["dlp"]:
            self.logger.info("Total cpu/elapsed time for entire discrete log: %g/%g",
                         cputotal, elapsed)
        else:
            self.logger.info("Total cpu/elapsed time for entire factorization: %g/%g",
                         cputotal, elapsed)

        if last_task and not last_status:
            self.logger.fatal("Premature exit within %s. Bye.", last_task)
            return None

        if self.params["dlp"]:
            ret = [ self.params["N"], self.params["ell"]] + self.reconstructlog.get_log2log3()
            if self.params["target"]:
                ret = ret + [self.descent.get_logtarget()]
            return ret
        else:
            return self.sqrt.get_factors()
    
    def start_all_clients(self):
        for clients in self.clients:
            clients.launch_clients(self.servertask)
    
    def stop_all_clients(self):
        for clients in self.clients:
            clients.kill_all_clients()

    def start_elapsed_time(self):
        if "starttime" in self.state:
            self.logger.warning("The start time of the last cado-nfs.py "
                                "run was recorded, but not its end time, "
                                "maybe because it died unexpectedly.")
            self.logger.warning("Elapsed time of last run is not known and "
                                "will not be counted towards total.")
        self.state["starttime"] = time.time()
        
    def end_elapsed_time(self):
        if not "starttime" in self.state:
            self.logger.error("Missing starttime in end_elapsed_time(). "
                              "This should not have happened.")
            return
        elapsed = time.time() - self.state["starttime"]
        elapsed += self.state.get("elapsed", 0)
        self.state.__delitem__("starttime", commit=False)
        self.state.update({"elapsed": elapsed}, commit=True)
        return elapsed

    
    def run_next_task(self):
        for task in self.tasks:
            if task in self.tasks_that_want_to_run:
                #self.logger.info("Next task that wants to run: %s", task.title)
                self.tasks_that_want_to_run.remove(task)
                return [task.run(), task.title]
        return [False, None]

    def get_sum_of_cpu_or_real_time(self, is_cpu):
        total = 0
        for task in self.tasks:
            task_time = task.get_total_cpu_or_real_time(is_cpu)
            total += task_time
            # self.logger.info("Task %s reports %s time of %g, new total: %g",
            #         task.name, "cpu" if is_cpu else "real", task_time, total)
        return total

    def register_filename(self, d):
        return self.servertask.register_filename(d)
    
    def relay_notification(self, message):
        """ The relay for letting Tasks talk to us and each other """
        assert isinstance(message, Notification)
        sender = message.get_sender()
        key = message.get_key()
        value = message.get_value()
        self.logger.message("Received notification from %s, key = %s, value = %s",
                            sender, Notification.reverse_lookup(key), value)
        if key is Notification.WANT_MORE_RELATIONS:
            if sender is self.purge:
                self.dup2.request_more_relations(value)
                if self.params["dlp"]:
                    self.filtergalois.request_more_relations(value)
            elif sender is self.dup2:
                self.dup1.request_more_relations(value)
            elif sender is self.dup1:
                self.sieving.request_more_relations(value)
            else:
                raise Exception("Got WANT_MORE_RELATIONS from unknown sender")
        elif key is Notification.HAVE_ENOUGH_RELATIONS:
            if sender is self.purge:
                self.servertask.stop_serving_wus()
                self.sieving.cancel_available_wus()
                self.stop_all_clients()
            else:
                raise Exception("Got HAVE_ENOUGH_RELATIONS from unknown sender")
        elif key is Notification.REGISTER_FILENAME:
            if isinstance(sender, ClientServerTask):
                self.register_filename(value)
            else:
                raise Exception("Got REGISTER_FILENAME, but not from a ClientServerTask")
        elif key is Notification.WANT_TO_RUN:
            if sender in self.tasks_that_want_to_run:
                raise Exception("Got request from %s to run, but it was in run queue already",
                                sender)
            else:
                self.tasks_that_want_to_run.append(sender)
        elif key is Notification.SUBSCRIBE_WU_NOTIFICATIONS:
            return self.db_listener.subscribeObserver(sender)
        else:
            raise KeyError("Notification from %s has unknown key %s" % (sender, key))
    
    def answer_request(self, request):
        assert isinstance(request, Request)
        sender = request.get_sender()
        key = request.get_key()
        value = request.get_value()
        self.logger.message("Received request from %s, key = %s, values = %s",
                            sender, Request.reverse_lookup(key), value)
        if not key in self.request_map:
            raise KeyError("Unknown Request key %s from sender %s" %
                           (key, sender))
        if value is None:
            result = self.request_map[key]()
        else:
            result = self.request_map[key](value)
        self.logger.message("Completed request from %s, key = %s, values = %s, result = %s",
                            sender, Request.reverse_lookup(key), value, result)
        return result
    
    def handle_message(self, message):
        if isinstance(message, Notification):
            self.relay_notification(Notification)
        elif isinstance(message, Request):
            return self.answer_request(message)
        else:
            raise TypeError("Message is neither Notification nor Request")
