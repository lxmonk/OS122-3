#include "types.h"
#include "user.h"

#define PAGE_SIZE 4096
/* A&T testing the swapping framework */

int main(int argc, char *argv[])
{
    int* pages[30];
    int i;

    printf(2, "starting myMemTest\n");

    for (i=0; i < 15; i++) {
        pages[i] = (int*) malloc(PAGE_SIZE);
        printf(2, "first loop. i=%d\n", i);
    }

    for (i = 0; i < 15; i++) {
        memset(pages[i], 2, PAGE_SIZE);
        printf(2, "second loop. i=%d\n", i);
    }

    return 0;
}
