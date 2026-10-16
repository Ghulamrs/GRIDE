#include <stdio.h>

int greet(int n);

int main(void)
{
    int n = 4;
    int g = greet(n);
    printf("greet(%d) is %d\n", n, g);
    printf("c90 demo done\n");
    return 3;
}
