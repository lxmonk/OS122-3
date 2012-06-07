#include "types.h"
#include "user.h"

#define PAGE_SIZE 4096
#define MAXARG 10


/* A&T testing the swapping framework */

int main(int argc, char *argv[])
{
    int* pages[30];
    int i,j;

    printf(2, "starting myMemTest\n");
    j = 123456;
    for (i=0; i < 8; i++) {
        printf(2, "first loop. i=%d\n", i);
        pages[i] = (int*) malloc(PAGE_SIZE);
        memset(pages[i], 'z', PAGE_SIZE);

    }

    for (i = 0; i <  8; i++) {
        pages[i][10] = 10;
        pages[i][100] = 10;
        pages[i][500] = 10;
        pages[i][900] = 10;
        printf(2, "second loop. i=%d\n", i);
    }

    printf(2, "myMemTest done,j=%d.\n",j);
    exit();
}
