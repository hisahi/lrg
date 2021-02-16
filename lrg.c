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
    const char *text;
};

struct lrg_lineout {
    char *data;
    size_t size;
    int eol;
};

#if LRG_POSIX
#define LRG_BUFSIZE BUFSIZ
#else
#define LRG_BUFSIZE 404
#endif

char tmpbuf[LRG_BUFSIZE + 1];
struct lrg_linerange linesbuf_static[64];
struct lrg_linerange *linesbuf = linesbuf_static;
size_t linesbuf_n = 0;
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

/* support code */

#if LRG_POSIX

#define NS_PER_SEC 1000000000L

char lrg_lps_enable = 0;
struct timespec posixnsreq;

void lrg_lps_init(float lps) {
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

INLINE void lrg_perror(const char *fn, const char *open) {
    fprintf(stderr, "%s: error %s %s: %s\n", myname, open, fn, strerror(errno));
}

INLINE void lrg_showusage(void) {
    fprintf(stderr,
            "Usage: %s [OPTION]... range"
            "[,range]... [input-file]...\n"
            "Try '%s --help' for more information.\n",
            myname, myname);
}

INLINE void lrg_invalid_option_c(char c) {
    fprintf(stderr,
            "%s: invalid option -- '%c'\n"
            "Try '%s --help' for more information.\n",
            myname, c, myname);
}

INLINE void lrg_invalid_option_s(const char *s) {
    fprintf(stderr,
            "%s: invalid option -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, s, myname);
}

INLINE void lrg_not_supported(const char *s) {
    fprintf(stderr,
            "%s: option not supported on this build -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, s, myname);
}

INLINE void lrg_invalid_param(const char *s) {
    fprintf(stderr,
            "%s: invalid or missing parameter -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, s, myname);
}

INLINE void lrg_invalid_range(const char *meta) {
    fprintf(stderr,
            "%s: invalid range -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, meta, myname);
}

INLINE void lrg_no_rewind(const char *meta) {
    fprintf(stderr,
            "%s: trying to rewind, but input file not seekable -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, meta, myname);
}

INLINE void lrg_eof_before(linenum_t target, linenum_t last) {
    fprintf(stderr, "%s: EOF before line %ld (last = %ld)\n", myname, target,
            last);
}

INLINE void lrg_alloc_fail(void) {
    fprintf(stderr, "%s: out of memory\n", myname);
}

/* end of translatable strings, beginning of code */

#if LRG_POSIX
/* optimized POSIX implementation */

#define NEXTLINE_FD 1
#define GET_FILE_FD fileno

char *buf_next;
char *buf_end;

INLINE void lrg_initbufs(void) {}

INLINE void lrg_initline(struct lrg_lineout *line) {
    buf_next = buf_end = NULL;
    line->data = tmpbuf;
    line->size = 0;
    line->eol = 0;
}

INLINE int lrg_nextline(struct lrg_lineout *line, int fd) {
    char *ptr;
    if (buf_next >= buf_end) {
        ssize_t n = read(fd, tmpbuf, sizeof(tmpbuf) - 1);
        if (n <= 0)
            return n; /* 0 for EOF, -1 for error */
        buf_end = tmpbuf + n;
        buf_next = tmpbuf;
        *buf_end = '\n'; /* must be here; see below */
    }

    line->data = buf_next;
    ptr = memchr(buf_next, '\n', buf_end + 1 - buf_next);
    line->size = ptr - buf_next;
    line->eol = ptr != buf_end;
    buf_next = ptr + 1;
    return 1;
}

#else
/* standard C implementation */

INLINE void lrg_initbufs(void) {
    tmpbuf[sizeof(tmpbuf) - 1] = '\n'; /* must be here; see lrg_nextline */
}

INLINE void lrg_initline(struct lrg_lineout *line) {
    line->data = tmpbuf;
    line->size = 0;
    line->eol = 0;
}

INLINE int lrg_nextline(struct lrg_lineout *line, FILE *file) {
    size_t sz;
    if (!fgets(tmpbuf, sizeof(tmpbuf) - 1, file))
        return feof(file) ? 0 : -1;
    else
        /* this is safe, as tmpbuf is initialized to end with '\n' */
        sz = (char *)memchr(tmpbuf, '\n', sizeof(tmpbuf)) - tmpbuf;

    line->size = sz;
    line->eol = sz < sizeof(tmpbuf) - 1;
    return 1;
}

#endif

int lrg_read_linenum(const char *str, const char **endptr, linenum_t *out,
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
int lrg_next_linerange(const char **RESTRICT ptr, linenum_t *RESTRICT start,
                       linenum_t *RESTRICT end) {
    const char *str = *ptr;
    const char *endptr;
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
        ++endptr;
    else if (*endptr) /* only comma or end of string allowed */
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

int lrg_parse_lines(const char *ln) {
    const char *oldptr = ln;
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

int lrg_nextfile(const char *fn) {
    FILE *f;
    int can_seek = 1;
    int status = 0;
    int returncode = 0;
    int show_this_linenum = show_linenums;
    struct lrg_lineout line;
    struct lrg_linerange range;
    size_t i;
    linenum_t linenum = 1;
#if NEXTLINE_FD
    int fd;
#endif

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

    can_seek = !fseek(f, 0, SEEK_SET);

#if NEXTLINE_FD
    fd = GET_FILE_FD(f);
#endif

    if (show_files)
        puts(fn); /* printf("%s\n", fn); */

    lrg_initline(&line);

    for (i = 0; i < linesbuf_n; ++i) {
        range = linesbuf[i];

        if (range.first <= linenum) {
            if (can_seek) {
                if (fseek(f, 0, SEEK_SET)) {
                    lrg_perror(fn, OPER_SEEK);
                    lrg_no_rewind(range.text);
                    returncode = 1;
                    goto unwind;
                }
                linenum = 1;
            } else {
                lrg_no_rewind(range.text);
                returncode = 1;
                goto unwind;
            }
        }

#if NEXTLINE_FD
        while (linenum < range.first &&
               (status = lrg_nextline(&line, fd)) > 0) {
#else
        while (linenum < range.first && (status = lrg_nextline(&line, f)) > 0) {
#endif
            linenum += line.eol;
        }

#if NEXTLINE_FD
        while ((status = lrg_nextline(&line, fd)) > 0) {
#else
        while ((status = lrg_nextline(&line, f)) > 0) {
#endif
            if (show_this_linenum)
                printf(" %7lu   ", linenum), show_this_linenum = 0;
            fwrite(line.data, 1, line.size + line.eol, stdout);

            if (line.eol) {
                if (linenum == range.last)
                    break;
#if LRG_SUPPORT_LPS
                if (lrg_lps_enable)
                    lrg_lps_sleep();
#endif
                ++linenum;
                show_this_linenum = show_linenums;
            }
        }

        if (status < 0) {
            /* file error */
            lrg_perror(fn, OPER_READ);
            returncode = 1;
            goto unwind;
        }
        if (status == 0 && range.last != LINENUM_MAX) {
            /* reached the end of the file before first or last line */
            if (warn_noline)
                lrg_eof_before(
                    linenum >= range.first ? range.last : range.first, linenum);
            returncode = 0;
            goto unwind;
        }
    }
unwind:
    if (f != stdin)
        fclose(f);
    return returncode;
}

int main(int argc, char **argv) {
    int flag_ok = 1, i, fend = 0, inputLines = 0;
    myname = argv[0];
    setlocale(LC_NUMERIC, "C");
    lrg_initbufs();

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
                        lrg_invalid_param(rest);
                        return EXITCODE_USE;
                    }
                    lrg_lps_init(f);
#else
                    lrg_not_supported(rest);
                    return EXITCODE_USE;
#endif
                } else if (!strcmp(rest, "help")) {
                    lrg_printhelp();
                    return EXITCODE_OK;
                } else {
                    lrg_invalid_option_s(rest);
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
                        lrg_invalid_option_c(c);
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
        flag_ok = 1;
        for (i = 0; i < fend; ++i) {
            if (lrg_nextfile(argv[i]))
                return EXITCODE_ERR;
        }
    }

    return EXITCODE_OK;
}
