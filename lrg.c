/*

Line RanGe (LRG)
A tool that allows displaying specific lines of files (or around a
specific line) with possible line number display
Copyright (c) 2017-2021 Sampo Hippeläinen (hisahi)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

/* a pair of integers that is meant to increase with every change
   newer version is with higher MAJOR or equal MAJOR and higher MINOR */
#define LRG_V_MAJOR 1
#define LRG_V_MINOR 1

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !LRG_NO_C99 && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define LRG_C99 1
#include <stdint.h>
#else
#define LRG_C99 0
#endif

#if !LRG_NO_C11 && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define LRG_C11 1
#else
#define LRG_C11 0
#endif

#if defined(__has_include) && !defined(HAVE_UNISTD_H)
#if __has_include(<unistd.h>)
#define HAVE_UNISTD_H 1
#endif
#endif

#if !LRG_NO_POSIX &&                                                           \
    (HAVE_UNISTD_H || defined(__unix__) || defined(__unix) ||                  \
     defined(__QNX__) || (defined(__APPLE__) && defined(__MACH__)))
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#include <unistd.h>
#endif

#if !LRG_NO_POSIX && defined(_POSIX_C_SOURCE) && defined(_POSIX_VERSION) &&    \
    _POSIX_VERSION >= _POSIX_C_SOURCE
#define LRG_POSIX 1
#include <time.h>
#else
#define LRG_POSIX 0
#endif

#if (LRG_C99 && UINTPTR_MAX > UINT32_MAX) || defined(__x86_64__) ||            \
    defined(_M_X64) || defined(__aarch64__)
#define LRG_IS64BIT 1
#else
#define LRG_IS64BIT 0
#endif

/* ========================================================= */
/*  preprocessor definitions (settings for compiled binary)  */
/* ========================================================= */

/* allow scanning backwards. requires that files are opened in binary mode (not
   different from text mode on most *nix systems). this is an optimization and
   not required for lrg to function. */
#ifndef LRG_BACKWARD_SCAN
#define LRG_BACKWARD_SCAN LRG_POSIX
#endif
/* do backwards scan if the target line number is greater than...
   (must always be at least 1) */
#ifndef LRG_BACKWARD_SCAN_THRESHOLD
#define LRG_BACKWARD_SCAN_THRESHOLD 128
#endif

/* set to 1 if a memcnt implementation is linked from elsewhere */
#ifndef LRG_HOSTED_MEMCNT
#define LRG_HOSTED_MEMCNT 0
#endif
/* the linked implementation must have the below signature and return the number
   of bytes (chars) equal to value in the buffer pointed to by ptr, the size of
   which is determined by num. its behavior is undefined if accessing an array
   beyond its bounds or if ptr is a null pointer. if not undefined, the
   implementation shall return the same value as the following function with
   the same parameters:

    size_t memcnt(const void *s, int c, size_t n) {
        size_t i, count = 0;
        const unsigned char *p = (const unsigned char *)s, v = (unsigned char)c;
        for (i = 0; i < n; ++i)
            if (p[i] == v)
                ++count;
        return count;
    } */
size_t memcnt(const void *s, int c, size_t n);

/* use memcnt to skip lines if we still have a long way to go. this is not
   worth it unless memcnt is really fast (such as when it's vectorized and
   uses CPU intrinsics), at least about as fast as the memchr of your libc */
#ifndef LRG_FAST_MEMCNT
#define LRG_FAST_MEMCNT LRG_HOSTED_MEMCNT
#endif

/* 0 = always use fillbuf_file. 1 = always use fillbuf_pipe.
   2 (or anything else) = auto (use _file if seekable, else _pipe).
   consider setting to 0 or 1 if branches are expensive (even if predictable)
   fillbuf_file will always read as much as it can. fillbuf_pipe on the other
   hand should only provide line buffering and thus can do partial reads.
   fillbuf_pipe should never be used when seeking backwards */
