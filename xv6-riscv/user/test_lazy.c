#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/vm.h"
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
test_lazy_sbrk(void)
{
    printf("\nTest 1: Lazy sbrk Returns Address\n");

    int free_before = getfreemem();
    char *p = sbrklazy(4096 * 20);
    int free_after = getfreemem();

    check(p != SBRK_ERROR, "sbrklazy(80KB) succeeded");
    check(free_before - free_after < 5, "Free memory barely changed (lazy -- no pages allocated)");
}

void
test_lazy_access(void)
{
    printf("\nTest 2: Lazy Access Triggers Allocation\n");

    char *p = sbrklazy(4096 * 5);
    check(p != SBRK_ERROR, "sbrklazy(20KB) succeeded");

    for(int i = 0; i < 5; i++){
        p[i * 4096] = 'X';
    }

    int ok = 1;
    for(int i = 0; i < 5; i++){
        if(p[i * 4096] != 'X')
            ok = 0;
    }
    check(ok, "All 5 lazily-allocated pages readable after write");
}

void
test_lazy_zeroed(void)
{
    printf("\nTest 3: Lazy Pages Are Zeroed\n");

    char *p = sbrklazy(4096);
    check(p != SBRK_ERROR, "sbrklazy(4KB) succeeded");

    int sum = 0;
    for(int i = 0; i < 4096; i++)
        sum += p[i];

    check(sum == 0, "Lazily-allocated page is zeroed");
}

void
test_eager_sbrk(void)
{
    printf("\nTest 4: Eager sbrk Still Works\n");

    int free_before = getfreemem();
    char *p = sbrk(4096 * 4);
    int free_after = getfreemem();

    check(p != SBRK_ERROR, "sbrk(16KB) succeeded");
    check(free_before - free_after >= 4, "Eager alloc consumed >= 4 pages");

    p[0] = 'A';
    p[4095] = 'B';
    check(p[0] == 'A' && p[4095] == 'B', "Eagerly allocated memory works");
}

int
main(int argc, char *argv[])
{
    printf("==============================\n");
    printf("   Lazy Allocation Test Suite\n");
    printf("==============================\n");

    test_lazy_sbrk();
    test_lazy_access();
    test_lazy_zeroed();
    test_eager_sbrk();

    printf("\n--- Results: %d passed, %d failed ---\n\n", passed, failed);
    exit(failed > 0 ? 1 : 0);
}
