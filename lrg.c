/*

Line RanGe (LRG)
A tool that allows displaying specific lines of files (or around a
specific line) with possible line number display

Copyright (c) 2017-2021 Sampo Hippeläinen (hisahi)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if _POSIX_C_SOURCE >= 199309L && !LRG_NO_POSIX
#define LRG_POSIX 1
#include <time.h>
#include <unistd.h>
#else
#define LRG_POSIX 0
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L && !LRG_NO_C99
#define LRG_C99 1
#include <stdint.h>
#else
#define LRG_C99 0
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

/* try to use fast memcnt on supported systems?
   if your compiler does autovectorization, the "fast" memcnt may actually be
   slower, in which case you should set this to 0 */
#ifndef LRG_TRY_FAST_MEMCNT
#define LRG_TRY_FAST_MEMCNT 1
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

/* buffer size */
#define LRG_BUFSIZE BUFSIZ * 4
/* line range buffer size. this is only the static allocation;
   if there are too many ranges to fit, dynamic allocation will be used to
   get an expanded buffer */
#define LRG_LINEBUFSIZE 32

#if LRG_C99
typedef unsigned long long linenum_t;
#define LINENUM_MAX ULLONG_MAX
#define LINENUM_FMT "llu"
#else
typedef unsigned long linenum_t;
#define LINENUM_MAX ULONG_MAX
#define LINENUM_FMT "lu"
#endif

#if LRG_POSIX
#define EXITCODE_OK 0
#define EXITCODE_ERR 1
#define EXITCODE_USE 2
#else
#define EXITCODE_OK EXIT_SUCCESS
#define EXITCODE_ERR EXIT_FAILURE
#define EXITCODE_USE EXIT_FAILURE
#endif

#if (__STDC_VERSION__ >= 199901L)
#define INLINE static inline
#define RESTRICT restrict
#else
#define INLINE
#define RESTRICT
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
   be macros */
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

char tmpbuf[LRG_BUFSIZE];
struct lrg_linerange linesbuf_static[LRG_LINEBUFSIZE];
struct lrg_linerange *linesbuf = linesbuf_static;
/* number of line ranges */
size_t linesbuf_n = 0;
/* capacity. if we run past, we need to reallocate the line range buffer */
size_t linesbuf_c = sizeof(linesbuf_static) / sizeof(linesbuf_static[0]);

static const char *STDIN_FILE = "-";
const char *myname;
int show_linenums = 0, show_files = 0, warn_noline = 0, error_on_eof = 0;
int got_eof = 0;

/* ========================================================= */
/*                       support code                        */
/* ========================================================= */

#if LRG_POSIX

#define NS_PER_SEC 1000000000L

char lrg_lps_enable = 0;
struct timespec posixnsreq;

void lrg_lps_init(float lps) {
    /* sleep for 1/lps seconds */
    posixnsreq.tv_sec = (long)(1. / lps);
    posixnsreq.tv_nsec = (long)(NS_PER_SEC / lps) % NS_PER_SEC;
    lrg_lps_enable = 1;
}

void lrg_lps_sleep(void) { nanosleep(&posixnsreq, NULL); }

/* we support the --lines-per-second flag */
#define LRG_SUPPORT_LPS 1

#endif

/* ========================================================= */
/*                          strings                          */
/* ========================================================= */
/* in a centralized location to help with localization */

void lrg_printversion(void) {
    fprintf(stdout, "lrg by Sampo Hippeläinen (hisahi) - version " __DATE__ "\n"
#if LRG_POSIX
                    "POSIX version\n"
#else
                    "ANSI C version\n"
#endif
    );
}

