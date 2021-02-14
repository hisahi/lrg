/*

Line RanGe (LRG)
A tool that allows displaying specific lines
of files (or around a specific line)
with possible line number display

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

#define USE_POSIX_EXIT_CODES 1
#define BUFSIZE 404
char tmpbuf[BUFSIZE];

struct lrg_lineout {
    unsigned long line_num;
    char *data;
    size_t size;
    char *_buf;
    size_t _bsz;
    char eol;
    char newline;
};

static const char *STDIN_FILE = "-";
char *myname;
int show_lines = 0, show_files = 0, warn_noline = 0;

void print_help(char *name) {
    fprintf(stderr,
            "Usage: %s [OPTION]... range[,range]... "
            "[input-file]...\n"
            "Prints a specific range of lines from the given file.\n"
            "Note that 'rewinding' might be impossible - once a line "
            "has been printed,\nit is possible that only lines after it"
            "can be printed.\n"
            "Line numbers start at 1.\n\n"
            "  -h, --help\n"
            "                 prints this message\n"
            "  -f, --file-names\n"
            "                 print file names before each file\n"
            "  -l, --line-numbers\n"
            "                 print line numbers before each line\n",
            name);
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

void lrg_perror(const char *meta) {
    fprintf(stderr, "%s: %s: %s\n", myname, meta, strerror(errno));
}

void lrg_invalid_range(const char *meta) {
    fprintf(stderr,
            "%s: invalid range -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, meta, myname);
}

void lrg_no_rewind(const char *meta) {
    fprintf(stderr,
            "%s: trying to rewind in range -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, meta, myname);
}

void lrg_initline(struct lrg_lineout *line, char *buf, size_t size) {
    line->_buf = buf;
    line->_bsz = size;
    line->line_num = 0;
    line->newline = 1;
    line->eol = 1;
}

int lrg_nextline(struct lrg_lineout *line, FILE *file) {
    char *buf = line->_buf, *end, *bufend, eol;
    size_t bsz = line->_bsz;

    bufend = buf + bsz;
    end = fgets(buf, bsz, file);
    if (!end)
        return feof(file) ? 0 : -1;

    if ((line->newline = line->eol))
        ++line->line_num;

    end = memchr(end, '\n', bufend - end);
    if (end)
        eol = 1, ++end;
    else
        eol = 0, end = bufend;

    line->eol = eol;
    line->data = buf;
    line->size = end - buf;
    return 1;
}

/* 0 = ok, < 0 = fail, > 0 = end */
int lrg_next_linerange(const char **ptr, long *start, long *end) {
    const char *str = *ptr;
    char *endptr;
    unsigned long line0;

    if (!*str) /* end */
        return 1;
    line0 = strtoul(str, &endptr, 10);

    if (*endptr == '-') { /* 50-100... */
        unsigned long line1;
        str = endptr + 1;
        line1 = strtoul(str, &endptr, 10);
        if (str == endptr) {
            if (!line0)
                return -1;
            line1 = LONG_MAX;
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
        unsigned long linec;

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

            *start = line0 - linec, *end = line0 + linec, *ptr = endptr + 1;
            return 0;

        } else if (!*endptr) { /* end */
            if (!line0 || !linec)
                return -1;

            *start = line0 - linec, *end = line0 + linec, *ptr = endptr;
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

int lrg_nextfile(const char *fn, const char *ln) {
    FILE *f;
    int is_stdin = 1;
    int can_seek = 1;
    int pl = 0, status = 0;
    long l0 = 0, l1 = 0;
    const char *oldptr = ln;
    int returncode = 0;
    struct lrg_lineout line;

    if (!*fn || !strcmp(fn, STDIN_FILE)) {
        f = stdin;
    } else {
        is_stdin = 0;
        f = fopen(fn, "r");
        if (!f) {
            lrg_perror(fn);
            return 1;
        }
    }

    lrg_initline(&line, tmpbuf, BUFSIZE);

    can_seek = ftell(f) >= 0;
    if (show_files)
        printf("%s\n", fn);

    while ((pl = lrg_next_linerange(&ln, &l0, &l1)) <= 0) {
        if (pl < 0) {
            lrg_invalid_range(oldptr);
            returncode = 1;
            goto unwind;
        }

        if (l1 < l0) {
            /* swap l0 and l1 */
            int tmp = l1;
            l1 = l0;
            l0 = tmp;
        }

        if (l0 < 1)
            l0 = 1;

        if (l0 <= line.line_num) {
            if (can_seek) {
                if (fseek(f, 0, SEEK_SET)) {
                    fprintf(stderr, "cannot seek: %s\n", strerror(errno));
                    lrg_no_rewind(oldptr);
                    returncode = 1;
                    goto unwind;
                }
                lrg_initline(&line, tmpbuf, BUFSIZE);
            } else {
                lrg_no_rewind(oldptr);
                returncode = 1;
                goto unwind;
            }
        }

        while ((status = lrg_nextline(&line, f)) > 0) {
            if (line.line_num < l0)
                continue;

            if (line.newline && show_lines)
                printf("%7ld   ", line.line_num);
            fwrite(line.data, line.size, 1, stdout);
            if (line.eol && line.line_num == l1)
                break;
        }

        if (status < 0) {
            /* file error */
            lrg_perror(fn);
            returncode = 1;
            goto unwind;
        }
        if (status == 0 && l1 < LONG_MAX) {
            /* reached the end of the file before l0 or l1 */
            if (warn_noline)
                fprintf(stderr, "%s: EOF before line %ld (last = %ld)\n",
                        myname, line.line_num >= l0 ? l1 : l0, line.line_num);
            returncode = 0;
            goto unwind;
        }

        oldptr = ln;
    }
unwind:
    if (!is_stdin)
        fclose(f);
    return returncode;
}

int main(int argc, char **argv) {
    int flag_ok = 1, i;
    int inputLines = 0, useStdin = 1;
    myname = argv[0];

    for (i = 1; i < argc; ++i) {
        if (flag_ok && argv[i][0] == '-' && argv[i][1]) {
            if (argv[i][1] == '-') {
                /* -- = long flag */
                if (!argv[i][2]) {
                    flag_ok = 0;
                    continue;
                } else if (!strcmp(argv[i] + 2, "line-numbers")) {
                    show_lines = 1;
                } else if (!strcmp(argv[i] + 2, "file-names")) {
                    show_files = 1;
                } else if (!strcmp(argv[i] + 2, "warn-eof")) {
                    warn_noline = 1;
                } else if (!strcmp(argv[i] + 2, "help")) {
                    print_help(myname);
#if USE_POSIX_EXIT_CODES
                    return 0;
#else
                    return EXIT_SUCCESS;
#endif
                } else {
                    fprintf(stderr,
                            "%s: invalid option -- '%s'\n"
                            "Try '%s --help' for more information.\n",
                            myname, argv[i] + 2, myname);
#if USE_POSIX_EXIT_CODES
                    return 2;
#else
                    return EXIT_FAILURE;
#endif
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
                        print_help(myname);
#if USE_POSIX_EXIT_CODES
                        return 0;
#else
                        return EXIT_SUCCESS;
#endif
                    } else {
                        fprintf(stderr,
                                "%s: invalid option -- '%c'\n"
                                "Try '%s --help' for more information.\n",
                                myname, c, myname);
#if USE_POSIX_EXIT_CODES
                        return 2;
#else
                        return EXIT_FAILURE;
#endif
                    }
                }
            }
        } else if (inputLines <= 0) {
            inputLines = i;
        } else if (useStdin) {
            useStdin = 0;
        }
    }

    if (inputLines <= 0) {
        fprintf(stderr,
                "Usage: %s [OPTION]... range"
                "[,range]... [input-file]...\n"
                "Try '%s --help' for more information.\n",
                argv[0], argv[0]);
#if USE_POSIX_EXIT_CODES
        return 1;
#else
        return EXIT_FAILURE;
#endif
    }

    if (useStdin) {
        if (lrg_nextfile(STDIN_FILE, argv[inputLines])) {
#if USE_POSIX_EXIT_CODES
            return 1;
#else
            return EXIT_FAILURE;
#endif
        }
    } else {
        flag_ok = 1;
        for (i = 1; i < argc; ++i) {
            if (flag_ok && argv[i][0] == '-' && argv[i][1]) {
                if (!strcmp(argv[i], "--")) { /* --, end of flags */
                    flag_ok = 0;
                    continue;
                }
            } else if (i > inputLines) {
                if (lrg_nextfile(argv[i], argv[inputLines])) {
#if USE_POSIX_EXIT_CODES
                    return 1;
#else
                    return EXIT_FAILURE;
#endif
                }
            }
        }
    }
#if USE_POSIX_EXIT_CODES
    return 0;
#else
    return EXIT_SUCCESS;
#endif
}
