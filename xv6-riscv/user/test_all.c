#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
run_test(char *name)
{
    printf("\n========================================\n");
    printf("  Running: %s\n", name);
    printf("========================================\n");

    int pid = fork();
    if(pid == 0){
        char *argv[] = { name, 0 };
        exec(name, argv);
        printf("exec %s failed\n", name);
        exit(1);
    }

    int status;
    wait(&status);
    if(status == 0)
        printf(">>> %s: ALL PASSED <<<\n", name);
    else
        printf(">>> %s: SOME FAILED <<<\n", name);
}

int
main(int argc, char *argv[])
{
    printf("\n##############################################\n");
    printf("#    xv6-extended Full Test Suite             #\n");
    printf("##############################################\n");

    run_test("test_cow");
    run_test("test_lazy");
    run_test("test_threads");
    run_test("test_mlfq");

    printf("\n##############################################\n");
    printf("#    All test suites completed.               #\n");
    printf("##############################################\n\n");

    exit(0);
}
