# lrg

This utility allows showing specific lines from files, with support for
line ranges.

# Building

lrg is distributed as a single standalone .c file that can be compiled
with any standard C compiler. The program by default assumes POSIX standards,
but does not use any POSIX-specific functions and is valid ANSI C (C89)
which can be compiled with any standard-compatible compiler.

# Usage

```
Usage: lrg [OPTION]... range[,range]... [input-file]...
Prints a specific range of lines from the given file.
Note that 'rewinding' might be impossible - once a line has been printed,
it is possible that only lines after it can be printed.
Line numbers start at 1.

  -h, --help
                 prints this message
  -f, --file-names
                 print file names before each file
  -l, --line-numbers
                 print line numbers before each line
  -w, --warn-eof
                 print a warning when a line is not found

Line range formats:
   N
                 line with line number N
   N-[M]
                 lines between lines N and M (inclusive)
                 if M not specified, goes until end of file
   N~[M]
                 line numbers around N
                 equivalent roughly to (N-M)-(N+M), therefore
                 displaying 2*M+1 lines
                 if M not specified, defaults to 3
```