#ifndef LRG_FILLBUF_MODE
#define LRG_FILLBUF_MODE 2
#endif

/* buffer size. adjusting this can improve performance considerably and is
   a good place to start if you wish to build a fast lrg for your system */
#ifndef LRG_BUFSIZE
#if LRG_POSIX
#define LRG_BUFSIZE BUFSIZ * 4
#else
#define LRG_BUFSIZE BUFSIZ
#endif
#endif
/* line range buffer size. this is only the static allocation;
   if there are too many ranges to fit, dynamic allocation will be used to
   get an expanded buffer */
#ifndef LRG_LINEBUFSIZE
#define LRG_LINEBUFSIZE 32
#endif

/* align buffer to N bytes. only actually happens if the compiler supports it */
#ifndef LRG_BUFFER_ALIGN
#if LRG_C99 && CHAR_BIT == 8 && LRG_IS64BIT
#define LRG_BUFFER_ALIGN 8
#elif LRG_C99 && CHAR_BIT == 8 && LRG_IS64BIT
#define LRG_BUFFER_ALIGN 4
#else
/* 0 is ignored */
#define LRG_BUFFER_ALIGN 0
#endif
#endif

#if LRG_C99
typedef unsigned long long linenum_t;
#define LINENUM_MAX ULLONG_MAX
#define LINENUM_FMT "llu"
#define STR_TO_LINENUM strtoull
#else
typedef unsigned long linenum_t;
#define LINENUM_MAX ULONG_MAX
#define LINENUM_FMT "lu"
#define STR_TO_LINENUM strtoul
#endif

/* exit codes. successful, I/O error, usage error */
#if LRG_POSIX
#define EXITCODE_OK 0
#define EXITCODE_ERR 1
#define EXITCODE_USE 2
#else
#define EXITCODE_OK EXIT_SUCCESS
#define EXITCODE_ERR EXIT_FAILURE
#define EXITCODE_USE EXIT_FAILURE
#endif

#if LRG_C99
/* inline this func, please */
#define INLINE static inline
#define RESTRICT restrict
#else
#define INLINE static
#define RESTRICT
#endif

/* ALIGNAS: aligns buffer to N-byte boundary, or does nothing if N == 0 */
#if LRG_C11
#define ALIGNAS(N) _Alignas(N)
#elif defined(__GNUC__)
#define ALIGNAS(N) __attribute__((aligned(N)))
#else
#define ALIGNAS(N)
#endif

#ifdef __GNUC__
#define LIKELY(x) __builtin_expect((x), 1)
#define UNLIKELY(x) __builtin_expect((x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

#ifdef _MSC_VER
/* leave me alone, I know what to do */
#undef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

/* memory allocation functions. assumed to have the same signature and return
   value behavior as standard C malloc, realloc, free. these don't need to
   be macros, they just are for now to allow easier customization */
#define lrg_malloc malloc
#define lrg_realloc realloc
#define lrg_free free

/* ========================================================= */
/*                        definitions                        */
/* ========================================================= */

struct lrg_linerange {
    linenum_t first;
    linenum_t last;
    /* original range format given as a parameter. used for error msgs */
    const char *text;
};

/* argv[0] */
static const char *myname;
/* the flags that the user gave */
static int show_linenums = 0, show_files = 0, warn_noline = 0, error_on_eof = 0;
/* any of our files got EOF? */
static int got_eof = 0;

/* ========================================================= */
/*                        support code                       */
/* ========================================================= */

#if LRG_POSIX

#define NS_PER_SEC 1000000000L

static char lps_enable = 0;
static struct timespec posixnsreq;

static void lps_init(float lps) {
    /* set up a timespec to sleep for 1/lps seconds */
    posixnsreq.tv_sec = (long)(1. / lps);
    posixnsreq.tv_nsec = (long)(NS_PER_SEC / lps) % NS_PER_SEC;
    lps_enable = 1;
}

INLINE void lps_sleep(void) { nanosleep(&posixnsreq, NULL); }

