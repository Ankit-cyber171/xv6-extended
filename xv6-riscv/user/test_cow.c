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
test_data_isolation(void)
{
    printf("\nTest 1: COW Data Isolation\n");

    int x = 42;
    int pid = fork();

    if(pid == 0){
        x = 99;
        check(x == 99, "Child sees its own modified value (99)");
        exit(0);
    } else {
        int status;
        wait(&status);
        check(x == 42, "Parent value unchanged after child write (42)");
    }
}

void
test_memory_efficiency(void)
{
    printf("\nTest 2: COW Memory Efficiency\n");

    int sz = 4096 * 10;
    char *mem = sbrk(sz);
    check(mem != SBRK_ERROR, "Allocated 40KB of memory");

    for(int i = 0; i < sz; i += 4096)
        mem[i] = 'A';

    int nchildren = 10;
    int success = 1;

    for(int i = 0; i < nchildren; i++){
        int pid = fork();
        if(pid < 0){
            success = 0;
            break;
        }
        if(pid == 0){
            char c = mem[0];
            (void)c;
            exit(0);
        }
    }

    for(int i = 0; i < nchildren; i++)
        wait(0);

    check(success, "Forked 10 children with shared 40KB -- no OOM");
}

void
test_multi_child_writes(void)
{
    printf("\nTest 3: COW Multi-Child Writes\n");

    int *shared = (int *)sbrk(4096);
    *shared = 100;

    for(int i = 0; i < 3; i++){
        int pid = fork();
        if(pid == 0){
            *shared = i * 10;
            check(*shared == i * 10, "Child sees correct local value");
            exit(0);
        }
    }

    for(int i = 0; i < 3; i++)
        wait(0);

    check(*shared == 100, "Parent still sees original value (100)");
}

int
main(int argc, char *argv[])
{
    printf("==============================\n");
    printf("   COW Fork Test Suite\n");
    printf("==============================\n");

    test_data_isolation();
    test_memory_efficiency();
    test_multi_child_writes();

    printf("\n--- Results: %d passed, %d failed ---\n\n", passed, failed);
    exit(failed > 0 ? 1 : 0);
}
