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

#define LRG_POSIX (_POSIX_C_SOURCE >= 199309L)

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if LRG_POSIX
#include <time.h>
#include <unistd.h>
#endif

/* definitions */

typedef unsigned long linenum_t;
#define LINENUM_MAX ULONG_MAX

struct lrg_linerange {
    linenum_t first;
    linenum_t last;
    /* original range format given as a parameter. used for error msgs */
    const char *text;
};

/* 0 = always use fillbuf_file. 1 = always use fillbuf_pipe.
   2 (or anything else) = auto (use _file if seekable, else _pipe).
   consider setting to 0 or 1 if branches are expensive (even if predictable) */
#define LRG_FILLBUF_MODE 2

#define LRG_BUFSIZE BUFSIZ

/* 1 more for final terminating newline */
char tmpbuf[LRG_BUFSIZE + 1];
struct lrg_linerange linesbuf_static[64];
struct lrg_linerange *linesbuf = linesbuf_static;
/* number of line ranges */
size_t linesbuf_n = 0;
/* capacity. if we run past, we need to reallocate the line range buffer */
size_t linesbuf_c = sizeof(linesbuf_static) / sizeof(linesbuf_static[0]);

static const char *STDIN_FILE = "-";
const char *myname;
int show_linenums = 0, show_files = 0, warn_noline = 0;

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

/* support code */

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

/* strings in funcs for translations */

