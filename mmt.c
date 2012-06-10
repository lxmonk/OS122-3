#include "types.h"
#include "user.h"

#define PAGE_SIZE 4096



/* A&T testing the swapping framework */

int main(int argc, char *argv[])
{
    int* pages[30];
    int i, j, pid;

    printf(2, "\t starting myMemTest\n");
    j = 123456;
    for (i=0; i <= 10; i++) {
        printf(2, "\t first loop. i=%d\n", i);
        pages[i] = (int*) malloc(PAGE_SIZE);
        memset(pages[i], 'z', PAGE_SIZE);
    }

    for (i = 10; i >=  0; i--) {
        pages[i][10] = 10;
        pages[i][100] = 10;
        pages[i][500] = 10;
        pages[i][900] = 10;
        printf(2, "\t second loop. i=%d\n", i);
    }

    if ((pid = fork()) == 0) {
        printf(2, "\t child: j=%d, pid=%d\n", j, getpid());
        /* pages[1][19] = 'V'; */
        for (i = 10; i >=  0; i--) {
            /* char str[20]; */
            /* pages[i][19] = 0; */
            /* memmove(str, pages[i], 20); */
            /* printf(2, "%s \n(should have z's)\n", str); */
            pages[i%4][i*i+3] = '_';
            pages[i][0] = 95;
            pages[i][20] = 10;
            pages[i][21] = 'A';

            pages[i][120] = 10;
            pages[i][121] = 'A';

            pages[i][520] = 10;
            pages[i][521] = 'A';

            /* pages[i][920] = 10; */
            /* pages[i][921] = 'A'; */

            printf(2, "\t third (child) loop. i=%d\n", i);
        }
        printf(2, "\t child exiting\n");
        exit();			/* child exit */
    } else {
        pid = fork();

        for (i=11; i<17; i++) {
            printf(2, "forks loop. pid = %d\n", getpid());
            pages[i] = malloc(PAGE_SIZE);
            memset(pages[i], 'M', PAGE_SIZE);
            pages[i%6][1] = 100;
        }
        if (pid != 0)
            wait();
        else {
            wait();
        }

    }
    printf(2, "\t myMemTest done,j=%d.\n",j);
    sleep(5);
    exit();
}
