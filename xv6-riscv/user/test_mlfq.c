#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static int passed = 0;
static int failed = 0;

void
check(int cond, char *msg)
{
    if(cond){
        printf("  [PASS] %s\n", msg);
        passed++;
    } else {
        printf("  [FAIL] %s\n", msg);
        failed++;
    }
}

void
test_initial_priority(void)
{
    printf("\nTest 1: Initial Priority\n");

    struct procinfo info[64];
    int n = getprocs(info, 64);
    int found = 0;
    int mypid = getpid();

    for(int i = 0; i < n; i++){
        if(info[i].pid == mypid){
            found = 1;
            check(info[i].priority == 0, "Current process starts at priority 0 (highest)");
            break;
        }
    }
    check(found, "Found self in process list");
}

void
test_cpu_bound_demotion(void)
{
    printf("\nTest 2: CPU-Bound Demotion\n");

    int pid = fork();
    if(pid == 0){
        int start = uptime();
        while(uptime() - start < 5) {
            // spin to burn ticks
        }

        struct procinfo info[64];
        int n = getprocs(info, 64);
        int mypid = getpid();
        for(int i = 0; i < n; i++){
            if(info[i].pid == mypid){
                check(info[i].priority > 0, "CPU-bound process demoted below Q0");
                break;
            }
        }
        exit(0);
    }
    wait(0);
}

void
test_io_bound_stays_high(void)
{
    printf("\nTest 3: I/O-Bound Stays High Priority\n");

    int pid = fork();
    if(pid == 0){
        for(int i = 0; i < 5; i++)
            pause(2);

        struct procinfo info[64];
        int n = getprocs(info, 64);
        int mypid = getpid();
        for(int i = 0; i < n; i++){
            if(info[i].pid == mypid){
                check(info[i].priority == 0, "I/O-bound process at Q0 (boosted on wakeup)");
                break;
            }
        }
        exit(0);
    }
    wait(0);
}

void
test_setpriority(void)
{
    printf("\nTest 4: setpriority Syscall\n");

    int mypid = getpid();
    int ret = setpriority(mypid, 2);
    check(ret == 0, "setpriority(self, 2) returned 0");

    struct procinfo info[64];
    int n = getprocs(info, 64);
    for(int i = 0; i < n; i++){
        if(info[i].pid == mypid){
            check(info[i].priority == 2, "Priority changed to 2");
            break;
        }
    }

    setpriority(mypid, 0);

    ret = setpriority(mypid, 99);
    check(ret == -1, "setpriority with invalid priority returns -1");
}

void
test_starvation_boost(void)
{
    printf("\nTest 5: Starvation Prevention (Boost)\n");

    int pid = fork();
    if(pid == 0){
        setpriority(getpid(), 3);
        pause(120);

        struct procinfo info[64];
        int n = getprocs(info, 64);
        int mypid = getpid();
        for(int i = 0; i < n; i++){
            if(info[i].pid == mypid){
                check(info[i].priority == 0, "Process boosted back to Q0 after boost interval");
                break;
            }
        }
        exit(0);
    }
    wait(0);
}

int
main(int argc, char *argv[])
{
    printf("==============================\n");
    printf("   MLFQ Scheduler Test Suite\n");
    printf("==============================\n");

    test_initial_priority();
    test_cpu_bound_demotion();
    test_io_bound_stays_high();
    test_setpriority();
    test_starvation_boost();

    printf("\n--- Results: %d passed, %d failed ---\n\n", passed, failed);
    exit(failed > 0 ? 1 : 0);
}
