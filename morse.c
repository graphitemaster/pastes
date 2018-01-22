#include <stdio.h>
int main() {
    char *T="-��a�C�0%�!�KRp4xi-";
    char n = 0, d = 0, l = 0, *e;
    for (char c = getchar(); c != EOF; c = getchar()) {
        if (c == '\n') {
            for (e = T; *e; e++)
                if (*e == (d << 4) | l)
                    break;
            putchar('A' + e - T);
            n = 0, d = 0, l = 0;
        }
        else if (c == '.') d |= (1 << n++);
        else if (c == '-') l |= (1 << n++);
    }
}
