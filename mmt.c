#include "types.h"
#include "user.h"

#define PAGE_SIZE 4096
#define MAXARG 10


/* A&T testing the swapping framework */

int main(int argc, char *argv[])
{
    int* pages[30];
    int i, j, pid;

    printf(2, "starting myMemTest\n");
    j = 123456;
    for (i=0; i < 8; i++) {
        printf(2, "first loop. i=%d\n", i);
        pages[i] = (int*) malloc(PAGE_SIZE);
        memset(pages[i], 'z', PAGE_SIZE);

    }

    for (i = 10; i >=  0; i--) {
        pages[i][10] = 10;
        pages[i][100] = 10;
        pages[i][500] = 10;
        pages[i][900] = 10;
        printf(2, "second loop. i=%d\n", i);
    }
    if ((pid = fork()) == 0){
        printf(2, "child: j=%d, pid=%d\n", j, getpid());
        for (i = 7; i >=  0; i--) {
            char str[20];
            pages[i][19] = 0;
            memmove(pages[i], str, 20);
            printf(2, "%s \n(should have z's)\n", str);

            pages[i][19] = '_';

            pages[i][20] = 10;
            pages[i][21] = 'A';

            pages[i][120] = 10;
            pages[i][121] = 'A';

            pages[i][520] = 10;
            pages[i][521] = 'A';

            pages[i][920] = 10;
            pages[i][921] = 'A';

            printf(2, "third (child) loop. i=%d\n", i);
        }
        exit();
    } else {
        wait();
    }
    printf(2, "myMemTest done,j=%d.\n",j);
    exit();
}