/* we support the --lines-per-second flag */
#define LRG_SUPPORT_LPS 1

#endif

#ifndef LRG_SUPPORT_LPS
#define LRG_SUPPORT_LPS 0
#endif

/* ========================================================= */
/*                          strings                          */
/* ========================================================= */
/* in a centralized location to help with localization */

/* the string that means "stdin" when given as a file name through the
   command line. - is the convention for *nix systems */
static const char *STDIN_FILE = "-";

/* language name in English, all lowercase, replace spaces with hyphens */
#define LANGUAGE_NAME "english"
/* how stdin is shown in error messages, etc. */
#define STDIN_FILENAME_APPEARANCE "(stdin)"
/* for -f/--file-names */
#define FILE_DISPLAY_FMT "%s\n"
/* for -l/--line-numbers */
#define LINE_DISPLAY_FMT " %7" LINENUM_FMT "   "

static void lrg_printversion(void) {
    fprintf(stdout, "lrg by Sampo Hippeläinen (hisahi), compiled " __DATE__ ", "
                    "numbered v%d.%d\n", LRG_V_MAJOR, LRG_V_MINOR);
    fprintf(stdout, "Variant: "
#if LRG_POSIX
                    "lrg_posix"
#else
                    "lrg_ansi"
#endif
                    " memcnt_"
#if LRG_HOSTED_MEMCNT
                    "external"
#else
                    "internal"
#endif
#if LRG_FAST_MEMCNT
                    "_fast"
#endif
                    " language_" LANGUAGE_NAME "\n");
    fprintf(stdout, "Copyright (c) 2017-2021 Sampo Hippeläinen (hisahi)\n"
                    "This program is free software and comes with ABSOLUTELY "
                    "NO WARRANTY.\n");
}

#define PRINT_FLAG(fmt, fl) fprintf(stdout, "%s=" fmt "\n", #fl, fl);

static void lrg_printversionversion(void) {
    lrg_printversion();
    fprintf(stdout, "Full build flags:\n");
    PRINT_FLAG("%d", LRG_POSIX);
    PRINT_FLAG("%d", LRG_C99);
    PRINT_FLAG("%d", LRG_C11);
    PRINT_FLAG("%d", LRG_BACKWARD_SCAN);
    PRINT_FLAG("%d", LRG_BACKWARD_SCAN_THRESHOLD);
    PRINT_FLAG("%d", LRG_HOSTED_MEMCNT);
    PRINT_FLAG("%d", LRG_FAST_MEMCNT);
    PRINT_FLAG("%d", LRG_FILLBUF_MODE);
    PRINT_FLAG("%d", LRG_BUFSIZE);
    PRINT_FLAG("%d", LRG_LINEBUFSIZE);
    PRINT_FLAG("%d", LRG_BUFFER_ALIGN);
    PRINT_FLAG("%d", LRG_SUPPORT_LPS);
    PRINT_FLAG("%" LINENUM_FMT, LINENUM_MAX);
    PRINT_FLAG("%%%s", LINENUM_FMT);
}

static void lrg_printhelp(void) {
    lrg_printversion();
    fprintf(stdout,
            "\nUsage: %s [OPTION]... range[,range]... "
            "[input-file]...\n"
            "Prints a specific range of lines from the given file.\n"
            "Note that 'rewinding' might be impossible - once a line "
            "has been printed,\nit is possible that only lines after it "
            "can be printed.\n"
            "Line numbers start at 1.\n\n",
            myname);
    fprintf(stdout,
            "  -?, --help\n"
            "                 prints this message\n"
            "  --version\n"
            "                 prints version information\n"
            "  -e, --error-on-eof\n"
            "                 treat premature EOF as an error\n"
            "  -f, --file-names\n"
            "                 print file names before each file\n"
            "  -l, --line-numbers\n"
            "                 print line numbers before each line\n"
            "  -w, --warn-eof\n"
            "                 print a warning when a line is not found\n");
#if LRG_SUPPORT_LPS
    fprintf(stdout,
            "  --lps, --lines-per-second <x>\n"
            "                 prints lines at an (approximate) top speed\n"
            "                 (minimum 0.001, maximum 1000000)\n");
#endif
    fprintf(stdout,
            "\nLine range formats:\n"
            "   N\n"
            "                 the line with line number N\n"
            "   N-[M]\n"
            "                 lines between lines N and M (inclusive)\n"
            "                 if M not specified, goes until end of file\n"
            "   N~[M]\n"
            "                 the lines around line number N\n"
            "                 equivalent to (N-M)-(N+M), therefore\n"
            "                 displaying 2*M+1 lines\n"
            "                 if M not specified, defaults to 3\n\n");
}

