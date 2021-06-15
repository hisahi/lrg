# lrg

A program that prints specific lines by number from files, with support for
line ranges.

# Copyright & License

lrg (C) Sampo Hippeläinen (hisahi) 2017-2021

This program is licensed under the GNU General Public License (GPL),
version 3.0. See `COPYING` for the license.

# Usage

```
Usage: lrg [OPTION]... range[,range]... [input-file]...
Prints a specific range of lines from the given file.
Note that 'rewinding' might be impossible - once a line has been printed,
it is possible that only lines after it can be printed.
Line numbers start at 1.

  -?, --help
                 prints this message
  --version
                 prints version information
  -e, --error-on-eof
                 treat premature EOF as an error
  -f, --file-names
                 print file names before each file
  -l, --line-numbers
                 print line numbers before each line
  -w, --warn-eof
                 print a warning when a line is not found

Line range formats:
   N
                 the line with line number N
   N-[M]
                 lines between lines N and M (inclusive)
                 if M not specified, goes until end of file
   N~[M]
                 the lines around line number N
                 equivalent to (N-M)-(N+M), therefore
                 displaying 2*M+1 lines
                 if M not specified, defaults to 3
```

Extra (POSIX-exclusive) feature:

```
  --lps, --lines-per-second <x>
                 prints lines at an (approximate) top speed
                 (minimum 0.001, maximum 1000000)
```

# Building

lrg is distributed as a single standalone .c file that can be compiled
with the vast majority of ANSI C (C89) standard-compliant C compilers, basically
any compiler with a reasonable length limit (at least ~20 significant initial
characters) for (external) identifiers.

While the base program is fully functional on ANSI C, on modern versions
of POSIX, an enhanced and optimized version can be compiled instead (and
will be compiled by default if one is detected).

Likewise, if C99 support is detected, the program will be compiled with some
C99 enchantments (such as `unsigned long long` for line numbers).

The following preprocessor definitions can be used to change the compiled
program. All defines are either 0 or 1, unless otherwise mentioned:

* `LRG_BACKWARD_SCAN` - normally, when a line number is requested that does not
  come after every line displayed thus far, the file will be rewound back to
  the beginning and the reading restarts to find the line again. If
  `LRG_BACKWARD_SCAN` is enabled, lrg will instead scan backwards from the
  current position which can be much faster if the lines are closer together.
  `LRG_BACKWARD_SCAN` requires that the system does not make a distinction
  between binary and text files in order to seek to arbitrary positions within
  the file. `LRG_BACKWARD_SCAN` is by default enabled on POSIX systems only.
* `LRG_BACKWARD_SCAN_THRESHOLD` - backwards scan will only be used if the target
  line number is greater than this threshold (this is a necessary but not a
  sufficient condition).
* `LRG_HOSTED_MEMCNT` - lrg makes use of a function called `memcnt` which
  counts the number of bytes in a buffer that equal some value. This function
  is not in the C standard, but not much sets it apart from those that are. The
  code includes a naive reference implementation, which tends to optimize pretty
  well with modern optimizing compilers. However, if you want to link an
  external implementation of `memcnt`, such as from
  [hisahi/memcnt](https://github.com/hisahi/memcnt), set this to 1. The linked
  implementation must have a certain signature and meet certain specifications;
  see the setting for `LRG_HOSTED_MEMCNT` under lrg.c for more.
* `LRG_FAST_MEMCNT` - if enabled, `memcnt` will be used to quickly scan
  over buffers which cannot contain the requested line. This can considerably
  speed up reading large files, but requires that the `memcnt` implementation is
  quick enough to outperform the standard reading procedure. This is only ever
  practically the case when the highest optimization level is used and the
  `memcnt` function gets autovectorized, or if you link an external
  implementation that is fast enough (this is indeed enabled by default if
  `LRG_HOSTED_MEMCNT` is set to 1).
* `LRG_FILLBUF_MODE` - 2 by default, which is the recommended value. If 2, lrg
  will use separate functions for reading from files and pipes (distinguished
  by whether a stream is seekable). 0 forces the use of file reading functions
  and 1 the use of pipe reading functions. On modern platforms with branch
  prediction, there should not really be any reason for this to not be 2, unless
  the file and pipe reading functions are identical.

# Performance

Not only is lrg more intuitive to use than many existing tricks for getting line
number N in Linux, it is also plenty fast too! See `BENCHMARK.md`.

# Contributing

Issues and pull requests are welcome.

# Notes

All files in this repository are encoded in UTF-8.

The `man` folder contains man pages for lrg.
