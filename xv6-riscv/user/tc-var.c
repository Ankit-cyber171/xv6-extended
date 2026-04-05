#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int VAR = 0;

void increment(int *thread_rank)
{
    int tmp = VAR;
    // pause(10);
    tmp++;
    // pause(10);
    VAR = tmp;
    exit(0);
}

int main(int argc, char *argv[])
{
    int N = 20;
    printf("Calling Process Print VAR value: %d, N: %d\n", VAR, N);

    for (int i = 0; i < N; i++) {
        create_thread(increment, &i);
    }

    for (int i = 0; i < N; i++) {
        join();
    }

    printf("All threads joined, VAR value: %d\n", VAR);
    exit(0);
}