/* used in error messages */
#define OPER_SEEK "seeking"
#define OPER_OPEN "opening"
#define OPER_READ "reading"

/* error messages */
#define OPT_ERR_INVAL "invalid option"
#define OPT_ERR_UNSUP "option not supported on this build"
#define OPT_ERR_PARAM "invalid or missing parameter"
#define TRY_HELP "Try '%s --help' for more information.\n"

INLINE void lrg_showusage(void) {
    fprintf(stderr,
            "Usage: %s [OPTION]... range"
            "[,range]... [input-file]...\n" TRY_HELP,
            myname, myname);
}

INLINE void lrg_perror(const char *fn, const char *open) {
    /* open = OPER_SEEK, OPER_OPEN or OPER_READ */
    fprintf(stderr, "%s: error %s %s: %s\n", myname, open, fn, strerror(errno));
}

INLINE void lrg_optc_error(const char *err, char c) {
    fprintf(stderr, "%s: %s -- '%c'\n" TRY_HELP, myname, err, c, myname);
}

INLINE void lrg_opts_error(const char *err, const char *s) {
    fprintf(stderr, "%s: %s -- '%s'\n" TRY_HELP, myname, err, s, myname);
}

INLINE void lrg_invalid_range(const char *meta) {
    fprintf(stderr, "%s: invalid range -- '%s'\n" TRY_HELP, myname, meta,
            myname);
}

INLINE void lrg_no_rewind(const char *fn, const char *meta) {
    fprintf(stderr,
            "%s: %s: trying to rewind, but input file not seekable -- "
            "'%s'\n" TRY_HELP,
            myname, fn, meta, myname);
}

INLINE void lrg_eof_before(const char *fn, linenum_t target, linenum_t last) {
    fprintf(stderr,
            "%s: %s: EOF before line %" LINENUM_FMT " (last = %" LINENUM_FMT
            ")\n",
            myname, fn, target, last);
}

INLINE void lrg_broken_pipe(void) {
    fprintf(stderr, "%s: error writing output: %s\n", myname, strerror(errno));
}

INLINE void lrg_alloc_fail(void) {
    fprintf(stderr, "%s: out of memory\n", myname);
}

/* ========================================================= */
/*                   memcnt implementation                   */
/* ========================================================= */

/* counts the number of bytes equal to value within a buffer */
#if !LRG_HOSTED_MEMCNT
size_t memcnt(const void *ptr, int value, size_t num) {
    size_t c = 0;
    const unsigned char *p = (const unsigned char *)ptr,
                        v = (unsigned char)value;
    while (num--)
        c += *p++ == v;
    return c;
}
#endif

/* ========================================================= */
/*                     main program code                     */
/* ========================================================= */

ALIGNAS(LRG_BUFFER_ALIGN) static char tmpbuf[LRG_BUFSIZE];
static struct lrg_linerange st_linesbuf[LRG_LINEBUFSIZE];
static struct lrg_linerange *linesbuf = st_linesbuf;
/* number of line ranges */
static size_t n_linesbuf = 0;
/* capacity. if we run past, we need to reallocate the line range buffer */
static size_t c_linesbuf = sizeof(st_linesbuf) / sizeof(st_linesbuf[0]);

