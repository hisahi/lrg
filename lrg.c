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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* definitions */

#define USE_POSIX_EXIT_CODES 1

typedef unsigned long linenum_t;
#define LINENUM_MAX ULONG_MAX

struct lrg_linerange {
    linenum_t first;
    linenum_t last;
    const char *text;
};

struct lrg_lineout {
    linenum_t line_num;
    char *data;
    size_t size;
    char *_buf;
    size_t _bsz;
    char eol;
    char newline;
};

char tmpbuf[404];
struct lrg_linerange linesbuf_static[64];
struct lrg_linerange *linesbuf = linesbuf_static;
size_t linesbuf_n = 0;
size_t linesbuf_c = sizeof(linesbuf_static) / sizeof(linesbuf_static[0]);

static const char *STDIN_FILE = "-";
const char *myname;
int show_lines = 0, show_files = 0, warn_noline = 0;

#ifdef USE_POSIX_EXIT_CODES
#define EXITCODE_OK 0
#define EXITCODE_ERR 1
#define EXITCODE_USE 2
#else
#define EXITCODE_OK EXIT_SUCCESS
#define EXITCODE_ERR EXIT_FAILURE
#define EXITCODE_USE EXIT_FAILURE
#endif

/* strings in funcs for translations */

void lrg_printhelp(void) {
    fprintf(stderr,
            "Usage: %s [OPTION]... range[,range]... "
            "[input-file]...\n"
            "Prints a specific range of lines from the given file.\n"
            "Note that 'rewinding' might be impossible - once a line "
            "has been printed,\nit is possible that only lines after it "
            "can be printed.\n"
            "Line numbers start at 1.\n\n"
            "  -h, --help\n"
            "                 prints this message\n"
            "  -f, --file-names\n"
            "                 print file names before each file\n"
            "  -l, --line-numbers\n"
            "                 print line numbers before each line\n",
            myname);
    fprintf(stderr,
            "  -w, --warn-eof\n"
            "                 print a warning when a line is not found\n\n"
            "Line range formats:\n"
            "   N\n"
            "                 line with line number N\n"
            "   N-[M]\n"
            "                 lines between lines N and M (inclusive)\n"
            "                 if M not specified, goes until end of file\n"
            "   N~[M]\n"
            "                 line numbers around N\n"
            "                 equivalent roughly to (N-M)-(N+M), therefore\n"
            "                 displaying 2*M+1 lines\n"
            "                 if M not specified, defaults to 3\n\n");
}

#define OPER_SEEK "seeking"
#define OPER_OPEN "opening"
#define OPER_READ "reading"

void lrg_perror(const char *fn, const char *open) {
    fprintf(stderr, "%s: error %s %s: %s\n", myname, open, fn, strerror(errno));
}

void lrg_showusage(void) {
    fprintf(stderr,
            "Usage: %s [OPTION]... range"
            "[,range]... [input-file]...\n"
            "Try '%s --help' for more information.\n",
            myname, myname);
}

void lrg_invalid_option_c(char c) {
    fprintf(stderr,
            "%s: invalid option -- '%c'\n"
            "Try '%s --help' for more information.\n",
            myname, c, myname);
}

