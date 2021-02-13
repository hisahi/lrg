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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *STDIN_FILE = "-";
char *myname;
int show_lines = 0, show_files = 0, warn_noline = 0;

void print_help(char *name) {
    fprintf(stderr,
            "Usage: %s [OPTION]... range[,range]... "
            "[input-file]...\n"
            "Prints a specific range of lines from the given file.\n"
            "Note that 'rewinding' might be impossible - once a line "
            "has been printed,\npossibly only lines after it can be "
            "printed.\n"
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
            "   N-M\n"
            "                 lines between lines N and M (inclusive)\n"
            "   N~[M]\n"
            "                 line numbers around N\n"
            "                 equivalent roughly to (N-M)-(N+M), therefore\n"
            "                 displaying 2*M+1 lines\n"
            "                 if M not specified, defaults to 3\n\n");
}

void lrg_perror(char *meta) {
    fprintf(stderr, "%s: %s: %s\n", myname, meta, strerror(errno));
}

void lrg_invalid_range(char *meta) {
    fprintf(stderr,
            "%s: invalid range -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, meta, myname);
}

void lrg_no_rewind(char *meta) {
    fprintf(stderr,
            "%s: trying to rewind in range -- '%s'\n"
            "Try '%s --help' for more information.\n",
            myname, meta, myname);
}

int lineread(char **ptr, size_t *n, size_t *cnt, FILE *stream) {
    size_t capacity = *n;
    size_t read = 0;
    char *buf = *ptr;
    int c;

    for (;;) {
        c = getc(stream);
        if (read == 0 && c == EOF)
            return -1;
        if (++read > capacity) {
            buf = realloc(buf, capacity += 128);
            if (!buf)
                abort();
            *ptr = buf;
        }
        buf[read++] = c;
        if (c == '\n' || (read + 1) == 0)
            break;
    }

    if (cnt)
        *cnt = read;
    return read > 0 ? 1 : 0;
}

/* 0 = ok, < 0 = fail, > 0 = end */
int process_line_range(char **ptr, long *start, long *end, int *mode) {
    char *str = *ptr, *endptr;
    unsigned long line0;

    if (!*str) /* end */
        return 1;
    *mode = 0;
    line0 = strtoul(str, &endptr, 10);
    if (*endptr == '-') { /* 50-100... */
        unsigned long line1;
        str = endptr + 1;
        line1 = strtoul(str, &endptr, 10);
        if (*endptr == ',') { /* 50-100,200-500... */
            if (line0 < 1 || line1 < 1)
                return -1;
            *start = line0;
            *end = line1;
            *ptr = endptr + 1;
            return 0;
        } else if (!*endptr) { /* end */
            if (line0 < 1 || line1 < 1)
                return -1;
            *start = line0;
            *end = line1;
            *ptr = endptr;
            return 0;
        } else {
            return -1; /* invalid */
        }
    } else if (*endptr == '~') { /* 50~ or 50~10... */
        unsigned long line1;
        *mode = 1;
        str = endptr + 1;
        if (!*str) { /* no M */
            if (line0 < 1)
                return -1;
            *start = line0;
            *end = 3;
            *ptr = endptr + 1;
            return 0;
        }
        line1 = strtoul(str, &endptr, 10);
        if (*endptr == ',') { /* 50~, ... */
            if (line0 < 1 || line1 < 1)
                return -1;
            *start = line0;
            *end = line1;
            *ptr = endptr + 1;
            return 0;
        } else if (!*endptr) { /* end */
            if (line0 < 1 || line1 < 1)
                return -1;
            *start = line0;
            *end = line1;
            *ptr = endptr;
            return 0;
        } else {
            return -1; /* invalid */
        }
    } else if (*endptr == ',') { /* 2,5... */
        if (line0 < 1)
            return -1;
        *start = line0;
        *end = line0;
        *ptr = endptr + 1;
        return 0;
    } else if (!*endptr) { /* end */
        if (line0 < 1)
            return -1;
        *start = line0;
        *end = line0;
        *ptr = endptr;
        return 0;
    } else {
        return -1; /* invalid */
    }
}

int process_file(char *fn, char *ln) {
    FILE *f;
    int is_stdin = 1;
    int can_seek = 1;
    int last_line = 1;
    int pl = 0;
    long l0 = 0, l1 = 0, i;
    int m = 0;
    char *oldptr = ln;
    char *line = NULL;
    size_t linebufsz = 0;
    size_t linelen = 0;

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
    if (ftell(f) < 0)
        can_seek = 0;
    if (show_files)
        printf("%s\n", fn);
    while ((pl = process_line_range(&ln, &l0, &l1, &m)) <= 0) {
        if (pl < 0) {
            lrg_invalid_range(oldptr);
            if (!is_stdin)
                fclose(f);
            return 1;
        }
        if (m == 0 && l1 < l0) {
            /* swap l0 and l1 */
            int tmp = l1;
            l1 = l0;
            l0 = tmp;
        }
        if (m == 1) {
            int ol0 = l0;
            l0 = ol0 - l1;
            l1 = ol0 + l1;
        }
        if (l0 < 1)
            l0 = 1;
        if (l1 < last_line) {
            if (can_seek) {
                last_line = 1;
                if (fseek(f, 0, SEEK_SET)) {
                    fprintf(stderr, "cannot seek: %s\n", strerror(errno));
                    lrg_no_rewind(oldptr);
                    if (!is_stdin)
                        fclose(f);
                    return 1;
                }
            } else {
                lrg_no_rewind(oldptr);
                if (!is_stdin)
                    fclose(f);
                return 1;
            }
        }
        if (l0 < last_line && !can_seek)
            l0 = last_line;
        for (i = last_line; i < l0; ++i) {
            if (lineread(&line, &linebufsz, &linelen, f) < 0) {
                /* reached the end of the file before l0 */
                if (warn_noline)
                    fprintf(stderr,
                            "%s: EOF before line "
                            "%ld (%ld)\n",
                            myname, l0, i);
                if (line)
                    free(line);
                if (!is_stdin)
                    fclose(f);
                return 0;
            }
        }
        for (i = l0; i <= l1; ++i) {
            if (lineread(&line, &linebufsz, &linelen, f) < 0) {
                /* reached the end of the file before l1 */
                if (warn_noline)
                    fprintf(stderr,
                            "%s: EOF before "
                            "line %ld (%ld)\n",
                            myname, l1, i);
                if (line)
                    free(line);
                if (!is_stdin)
                    fclose(f);
                return 0;
            }
            if (show_lines)
                printf("%8ld   ", i);
            fwrite(line, 1, linelen, stdout);
        }
        last_line = l1 + 1;
        oldptr = ln;
    }
    if (line)
        free(line);
    if (!is_stdin)
        fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    int flag_ok = 1, arg_counter = 0, i, j;
    int inputLines;
    int inputFiles[256];
    myname = argv[0];
    for (i = 1; i < argc; ++i) {
        int sl = strlen(argv[i]);
        if (flag_ok && argv[i][0] == '-' && sl > 1) {
            if (!strcmp(argv[i], "--")) { /* --, end of flags */
                flag_ok = 0;
                continue;
            } else if (sl > 2 && argv[i][1] == '-') {
                /* -- = long flag */
                if (!strcmp(argv[i], "--help")) {
                    print_help(argv[0]);
                    return 0;
                } else if (!strcmp(argv[i], "--line-numbers")) {
                    show_lines = 1;
                } else if (!strcmp(argv[i], "--file-names")) {
                    show_files = 1;
                } else if (!strcmp(argv[i], "--warn-eof")) {
                    warn_noline = 1;
                } else {
                    fprintf(stderr,
                            "%s: invalid option -- '%s'\n"
                            "Try '%s --help' for more information.\n",
                            argv[0], argv[i] + 2, argv[0]);
                    return 2;
                }
            } else {
                for (j = 1; j < sl; ++j) {
                    char c = argv[i][j];
                    if (c == 'l') {
                        show_lines = 1;
                    } else if (c == 'f') {
                        show_files = 1;
                    } else if (c == 'w') {
                        warn_noline = 1;
                    } else if (c == 'h') {
                        print_help(argv[0]);
                        return 0;
                    } else {
                        fprintf(stderr,
                                "%s: invalid option -- '%c'\n"
                                "Try '%s --help' for more information.\n",
                                argv[0], argv[i][1], argv[0]);
                        return 2;
                    }
                }
            }
        } else {
            ++arg_counter;
            if (arg_counter > 1) {
                inputFiles[arg_counter - 2] = i;
            } else {
                inputLines = i;
            }
            if (arg_counter > 256) {
                fprintf(stderr,
                        "Usage: %s [OPTION]... range"
                        "[,range]... [input-file]...\n"
                        "Try '%s --help' for more information.\n",
                        argv[0], argv[0]);
                return 2;
            }
        }
    }
    if (arg_counter < 1) {
        fprintf(stderr,
                "Usage: %s [OPTION]... range"
                "[,range]... [input-file]...\n"
                "Try '%s --help' for more information.\n",
                argv[0], argv[0]);
        return 2;
    }
    if (arg_counter < 2) {
        if (process_file((char *)STDIN_FILE, argv[inputLines])) {
            return 2;
        }
    } else {
        int fi = 0;
        inputFiles[arg_counter - 1] = 0;
        /* while still input files left... (inputFiles[fi] != 0) */
        while (inputFiles[fi]) {
            if (process_file(argv[inputFiles[fi]], argv[inputLines])) {
                return 2;
            }
            ++fi;
        }
    }
    return 0;
}