#if LRG_POSIX /* optimized POSIX implementation */

#define GET_FILE_FD fileno
#define FILEREF int
#define FD_SEEK_SET(fd, n) (lseek(fd, n, SEEK_SET) < 0)
#define FD_SEEK_CUR(fd, n) (lseek(fd, n, SEEK_CUR) < 0)

INLINE void lrg_initbuffers(void) {}

/* 0 for EOF, -1 for error */
INLINE int lrg_fillbuf_file(char *buffer, size_t bufsize, int fd) {
    return read(fd, buffer, bufsize);
}

#define lrg_fillbuf_pipe lrg_fillbuf_file
/* since pipe/file impls are the same, just use one of them */
#undef LRG_FILLBUF_MODE
#define LRG_FILLBUF_MODE 0

#else /* standard C implementation */

#define FILEREF FILE *

INLINE void lrg_initbuffers(void) {}

/* 0 for EOF, -1 for error */
INLINE int lrg_fillbuf_file(char *buffer, size_t bufsize, FILE *file) {
    size_t res = fread(buffer, 1, bufsize, file);
    return UNLIKELY(res < bufsize && ferror(file)) ? -1 : res;
}

/* 0 for EOF, -1 for error */
/* we do not use fread for pipes because it blocks until the buffer is full.
   however, this getc loop tends to be pretty slow compared to fread,
   but no real alternative exists. fgets is not suitable, as it cannot
   gracefully handle lines that contain null characters but do **not** end in
   a newline (at the very end of a file). there is no fast way to reliably tell
   its true length. */
INLINE int lrg_fillbuf_pipe(char *buffer, size_t bufsize, FILE *file) {
    char *p = buffer;
    int c;
    while (bufsize--) {
        c = getc(file);
        if (UNLIKELY(c == EOF)) {
            if (ferror(file))
                return -1;
            else
                break;
        }
        *p++ = c;
        if (UNLIKELY(c == '\n'))
            break;
    }
    return p - buffer;
}

#endif

static int lrg_read_linenum(char *str, char **endptr, linenum_t *out,
                            linenum_t fallback, char allow_zero) {
    linenum_t result;
    while (isspace(*str))
        ++str;
    if (*str == '-')
        result = 0, *endptr = str;
    else {
        result = STR_TO_LINENUM(str, (char **)endptr, 10);
        if (result == LINENUM_MAX && errno == ERANGE)
            return -1;
    }
    if (!result && (!allow_zero || str == *endptr))
        result = fallback;
    *out = result;
    return allow_zero || result != 0;
}

/* 0 = ok, < 0 = fail, > 0 = end */
static int lrg_next_linerange(char **RESTRICT ptr, linenum_t *RESTRICT start,
                              linenum_t *RESTRICT end) {
    char *str = *ptr, *endptr;
    linenum_t line0;

    if (!*str) /* end */
        return 1;
    /* must have valid line0 */
    if (lrg_read_linenum(str, &endptr, &line0, 0, 0) <= 0)
        return -1;

    if (*endptr == '-') { /* 50-100... */
        linenum_t line1;
        if (lrg_read_linenum(endptr + 1, &endptr, &line1, LINENUM_MAX, 0) < 0)
            return -1;
        *start = line0, *end = line1;

    } else if (*endptr == '~') { /* 50~ or 50~10... */
        linenum_t linec;
        if (lrg_read_linenum(endptr + 1, &endptr, &linec, 3, 1) < 0)
            return -1;
        *start = line0 > linec ? line0 - linec : 1;
        if (line0 + linec < line0) /* overflow protection */
            return -1;
        *end = line0 + linec;

    } else {
        *start = line0, *end = line0;
    }

    if (*endptr == ',') /* 2,5-6,10~3,... */
        *endptr++ = 0;  /* for printing .text later on error */
    else if (*endptr)   /* only comma or end of string allowed */
        return -1;

    *ptr = endptr;
    return 0;
}

