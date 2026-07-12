#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static char *states[] = {
    "UNUSED",
    "USED",
    "SLEEP",
    "RUNBLE",
    "RUN",
    "ZOMBIE"
};

int
main(int argc, char *argv[])
{
    struct procinfo info[64];
    int n = getprocs(info, 64);

    if(n < 0){
        printf("ps: getprocs failed\n");
        exit(1);
    }

    printf("\n%-5s %-8s %-4s %-10s %s\n", "PID", "STATE", "PRI", "MEM(KB)", "NAME");
    printf("---   -----    ---  -------    ----\n");

    for(int i = 0; i < n; i++){
        char *state = "???";
        if(info[i].state >= 0 && info[i].state <= 5)
            state = states[info[i].state];
        printf("%-5d %-8s Q%-3d %-10d %s\n",
               info[i].pid,
               state,
               info[i].priority,
               (int)(info[i].sz / 1024),
               info[i].name);
    }

    printf("\nTotal: %d processes\n", n);
    exit(0);
}