void lrg_printhelp(void) {
    lrg_printversion();
    fprintf(stdout,
            "Usage: %s [OPTION]... range[,range]... "
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

#define STDIN_FILENAME_APPEARANCE "(stdin)"
#define FILE_DISPLAY_FMT "%s\n"
#define LINE_DISPLAY_FMT " %7" LINENUM_FMT "   "

#define OPER_SEEK "seeking"
#define OPER_OPEN "opening"
#define OPER_READ "reading"

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
    fprintf(stderr, "%s: error %s %s: %s\n", myname, open, fn, strerror(errno));
}

INLINE void lrg_error_option_c(const char *err, char c) {
    fprintf(stderr, "%s: %s -- '%c'\n" TRY_HELP, myname, err, c, myname);
}

INLINE void lrg_error_option_s(const char *err, const char *s) {
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
/*                     main program code                     */
/* ========================================================= */

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
/* since pipe/file impls are the same, just always use one of them */
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
    while (LIKELY(bufsize--)) {
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

int lrg_read_linenum(char *str, char **endptr, linenum_t *out,
                     linenum_t fallback, char allow_zero) {
    linenum_t result;
    while (isspace(*str))
        ++str;
    if (*str == '-')
        result = 0, *endptr = str;
    else {
        result = strtoul(str, (char **)endptr, 10);
        if (result == ULONG_MAX && errno == ERANGE)
            return -1;
    }
    if (!result && (!allow_zero || str == *endptr))
        result = fallback;
    *out = result;
    return allow_zero || result != 0;
}

/* 0 = ok, < 0 = fail, > 0 = end */
int lrg_next_linerange(char **RESTRICT ptr, linenum_t *RESTRICT start,
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

void lrg_free_linebuf(void) { lrg_free(linesbuf); }

#if CHAR_BIT != 8 || !defined(LRG_MEMCNT_WORD) || !defined(UINTPTR_MAX)
/* unsupported */
#undef LRG_TRY_FAST_MEMCNT
#define LRG_TRY_FAST_MEMCNT 0
#endif

#if LRG_TRY_FAST_MEMCNT && defined(UINT64_MAX) && UINTPTR_MAX != UINT32_MAX && \
    !LRG_NO_UINT64
typedef uint64_t memcnt_word_t;
#define LRG_MEMCNT_WORD 64
#define LRG_MEMCNT_COUNT 8
#elif LRG_TRY_FAST_MEMCNT && defined(UINT32_MAX) && !LRG_NO_UINT32
typedef uint32_t memcnt_word_t;
#define LRG_MEMCNT_WORD 32
#define LRG_MEMCNT_COUNT 4
#else
#undef LRG_TRY_FAST_MEMCNT
#define LRG_TRY_FAST_MEMCNT 0
#endif

#if LRG_TRY_FAST_MEMCNT
#if __GNUC__
#define popcount32 __builtin_popcount
#define popcount64 __builtin_popcountl
#else
int popcount32(uint32_t v) {
    int c = 0;
    for (; v; ++c)
        v &= v - 1;
    return c;
}
int popcount64(uint64_t v) {
    int c = 0;
    for (; v; ++c)
        v &= v - 1;
    return c;
}
#endif
#endif

/* counts the number of bytes equal to value within a buffer */
size_t memcnt(const void *ptr, int value, size_t num) {
    size_t c = 0;
    const char *p = ptr;
#if LRG_TRY_FAST_MEMCNT
    if (num > LRG_MEMCNT_WORD * 4) {
#if LRG_MEMCNT_WORD == 32
        const memcnt_word_t mask = 0x01010101ULL;
#define POPCOUNT popcount32
#elif LRG_MEMCNT_WORD == 64
        const memcnt_word_t mask = 0x0101010101010101ULL;
#define POPCOUNT popcount64
#endif
        memcnt_word_t cmp = (memcnt_word_t)(value * mask), tmp;
        const memcnt_word_t *wp;
        while ((uintptr_t)p & (LRG_MEMCNT_COUNT - 1))
            --num, c += *p++ == value;
        wp = (const memcnt_word_t *)p;
        while (num >= LRG_MEMCNT_COUNT) {
            num -= LRG_MEMCNT_COUNT;
            tmp = *wp++ ^ cmp;
            /* count zero bytes in word tmp and add to c */
            tmp |= tmp >> 4;
            tmp |= tmp >> 2;
            tmp |= tmp >> 1;
            tmp &= mask;
            c += LRG_MEMCNT_COUNT - POPCOUNT(tmp);
        }
        p = (const char *)wp;
    }
#endif
    while (num--)
        c += *p++ == value;
    return c;
}

int lrg_parse_lines(char *ln) {
    char *oldptr = ln;
    int pl = 0;
    linenum_t l0 = 0, l1 = 0;

    while ((pl = lrg_next_linerange(&ln, &l0, &l1)) <= 0) {
        if (pl < 0) {
            lrg_invalid_range(oldptr);
            return 1;
        }

        if (l0 > l1) {
            oldptr = ln;
            continue;
        }

        if (linesbuf_n == linesbuf_c) {
            if (linesbuf == linesbuf_static) {
                linesbuf = lrg_malloc(sizeof(struct lrg_linerange) *
                                      (linesbuf_c *= 2));
                if (!linesbuf) {
                    lrg_alloc_fail();
                    return 1;
                }
                memcpy(linesbuf, linesbuf_static,
                       sizeof(struct lrg_linerange) * linesbuf_n);
                atexit(&lrg_free_linebuf);

            } else {
                struct lrg_linerange *newptr = lrg_realloc(
                    linesbuf, sizeof(struct lrg_linerange) * (linesbuf_c *= 2));
                if (!newptr) {
                    lrg_alloc_fail();
                    return 1;
                }
                linesbuf = newptr;
            }
        }

        linesbuf[linesbuf_n].first = l0;
        linesbuf[linesbuf_n].last = l1;
        linesbuf[linesbuf_n].text = oldptr;
        ++linesbuf_n;

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
    char *buf_prev, *buf_next, *buf_end = NULL, *next_eol;
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

    for (range_i = 0; range_i < linesbuf_n; ++range_i) {
        range = linesbuf[range_i];

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
                    if (FILE_SEEK_CUR(-(long)(read_n + sizeof(tmpbuf)))) {
                        if (FILE_SEEK_SET(0)) {
                            lrg_perror(fn, OPER_SEEK);
                            lrg_no_rewind(fn, range.text);
                            return 1;
                        }
                        JUMP_LINE(1);
                        goto backward_jumped;
                    }
                    read_n = READ_BUFFER(tmpbuf, sizeof(tmpbuf));
                    if (read_n < sizeof(tmpbuf))
                        goto read_error;
                    linenum -= memcnt(tmpbuf, '\n', read_n);
                }
                /* no jump. the buffer is already full of what we need */
                buf_next = tmpbuf, buf_end = tmpbuf + read_n;
            backward_jumped:;
            } else
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
                read_n = READ_BUFFER(tmpbuf, sizeof(tmpbuf));
                if (read_n <= 0)
                    goto read_error;
                buf_next = tmpbuf, buf_end = tmpbuf + read_n;
            }

            buf_prev = buf_next;
            next_eol = memchr(buf_next, '\n', buf_end - buf_next);
            had_eol = next_eol != NULL;
            buf_next = had_eol ? next_eol + 1 : buf_end;

            if (linenum < range.first) {
                if (LIKELY(had_eol))
                    ++linenum;
                continue;
            }

            if (show_this_linenum) /* show one line number and then not again */
                printf(LINE_DISPLAY_FMT, linenum), show_this_linenum = 0;

            /* by using fwrite this way, it returns 1 for successful write */
            if (UNLIKELY(!fwrite(buf_prev, buf_next - buf_prev, 1, stdout))) {
                lrg_broken_pipe();
                return 1;
            }

            if (LIKELY(had_eol)) {
#if LRG_SUPPORT_LPS
                if (lrg_lps_enable)
                    lrg_lps_sleep();
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

int lrg_nextfile(const char *fn) {
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
                        lrg_error_option_s(OPT_ERR_PARAM, rest);
                        return EXITCODE_USE;
                    }
                    lrg_lps_init(f);
#else
                    lrg_error_option_s(OPT_ERR_UNSUP, rest);
                    return EXITCODE_USE;
#endif
                } else if (!strcmp(rest, "help")) {
                    lrg_printhelp();
                    return EXITCODE_OK;
                } else if (!strcmp(rest, "version")) {
                    lrg_printversion();
                    return EXITCODE_OK;
                } else {
                    lrg_error_option_s(OPT_ERR_INVAL, rest);
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
                        lrg_error_option_c(OPT_ERR_INVAL, c);
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

    if (!linesbuf_n) {
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
