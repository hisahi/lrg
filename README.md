# lrg

This utility allows showing specific lines from files, with support for line ranges.

# Usage

```
Usage: lrg [OPTION]... range[,range]... [input-file]...
Prints a specific range of lines from the given file.
Note that 'rewinding' might be impossible - once a line has been printed,
possibly only lines after it can be printed.
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
   N-M
                 lines between lines N and M (inclusive)
   N~[M]
                 line numbers around N
                 equivalent roughly to (N-M)-(N+M), therefore
                 displaying 2*M+1 lines
                 if M not specified, defaults to 3
```



