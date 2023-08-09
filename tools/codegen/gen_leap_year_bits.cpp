#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_leap_year(long long year) {
    if (year % 4) return 0;         /* A year not divisible by 4 is not leap. */
    else if (year % 100) return 1;  /* If div by 4 and not 100 is surely leap. */
    else if (year % 400) return 0;  /* If div by 100 *and* 400 is not leap. */
    else return 1;                  /* If div by 100 and not by 400 is leap. */
}

int main(int argc, char** argv) {
    long long beg = 1900, end = 2040;
    auto prog = argv[0];
    if (argc == 2) {
        fprintf(stderr, "usage %s [beg end]\n", prog);
        return 1;
    }
    if (argc >= 3) {
        beg = strtoll(argv[1], NULL, 10);
        end = strtoll(argv[2], NULL, 10);
    }
    for (long long year = beg; year < end;) {
        for (int col = 0; col < 5; col++) {
            long long ubits = 0;
            for (int i = 0; i < 64; ++i) {
                if (is_leap_year(year++)) {
                    ubits |= 1ull << i;
                }
            }
            printf("0x%016llX, ", ubits);
        }
        printf("\n");
    }
    printf("// rank1 ------------------------\n");
    long long rank = 0;
    for (long long year = beg; year < end;) {
        for (int col = 0; col < 5; col++) {
            for (int i = 0; i < 64; ++i) {
                if (is_leap_year(year++)) {
                    rank++;
                }
            }
            printf("0x%04llX, ", rank);
        }
        printf("\n");
    }
    return 0;
}