static void lrg_free_linebuf(void) { lrg_free(linesbuf); }

static int lrg_parse_lines(char *ln) {
    char *oldptr = ln;
    int pl = 0;
    linenum_t l0 = 0, l1 = 0;

    while ((pl = lrg_next_linerange(&ln, &l0, &l1)) <= 0) {
        if (pl < 0) {
            lrg_invalid_range(oldptr);
            return 1;
        }

        if (n_linesbuf == c_linesbuf) {
            /* don't try to call realloc on the static buffer! */
            if (linesbuf == st_linesbuf) {
                linesbuf = lrg_malloc(sizeof(struct lrg_linerange) *
                                      (c_linesbuf *= 2));
                if (!linesbuf) {
                    lrg_alloc_fail();
                    return 1;
                }
                memcpy(linesbuf, st_linesbuf,
                       sizeof(struct lrg_linerange) * n_linesbuf);
                atexit(&lrg_free_linebuf);

            } else {
                struct lrg_linerange *newptr = lrg_realloc(
                    linesbuf, sizeof(struct lrg_linerange) * (c_linesbuf *= 2));
                if (!newptr) {
                    lrg_alloc_fail();
                    return 1;
                }
                linesbuf = newptr;
            }
        }

        linesbuf[n_linesbuf].first = l0;
        linesbuf[n_linesbuf].last = l1;
        linesbuf[n_linesbuf].text = oldptr;
        ++n_linesbuf;

        oldptr = ln;
    }

    return 0;
}

#define JUMP_LINE(ln)                                                          \
    do {                                                                       \
        lrg_initbuffers();                                                     \
        buf_next = buf_end;                                                    \
        linenum = ln;                                                          \
    } while (0);

