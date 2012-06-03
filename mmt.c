#include "types.h"
#include "user.h"

#define PAGE_SIZE 4096
#define MAXARG 10
#define T_A_DEBUG 0

/* A&T testing the swapping framework */

int main(int argc, char *argv[])
{
    static int* pages[30];
    int i,j;
    int sum = 0;

    printf(2, "starting myMemTest\n");
    j = 0;
    for (i=0; i < 30; i++) {
        /* for (j=0; j < PAGE_SIZE/40; j++) { */
            /* pages[i][j] = 0x832974; */
        pages[i] = (int*) malloc(PAGE_SIZE);
        memset(pages[i], 2, PAGE_SIZE);
        /* } */
        printf(2, "first loop. i=%d\n", i);
    }

    for (i = 0; i < 30; i++) {
        /* for (j=0; j < PAGE_SIZE/4; j++) { */
        /* } */
        j = 0;
        pages[i][j] = 0;
        sum += 3 + pages[i][j];
        printf(2, "second loop. i=%d, sum=%d\n", i, sum);
    }

    /* int pid, fd; */

    /* unlink("bigarg-ok"); */
    /* pid = fork(); */
    /* if(pid == 0){ */
    /*     static char *args[MAXARG]; */
    /*     int i; */
    /*     for(i = 0; i < MAXARG-1; i++) */
    /*         args[i] = "bigargs test: failed\n                                                                                                                                                                                                       "; */
    /*     DEBUG_PRINT(3, "here", 999); */

    /*     args[MAXARG-1] = 0; */
    /*     DEBUG_PRINT(3, "here", 999); */

    /*     printf(2, "bigarg test\n"); */
    /*     exec("echo", args); */
    /*     DEBUG_PRINT(3, "here", 999); */

    /*     printf(2, "bigarg test ok\n"); */
    /*     /\* fd = open("bigarg-ok", O_CREATE); *\/ */
    /*     /\* close(fd); *\/ */
    /*     exit(); */
    /* } else if(pid < 0){ */
    /*     printf(2, "bigargtest: fork failed\n"); */
    /*     exit(); */
    /* } */
    /* wait(); */
    /* fd = open("bigarg-ok", 0); */
    /* if(fd < 0){ */
    /*     printf(2, "bigarg test failed!\n"); */
    /*     exit(); */
    /* } */
    /* close(fd); */
    /* unlink("bigarg-ok"); */


    printf(2, "myMemTest done.\n");
    exit();
}
