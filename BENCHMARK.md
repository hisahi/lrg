
# Benchmark

This test involved printing the twenty-five-millionth line in a file. First
are the "usual" solutions up to this point, and then lrg for comparison, with
reasonable optimization levels, as well as unoptimized for comparison:

| speedup x (> = faster)                                               | block.txt | short.txt | long.txt | random.txt |
|----------------------------------------------------------------------|-----------|-----------|----------|------------|
| **Baseline**: `head -25000000 \| tail -1`                            | _1.00_    | _1.00_    | _1.00_   | _1.00_     |
| `awk 'NR == 25000000 {print; exit}'`                                 | 0.91      | 0.22      | 1.13     | 0.57       |
| `sed '25000000q;d'`                                                  | 2.50      | 0.58      | 2.57     | 1.40       |
| `tail -n+25000000 \| head -1`                                        | 7.18      | **3.43**  | 6.42     | 3.83       |
| `clang -O0`: `lrg 25000000`                                          | 7.23      | 2.40      | 7.01     | 3.71       |
| `clang -O2`: `lrg 25000000`                                          | 8.20      | 3.30      | **8.27** | **4.24**   |
| `clang -O3`: `lrg 25000000`                                          | **8.20**  | 3.32      | **8.13** | 4.20       |
| `clang -O3 -march=native`: `lrg 25000000`                            | **8.21**  | **3.36**  | **8.23** | **4.27**   |
| `clang -O3 -march=native -DLRG_HAVE_TURBO_MEMCNT=1`: `lrg 25000000`  | **8.73**  | **18.1**  | 8.02     | **8.20**   |

(top 3 of each file highlighted)

Test details: `block.txt` only had lines exactly 80 characters long,
`short.txt` contained short lines (1-10 characters in length, uniform),
`long.txt` contained long lines (100-200 characters in length, uniform), and
`random.txt` contained lines of all lengths (1-200 characters in length, squared
distribution with bias towards shorter lines). Each file was over thirty million
lines long. Five samples were recorded for every tool/file combination, and the
three middle results were averaged.

Test hardware: Intel Core i5-7300HQ @ 2.50 GHz (up to AVX-2), NVMe SSD

Test OS: Ubuntu 20.04 LTS, Linux kernel 4.19.128

Test software: GNU coreutils 8.30, GNU Awk 5.0.1, GNU sed 4.7

Test compiler: clang 10.0.0-4ubuntu1

Test version: lrg, [commit 94c74d7cb07552663900a29a599c75beddadf0e3](https://github.com/hisahi/lrg/tree/94c74d7cb07552663900a29a599c75beddadf0e3)

For lrg `-O3`, the preprocessor define `-DLRG_TRY_FAST_MEMCNT=0` was used
(in modern lrg, this flag does not exist anymore).
