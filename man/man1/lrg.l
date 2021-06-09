.TH LRG 1
.SH NAME
lrg \- displays lines with the given line numbers from files
.SH SYNOPSIS
.B lrg
[\fI\,OPTION\/\fR]... \fI\,RANGE\fR[,\fI\,RANGE\/\fR]... [\fI\,FILE\/\fR]...
.SH DESCRIPTION
.B lrg
displays lines from files with the specified line numbers.

One or more FILEs can be given. If none are given,
.B lrg
will read from the standard input (stdin). This can also be explicitly specified
by giving - as a FILE. If multiple FILEs are given, each FILE is processed
one after the other, displaying all of the given ranges for a FILE before
proceeding to the next one.

A RANGE specifies the lines to be displayed. It can be in one of the following
three formats, with N and M being positive integers unless otherwise specified:

.RS
.IP \fBN\fP
A single line number. Line N will be displayed. Lines are numbered from one; the
first line in a file is line 1. 
.IP \fBN\-[M]\fP
An interval of line numbers. Ranges are considered inclusive; they will contain
both the first and the last line specified. If N is greater than M, no lines
will be displayed. If M is omitted, all lines of the file starting from line N
will be displayed.
.IP \fBN~[M]\fP
A line number and the vicinity thereof. M specifies the number of lines to
display before and after line N; it can be omitted in which case it is assumed
to be 3. In total, at most 2M+1 lines will be displayed. M may also be set to
zero, in which case this is equivalent to simply displaying line N.
.RE

If
.B lrg
tries to display a line when one doesn't exist (such as line number 100 in a
file that only has 50 lines), the current range will stop being processed, but
the file will still keep processing the rest of the ranges by default.

.SH OPTIONS
.TP
\fB\-e\fR, \fB\-\-error\-on\-eof\fR
treat an early end-of-file as an error. if an end-of-file is encountered, the
file will stop being processed. an exit status of 1 will be returned by the
command if an end-of-file was encountered for any of the specified files.
does not automatically print an error message; see option \fB\-w\fR
.TP
\fB\-f\fR, \fB\-\-file\-names\fR
display file name headers before every file
.TP
\fB\-l\fR, \fB\-\-line\-numbers\fR
display line numbers before every line
.TP
\fB\-w\fR, \fB\-\-warn\-eof\fR
display a warning if an end-of-file (EOF) occurs before the first or last line
in a given range is reached
.TP
\fB\-\-lps=\fI\,NUM\/\fR, \fB\-\-lines\-per\-second=\fI\,NUM\/\fR
display lines at a certain rate. the NUM represents lines per second and can
be fractional. NUM must be between 0.001 (1/1000) and 1000000 (one million).
a LPS setting of 1 will display approximately one line per second, waiting
one second between every printed line. LPS is only available if the feature is
compiled in
.TP
\fB\-?\fR, \fB\-\-help\fR
display this help message and exit
.TP
\fB\-\-version\fR
display the version information and exit
.SH EXIT STATUS
In POSIX-compatible environments, the command will return 0 when successful,
1 if an error occurs and 2 if there was incorrect usage. A premature end-of-file
condition is not considered an error (unless the option \fB\-e\fR,
\fB\-\-error\-on\-eof\fR is given).
.SH EXAMPLE
.TP
lrg -f 10 *.c
Output the line with the line number 10 from every C source code file in the
current directory. The file name will be displayed before every line.
.TP
lrg -l 1-10
Output the first ten lines from standard input (stdin), with a line number
before every displayed line.
.TP
lrg 105~5,250 /usr/share/dict/words
If installed, displays lines 100-110 (5 lines around line 105) and line 250 from
the word list.
.SH AUTHORS
Program and manual written by Sampo Hippeläinen (hisahi).
.SH REPORTING BUGS
.B lrg
is maintained at
https://github.com/hisahi/lrg

Pull requests and issues are welcome.
.SH SEE ALSO
.BR head(1),
.BR tail(1)
.PP