void lrg_printhelp(void) {
    fprintf(stderr, "lrg by Sampo Hippeläinen (hisahi) - version " __DATE__ "\n"
#if LRG_POSIX
                    "POSIX version\n"
#else
                    "ANSI C version\n"
#endif
    );
    fprintf(stderr,
            "Usage: %s [OPTION]... range[,range]... "
            "[input-file]...\n"
            "Prints a specific range of lines from the given file.\n"
            "Note that 'rewinding' might be impossible - once a line "
            "has been printed,\nit is possible that only lines after it "
            "can be printed.\n"
            "Line numbers start at 1.\n\n",
            myname);
    fprintf(stderr,
            "  -h, --help\n"
            "                 prints this message\n"
            "  -f, --file-names\n"
            "                 print file names before each file\n"
            "  -l, --line-numbers\n"
            "                 print line numbers before each line\n"
            "  -w, --warn-eof\n"
            "                 print a warning when a line is not found\n");
#if LRG_SUPPORT_LPS
    fprintf(stderr,
            "  --lps, --lines-per-second <x>\n"
            "                 prints lines at an (approximate) top speed\n"
            "                 (minimum 0.001, maximum 1000000)\n");
#endif
    fprintf(stderr,
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

INLINE void lrg_no_rewind(const char *meta) {
    fprintf(
        stderr,
        "%s: trying to rewind, but input file not seekable -- '%s'\n" TRY_HELP,
        myname, meta, myname);
}

INLINE void lrg_eof_before(linenum_t target, linenum_t last) {
    fprintf(stderr, "%s: EOF before line %ld (last = %ld)\n", myname, target,
            last);
}

INLINE void lrg_broken_pipe(void) {
    fprintf(stderr, "%s: error writing output: %s\n", myname, strerror(errno));
}

INLINE void lrg_alloc_fail(void) {
    fprintf(stderr, "%s: out of memory\n", myname);
}

/* end of translatable strings, beginning of code */

#if LRG_POSIX /* optimized POSIX implementation */

#define GET_FILE_FD fileno
#define FILEREF int
/* there can be multiple newlines in a single buffer */
#define ONE_LINE_PER_READ_FILE 0
#define ONE_LINE_PER_READ_PIPE 0

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
/* make sure we read the next line even if the "buffer" is not over yet
   only applies to pipes, fread might result in multiple lines */
#define ONE_LINE_PER_READ_FILE 0
#define ONE_LINE_PER_READ_PIPE 1

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
   a newline (at the very end of a file). there is no way to reliably tell
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

void lrg_free_linebuf(void) { free(linesbuf); }

#define SWAP(T, x, y)                                                          \
    do {                                                                       \
        T tmp = x;                                                             \
        x = y;                                                                 \
        y = tmp;                                                               \
    } while (0)

int lrg_parse_lines(char *ln) {
    char *oldptr = ln;
    int pl = 0;
    linenum_t l0 = 0, l1 = 0;

    while ((pl = lrg_next_linerange(&ln, &l0, &l1)) <= 0) {
        if (pl < 0) {
            lrg_invalid_range(oldptr);
            return 1;
        }

        if (l1 < l0)
            SWAP(linenum_t, l0, l1);

        if (linesbuf_n == linesbuf_c) {
            if (linesbuf == linesbuf_static) {
                linesbuf =
                    malloc(sizeof(struct lrg_linerange) * (linesbuf_c *= 2));
                if (!linesbuf) {
                    lrg_alloc_fail();
                    return 1;
                }
                memcpy(linesbuf, linesbuf_static,
                       sizeof(struct lrg_linerange) * linesbuf_n);
                atexit(&lrg_free_linebuf);

            } else {
                linesbuf = realloc(linesbuf, sizeof(struct lrg_linerange) *
                                                 (linesbuf_c *= 2));
                if (!linesbuf) {
                    lrg_alloc_fail();
                    return 1;
                }
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
    int status;
    int can_seek = !fseek(f, 0, SEEK_SET);
    int show_this_linenum = show_linenums;
    char *buf_prev, *buf_next, *buf_end = NULL, *next_eol;
    struct lrg_linerange range;
    linenum_t linenum;
    size_t range_i;

#ifdef GET_FILE_FD
    int fd = GET_FILE_FD(f);
#define READ_BUFFER_FILE(buf, sz) lrg_fillbuf_file(buf, sz, fd)
#define READ_BUFFER_PIPE(buf, sz) lrg_fillbuf_pipe(buf, sz, fd)
#else
#define READ_BUFFER_FILE(buf, sz) lrg_fillbuf_file(buf, sz, f)
#define READ_BUFFER_PIPE(buf, sz) lrg_fillbuf_pipe(buf, sz, f)
#endif

#if LRG_FILLBUF_MODE == 0
#define oneline ONE_LINE_PER_READ_FILE
#define READ_BUFFER(buf, sz) READ_BUFFER_FILE(buf, sz)
#elif LRG_FILLBUF_MODE == 1
#define oneline ONE_LINE_PER_READ_PIPE
#define READ_BUFFER(buf, sz) READ_BUFFER_PIPE(buf, sz)
#else
    int oneline = can_seek ? ONE_LINE_PER_READ_FILE : ONE_LINE_PER_READ_PIPE;
    /* piece of cake for the branch predictor */
#define READ_BUFFER(buf, sz)                                                   \
    (can_seek ? (READ_BUFFER_FILE(buf, sz)) : (READ_BUFFER_PIPE(buf, sz)))
#endif

    JUMP_LINE(1);

    for (range_i = 0; range_i < linesbuf_n; ++range_i) {
        range = linesbuf[range_i];

        /* do we need to go back? */
        if (UNLIKELY(range.first < linenum)) {
            if (!can_seek) {
                /* this is not a seekable file! cannot rewind */
                lrg_no_rewind(range.text);
                return 1;
            }

            /* TODO: maybe not full rewind every time? we can
                keep a table of positions for line ranges by recording
                the position of their first line */
            if (fseek(f, 0, SEEK_SET)) {
                lrg_perror(fn, OPER_SEEK);
                lrg_no_rewind(range.text);
                return 1;
            }
            JUMP_LINE(1);
        }

        status = 1;
        for (;;) {
            /* have to read more? */
            if (oneline || buf_next == buf_end) {
                status = READ_BUFFER(tmpbuf, sizeof(tmpbuf));
                if (status <= 0)
                    break;
                buf_next = tmpbuf, buf_end = tmpbuf + status;
            }

            buf_prev = buf_next;
            next_eol = memchr(buf_next, '\n', buf_end - buf_next);
            buf_next = next_eol ? next_eol + 1 : buf_end;
            if (linenum < range.first) {
                if (LIKELY(next_eol != NULL))
                    ++linenum;
                continue;
            }

            if (show_this_linenum) /* show one line number and then not again */
                printf(" %7lu   ", linenum), show_this_linenum = 0;

            /* by using fwrite this way, it returns 1 for successful write */
            if (UNLIKELY(!fwrite(buf_prev, buf_next - buf_prev, 1, stdout))) {
                lrg_broken_pipe();
                return 1;
            }

            if (LIKELY(next_eol != NULL)) {
#if LRG_SUPPORT_LPS
                if (lrg_lps_enable)
                    lrg_lps_sleep();
#endif
                if (linenum++ == range.last)
                    break;
                show_this_linenum = show_linenums; /* maybe show again */
            }
        }
        /* linenum is one past range.last here if all went well */

        if (UNLIKELY(status < 0)) {
            /* file error */
            lrg_perror(fn, OPER_READ);
            return 1;
        }
        if (UNLIKELY(status == 0 && range.last != LINENUM_MAX)) {
            /* reached the end of the file before first or last line */
            if (warn_noline)
                lrg_eof_before(
                    linenum >= range.first ? range.last : range.first, linenum);
            return 0;
        }
    }
    return 0;
#undef oneline
}

int lrg_nextfile(const char *fn) {
    FILE *f;
    int returncode;

    if (!fn || !strcmp(fn, STDIN_FILE)) {
        f = stdin;
        fn = STDIN_FILENAME_APPEARANCE;
    } else {
        f = fopen(fn, "r");
        if (!f) {
            lrg_perror(fn, OPER_OPEN);
            return 1;
        }
    }

    if (show_files)
        puts(fn); /* printf("%s\n", fn); */

    returncode = lrg_processfile(fn, f);

    if (f != stdin)
        fclose(f);
    return returncode;
}

int main(int argc, char **argv) {
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
                    } else if (c == 'f') {
                        show_files = 1;
                    } else if (c == 'w') {
                        warn_noline = 1;
                    } else if (c == 'h' || c == '?') {
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

    return EXITCODE_OK;
}