INLINE int lrg_processfile(const char *fn, FILE *f) {
    int read_n, had_eol, can_seek = !fseek(f, 0, SEEK_SET),
                         show_this_linenum = show_linenums;
    char *buf_prev, *buf_next, *buf_end = NULL;
    struct lrg_linerange range;
    linenum_t linenum, eof_at = LINENUM_MAX;
    size_t range_i;

#ifdef GET_FILE_FD
    int fd = GET_FILE_FD(f);
#define READ_BUFFER_FILE(buf, sz) lrg_fillbuf_file(buf, sz, fd)
#define READ_BUFFER_PIPE(buf, sz) lrg_fillbuf_pipe(buf, sz, fd)
#define FILE_SEEK_SET(n) FD_SEEK_SET(fd, n)
#define FILE_SEEK_CUR(n) FD_SEEK_CUR(fd, n)
#else
#define READ_BUFFER_FILE(buf, sz) lrg_fillbuf_file(buf, sz, f)
#define READ_BUFFER_PIPE(buf, sz) lrg_fillbuf_pipe(buf, sz, f)
#define FILE_SEEK_SET(n) fseek(f, n, SEEK_SET)
#define FILE_SEEK_CUR(n) fseek(f, n, SEEK_CUR)
#endif

#if LRG_FILLBUF_MODE == 0
#define READ_BUFFER(buf, sz) READ_BUFFER_FILE(buf, sz)
#elif LRG_FILLBUF_MODE == 1
#define READ_BUFFER(buf, sz) READ_BUFFER_PIPE(buf, sz)
#undef LRG_BACKWARD_SCAN
#define LRG_BACKWARD_SCAN 0
#else
    /* piece of cake for the branch predictor */
#define READ_BUFFER(buf, sz)                                                   \
    (can_seek ? (READ_BUFFER_FILE(buf, sz)) : (READ_BUFFER_PIPE(buf, sz)))
#endif

    JUMP_LINE(1);
    read_n = 0;

    for (range_i = 0; range_i < n_linesbuf; ++range_i) {
        range = linesbuf[range_i];

        if (UNLIKELY(range.first > range.last))
            continue;

        if (UNLIKELY(range.first > eof_at)) {
            /* we already know this won't work */
            if (warn_noline)
                lrg_eof_before(fn, range.first, eof_at);
            if (error_on_eof)
                return 0;
            continue;
        }

        /* do we need to go back? */
        if (UNLIKELY(range.first < linenum)) {
            if (!can_seek) {
                /* this is not a seekable file! cannot rewind */
                lrg_no_rewind(fn, range.text);
                return 1;
            }

#if LRG_BACKWARD_SCAN
            if (range.first > LRG_BACKWARD_SCAN_THRESHOLD &&
                range.first > linenum / 2) {
                linenum -= memcnt(tmpbuf, '\n', buf_next - tmpbuf);
                /* scan backwards until we reach the correct previous line.
                   can_seek assumed to be true and range.first > 1 */
                while (linenum >= range.first) {
                    if (FILE_SEEK_CUR(-(long)(read_n + sizeof(tmpbuf))))
                        goto jump_backwards;
                    read_n = READ_BUFFER(tmpbuf, sizeof(tmpbuf));
                    if (read_n < sizeof(tmpbuf))
                        goto read_error;
                    linenum -= memcnt(tmpbuf, '\n', read_n);
                }
                /* no jump. the buffer is already full of what we need */
                buf_next = tmpbuf, buf_end = tmpbuf + read_n;
            } else
            jump_backwards: /* goto abuse. this is somehow allowed! */
#endif
            {
                if (FILE_SEEK_SET(0)) {
                    lrg_perror(fn, OPER_SEEK);
                    lrg_no_rewind(fn, range.text);
                    return 1;
                }
                JUMP_LINE(1);
            }
        }

        for (;;) {
            /* have to read more? */
            if (buf_next == buf_end) {
                do {
                    read_n = READ_BUFFER(tmpbuf, sizeof(tmpbuf));
                    if (UNLIKELY(read_n <= 0))
                        goto read_error;
#if LRG_FAST_MEMCNT
                    if (linenum < range.first &&
                        range.first - linenum > read_n) {
                        /* still a long way to go. if we are still 2000 lines
                           away and the buffer has 1000 bytes, obviously this
                           data will not have the line we're looking for. thus,
                           we'll just count the number of newlines and fill
                           the buffer with new data */
                        linenum += memcnt(tmpbuf, '\n', read_n);
                        continue;
                    }
#endif
                    buf_next = tmpbuf, buf_end = tmpbuf + read_n;
                } while (0);
            }

            buf_prev = buf_next;
            buf_next = memchr(buf_next, '\n', buf_end - buf_next);
            had_eol = buf_next != NULL;
            buf_next = had_eol ? buf_next + 1 : buf_end;

            if (linenum < range.first) {
                linenum += had_eol;
                continue;
            }

            if (show_this_linenum) /* show one line number and then not again */
                printf(LINE_DISPLAY_FMT, linenum), show_this_linenum = 0;

            /* by using fwrite this way, it returns 1 for successful write */
            if (UNLIKELY(!fwrite(buf_prev, buf_next - buf_prev, 1, stdout))) {
                lrg_broken_pipe();
                return 1;
            }

            if (had_eol) {
#if LRG_SUPPORT_LPS
                if (lps_enable)
                    lps_sleep();
#endif
                show_this_linenum = show_linenums; /* maybe show again */
                if (linenum++ == range.last)
                    break;
            }
        }
        /* linenum is one past range.last here if all went well */

    read_error:
        if (UNLIKELY(read_n < 0)) {
            /* file error */
            lrg_perror(fn, OPER_READ);
            return 1;
        }
        if (UNLIKELY(read_n == 0 && range.last != LINENUM_MAX)) {
            /* reached the end of the file before first or last line */
            eof_at = linenum;
            if (warn_noline)
                lrg_eof_before(
                    fn, linenum >= range.first ? range.last : range.first,
                    eof_at);
            got_eof = 1;
            if (error_on_eof)
                return 0;
            read_n = buf_end - tmpbuf;
        }
    }
    return 0;
}

