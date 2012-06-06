#include "types.h"
#include "user.h"

#define PAGE_SIZE 4096
#define MAXARG 10


/* A&T testing the swapping framework */

int main(int argc, char *argv[])
{
    int* pages[30];
    int i,j;
    //int sum = 0;

    printf(2, "starting myMemTest\n");
    j = 0;
    for (i=0; i < 20; i++) {
        printf(2, "first loop. i=%d\n", i);

        pages[i] = (int*) malloc(PAGE_SIZE);
        memset(pages[i], 'z', PAGE_SIZE);

    }

    /*    for (i = 0; i <  1; i++) { */
    /*     /\* for (j=0; j < PAGE_SIZE/4; j++) { *\/ */
    /*     /\* } *\/ */
    /*     j = 0; */
    /*     pages[i][j] = 0; */
    /*     sum += 3 + pages[i][j]; */
    /*     printf(2, "second loop. i=%d, sum=%d\n", i, sum); */
    /* } */

    printf(2, "myMemTest done.\n");
    exit();
}
