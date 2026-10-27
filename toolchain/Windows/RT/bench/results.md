# GRIDE's runtime for Windows: benchmark results

Written by bench.ps1 -Record (docs\Runtime_Design.md 10.5): bench.c compiled once by c90 -O2, linked against
GRIDE's runtime and against Microsoft's static CRT, each test best of N runs, milliseconds. The rule from stage 2's
exit on: every row within 5 % of Microsoft's or faster.

## 2026-10-23 16:10 stage 1 (the prototype is branch grt-prototype c88573b)

Machine: Intel(R) Core(TM) i5-10400T CPU @ 2.00GHz. Best of 5 runs, milliseconds; ratio = GRIDE / Microsoft (below 1 is faster).

| Test | GRIDE's runtime | Microsoft's static CRT | Ratio | Same work | Prototype |
|---|---:|---:|---:|---|---:|
| printf-int | 419 | 249 | 1.68 | yes | 3798 |
| printf-float | 1190 | 903 | 1.32 | yes | 4514 |
| printf-string | 467 | 364 | 1.28 | yes | 3838 |
| printf-stdout | 401 | 885 | 0.45 | yes | 950 |
| sscanf-int | 215 | 186 | 1.16 | yes | 240 |
| sscanf-float | 465 | 399 | 1.17 | yes | 517 |
| fwrite | 26 | 25 | 1.04 | yes | 23 |
| fread | 26 | 26 | 1.00 | yes | 317 |
| fgets | 392 | 439 | 0.89 | yes | 940 |
| memcpy-16 | 204 | 200 | 1.02 | yes | 604 |
| memcpy-256 | 39 | 44 | 0.89 | yes | 46 |
| memcpy-4096 | 36 | 7 | 5.14 | yes | 50 |
| memcpy-65536 | 53 | 55 | 0.96 | yes | 35 |
| memcpy-1048576 | 52 | 53 | 0.98 | yes | 53 |
| memset-16 | 202 | 207 | 0.98 | yes | 490 |
| memset-256 | 23 | 19 | 1.21 | yes | 37 |
| memset-4096 | 9 | 6 | 1.50 | yes | 11 |
| memset-65536 | 13 | 13 | 1.00 | yes | 13 |
| memset-1048576 | 14 | 14 | 1.00 | yes | 14 |
| memmove-256 | 29 | 22 | 1.32 | yes | 284 |
| memmove-65536 | 21 | 14 | 1.50 | yes | 171 |
| strlen | 715 | 181 | 3.95 | yes | 696 |
| strcmp | 1425 | 268 | 5.32 | yes | 1425 |
| strchr | 4914 | 111 | 44.27 | yes | 4852 |
| malloc-small | 816 | 783 | 1.04 | yes | 840 |
| malloc-mixed | 1105 | 2222 | 0.50 | yes | 2303 |
| qsort | 287 | 252 | 1.14 | yes | 276 |
| sqrt | 47 | 63 | 0.75 | yes | 454 |
| sin | 408 | 113 | 3.61 | yes | 313 |
| exp | 233 | 116 | 2.01 | yes | 296 |
| log | 375 | 101 | 3.71 | yes | 363 |
| pow | 654 | 197 | 3.32 | yes | 649 |
| hello start-to-exit (ms, mean of 200) | 17.75 | 21.66 | 0.82 |  | 18.97 |
| hello size (bytes) | 51712 | 144896 | 0.36 |  | 50688 |

Checks on the rows above that came out slower than the prototype (the same machine, quiet; the noise between two
runs of one program is 5-25 %, Microsoft's own log moving from 101 to 75 ms):
- best of 9: sin 315 / 304 ms (GRIDE / prototype), log 268 / 249, pow 496 / 558, exp 211 / 215, qsort 229 / 225,
  fwrite 23 / 21, memcpy-65536 50 / 54, memset-256 22 / 38;
- best of 12, the two programs run alternately: log 268 / 270 ms, sin 304 / 306 ms.

So stage 1 is slower than the prototype on none of them; the math, str* and printf-to-file rows are stage 2's
(design 10.2-10.4: SSE2 str*, the printf fast paths, faster kernels), as is memcpy at 4 KB, where this program's two
static arrays alias by 4 KB and Microsoft's own time moves between 7 and 24 ms with the layout.

## 2026-10-27 the descriptors, streams and atexit list locked once a program has threads (design 9.6)

Machine: Intel(R) Core(TM) i5-10400T CPU @ 2.00GHz. Best of 5 runs, milliseconds; ratio = GRIDE / Microsoft. The
stream rows and malloc-small only, each runtime's two passes back to back: before is the runtime as it was (its
stream layer unchanged since stage 1), after the locked one. putc and getc are new rows: 64 MB a byte at a time, the
character functions' fast paths, which the program, with one thread, takes without a lock.

| Test | Before | Microsoft | Ratio | After | Microsoft | Ratio |
|---|---:|---:|---:|---:|---:|---:|
| printf-int | 436 | 265 | 1.65 | 417 | 254 | 1.64 |
| printf-float | 1241 | 914 | 1.36 | 1130 | 871 | 1.30 |
| printf-string | 537 | 459 | 1.17 | 502 | 439 | 1.14 |
| printf-stdout | 389 | 886 | 0.44 | 369 | 827 | 0.45 |
| sscanf-int | 220 | 181 | 1.22 | 205 | 180 | 1.14 |
| sscanf-float | 423 | 366 | 1.16 | 416 | 389 | 1.07 |
| fwrite | 21 | 21 | 1.00 | 23 | 22 | 1.05 |
| fread | 22 | 22 | 1.00 | 24 | 23 | 1.04 |
| fgets | 374 | 386 | 0.97 | 392 | 400 | 0.98 |
| putc | 462 | 2188 | 0.21 | 511 | 2374 | 0.22 |
| getc | 372 | 1858 | 0.20 | 404 | 1938 | 0.21 |
| malloc-small | 741 | 704 | 1.05 | 808 | 795 | 1.02 |
| hello start-to-exit (ms, mean of 200) | 7.75 | 8.83 | 0.88 | 8.32 | 9.49 | 0.88 |
| hello size (bytes) | 52224 | 144896 | 0.36 | 55296 | 144896 | 0.38 |

The machine slowed between the two passes (Microsoft's own putc 2188 to 2374 ms), so the I/O rows were run again,
before and after alternately, twice, best of 7: putc 546 and 490 ms before, 515 and 501 after; getc 398 and 397,
406 and 405; fwrite 24 and 24, 23 and 24; fread 24 and 23, 24 and 24. The one cost that stays is getc's 2 % (8 ms in
400), the fast path's test of the flag; no ratio to Microsoft's moved by more than the noise. hello is 3 KB larger:
the descriptors' and streams' lock calls.