static int lrg_nextfile(const char *fn) {
    FILE *f;
    int returncode;

    if (!fn || !strcmp(fn, STDIN_FILE)) {
        f = stdin;
        fn = STDIN_FILENAME_APPEARANCE;
    } else {
        f = fopen(fn, LRG_BACKWARD_SCAN ? "rb" : "r");
        if (!f) {
            lrg_perror(fn, OPER_OPEN);
            return 1;
        }
    }

    if (show_files)
        printf(FILE_DISPLAY_FMT, fn);

    returncode = lrg_processfile(fn, f);

    if (f != stdin)
        fclose(f);
    return returncode;
}

int main(int argc, char *argv[]) {
    int flag_ok = 1, i, fend = 0, inputLines = 0;
    myname = argv[0];
    setlocale(LC_NUMERIC, "C");

    /* note: we will modify argv so that it only has the input files */
    for (i = 1; i < argc; ++i) {
        if (flag_ok && argv[i][0] == '-' && argv[i][1]) {
            if (argv[i][1] == '-') {
                /* -- = long flag */
                char *rest = argv[i] + 2;
                if (!*rest) { /* "--" - end of flags */
                    flag_ok = 0;
                    continue;
                } else if (!strcmp(rest, "line-numbers")) {
                    show_linenums = 1;
                } else if (!strcmp(rest, "file-names")) {
                    show_files = 1;
                } else if (!strcmp(rest, "warn-eof")) {
                    warn_noline = 1;
                } else if (!strcmp(rest, "error-on-eof")) {
                    error_on_eof = 1;
                } else if (!strcmp(rest, "lps") ||
                           !strcmp(rest, "lines-per-second")) {
#if LRG_SUPPORT_LPS
                    float f = 0;
                    if (++i < argc)
                        f = atof(argv[i]);
                    if (f <= 0.001f || f > 1000000.f) {
                        lrg_opts_error(OPT_ERR_PARAM, rest);
                        return EXITCODE_USE;
                    }
                    lps_init(f);
#else
                    lrg_opts_error(OPT_ERR_UNSUP, rest);
                    return EXITCODE_USE;
#endif
                } else if (!strcmp(rest, "help")) {
                    lrg_printhelp();
                    return EXITCODE_OK;
                } else if (!strcmp(rest, "version")) {
                    lrg_printversion();
                    return EXITCODE_OK;
                } else if (!strcmp(rest, "versionversion")) {
                    lrg_printversionversion();
                    return EXITCODE_OK;
                } else {
                    lrg_opts_error(OPT_ERR_INVAL, rest);
                    return EXITCODE_USE;
                }
            } else {
                char *cp = argv[i] + 1, c;
                while (*cp) {
                    c = *cp++;
                    if (c == 'l') {
                        show_linenums = 1;
                    } else if (c == 'e') {
                        error_on_eof = 1;
                    } else if (c == 'f') {
                        show_files = 1;
                    } else if (c == 'w') {
                        warn_noline = 1;
                    } else if (c == '?') {
                        lrg_printhelp();
                        return EXITCODE_OK;
                    } else {
                        lrg_optc_error(OPT_ERR_INVAL, c);
                        return EXITCODE_USE;
                    }
                }
            }
        } else if (!inputLines) {
            inputLines = i;
            if (lrg_parse_lines(argv[inputLines]))
                return EXITCODE_USE;
        } else {
            argv[fend++] = argv[i];
        }
    }

    if (!n_linesbuf) {
        lrg_showusage();
        return EXITCODE_USE;
    }

    if (!fend) { /* no input files */
        if (lrg_nextfile(NULL))
            return EXITCODE_ERR;
    } else {
        for (i = 0; i < fend; ++i) {
            if (lrg_nextfile(argv[i]))
                return EXITCODE_ERR;
        }
    }

    if (error_on_eof && got_eof)
        return EXITCODE_ERR;

    return EXITCODE_OK;
}