void lrg_invalid_option_s(const char *s) {
    fprintf(stderr,
            "%s: invalid option -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, s, myname);
}

void lrg_invalid_range(const char *meta) {
    fprintf(stderr,
            "%s: invalid range -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, meta, myname);
}

void lrg_no_rewind(const char *meta) {
    fprintf(stderr,
            "%s: trying to rewind, but input file not seekable -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, meta, myname);
}

void lrg_eof_before(linenum_t target, linenum_t last) {
    fprintf(stderr, "%s: EOF before line %ld (last = %ld)\n", myname, target,
            last);
}

void lrg_alloc_fail(void) { fprintf(stderr, "%s: out of memory\n", myname); }

/* end of translatable strings, beginning of code */

void lrg_initline(struct lrg_lineout *line, char *buf, size_t size) {
    line->_buf = buf;
    line->_bsz = size;
    line->line_num = 0;
    line->newline = 1;
    line->eol = 1;
}

/* same as fgets, but returns ptr to newline or end of buffer
   (fgets returns ptr to beginning of buffer or NULL for eof/error) */
char *fgets2(char *str, size_t num, FILE *file) {
    char *end = fgets(str, num, file);
    if (end) {
        end = memchr(str, '\n', num);
        if (!end)
            end = str + num;
    }
    return end;
}

int lrg_nextline(struct lrg_lineout *line, FILE *file) {
    size_t bsz = line->_bsz;
    char *buf = line->_buf, *end;
    char *bufend = buf + bsz;

    if ((line->newline = line->eol))
        ++line->line_num;

    end = fgets2(buf, bsz, file);
    if (!end)
        return feof(file) ? 0 : -1;

    if ((line->eol = end < bufend))
        ++end;

    line->data = buf;
    line->size = end - buf;
    return 1;
}

/* 0 = ok, < 0 = fail, > 0 = end */
int lrg_next_linerange(const char **ptr, linenum_t *start, linenum_t *end) {
    const char *str = *ptr;
    char *endptr;
    linenum_t line0;

    if (!*str) /* end */
        return 1;
    line0 = strtoul(str, &endptr, 10);

    if (*endptr == '-') { /* 50-100... */
        linenum_t line1;
        str = endptr + 1;
        line1 = strtoul(str, &endptr, 10);
        if (str == endptr) {
            if (!line0)
                return -1;
            line1 = LINENUM_MAX;
        }

        if (*endptr == ',') { /* 50-100,200-500... */
            if (!line0 || !line1)
                return -1;

            *start = line0, *end = line1, *ptr = endptr + 1;
            return 0;

        } else if (!*endptr) { /* end */
            if (!line0 || !line1)
                return -1;
            *start = line0, *end = line1, *ptr = endptr;
            return 0;

        } else {
            return -1; /* invalid */
        }

    } else if (*endptr == '~') { /* 50~ or 50~10... */
        linenum_t linec;

        str = endptr + 1;
        linec = strtoul(str, &endptr, 10);
        if (str == endptr) { /* no M */
            if (!line0)
                return -1;
            linec = 3;
        }

        if (*endptr == ',') { /* 50~, ... */
            if (!line0 || !linec)
                return -1;

            *start = linec >= line0 ? 1 : line0 - linec;
            *end = line0 + linec, *ptr = endptr + 1;
            return 0;

        } else if (!*endptr) { /* end */
            if (!line0 || !linec)
                return -1;

            *start = linec >= line0 ? 1 : line0 - linec;
            *end = line0 + linec, *ptr = endptr;
            return 0;

        } else {
            return -1; /* invalid */
        }

    } else if (*endptr == ',') { /* 2,5... */
        if (!line0)
            return -1;

        *start = line0, *end = line0, *ptr = endptr + 1;
        return 0;

    } else if (!*endptr) { /* end */
        if (!line0)
            return -1;

        *start = line0, *end = line0, *ptr = endptr;
        return 0;

    } else {
        return -1; /* invalid */
    }
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
    int is_stdin = 1;
    int can_seek = 1;
    int status = 0;
    int returncode = 0;
    struct lrg_lineout line;
    struct lrg_linerange range;
    size_t i;

    if (!*fn || !strcmp(fn, STDIN_FILE)) {
        f = stdin;
    } else {
        is_stdin = 0;
        f = fopen(fn, "r");
        if (!f) {
            lrg_perror(fn, OPER_OPEN);
            return 1;
        }
    }

    lrg_initline(&line, tmpbuf, sizeof(tmpbuf));

    can_seek = ftell(f) >= 0;
    if (show_files)
        puts(fn); /* printf("%s\n", fn); */

    for (i = 0; i < linesbuf_n; ++i) {
        range = linesbuf[i];

        if (range.first <= line.line_num) {
            if (can_seek) {
                if (fseek(f, 0, SEEK_SET)) {
                    lrg_perror(fn, OPER_SEEK);
                    lrg_no_rewind(range.text);
                    returncode = 1;
                    goto unwind;
                }
                lrg_initline(&line, tmpbuf, sizeof(tmpbuf));
            } else {
                lrg_no_rewind(range.text);
                returncode = 1;
                goto unwind;
            }
        }

        while ((status = lrg_nextline(&line, f)) > 0) {
            if (line.line_num < range.first)
                continue;

            if (line.newline && show_lines)
                printf(" %7lu   ", line.line_num);
            fwrite(line.data, line.size, 1, stdout);
            if (line.eol && line.line_num == range.last)
                break;
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
                lrg_eof_before(line.line_num >= range.first ? range.last
                                                            : range.first,
                               line.line_num);
            returncode = 0;
            goto unwind;
        }
    }
unwind:
    if (!is_stdin)
        fclose(f);
    return returncode;
}

int main(int argc, char **argv) {
    int flag_ok = 1, i, fend = 0, inputLines = 0;
    myname = argv[0];

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
                    show_lines = 1;
                } else if (!strcmp(rest, "file-names")) {
                    show_files = 1;
                } else if (!strcmp(rest, "warn-eof")) {
                    warn_noline = 1;
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
                        show_lines = 1;
                    } else if (c == 'f') {
                        show_files = 1;
                    } else if (c == 'w') {
                        warn_noline = 1;
                    } else if (c == 'h') {
                        lrg_printhelp();
                        return EXITCODE_OK;
                    } else {
                        lrg_invalid_option_c(c);
                        return EXITCODE_USE;
                    }
                }
            }
        } else if (inputLines <= 0) {
            inputLines = i;
            if (lrg_parse_lines(argv[inputLines]))
                return EXITCODE_ERR;
        } else {
            argv[fend++] = argv[i];
        }
    }

    if (inputLines <= 0) {
        lrg_showusage();
        return EXITCODE_ERR;
    }

    if (!fend) { /* no input files */
        if (lrg_nextfile(STDIN_FILE))
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
