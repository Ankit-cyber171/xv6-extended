#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int free_pages = getfreemem();
    int free_kb = free_pages * 4;
    int total_kb = 128 * 1024;
    int used_kb = total_kb - free_kb;

    printf("\n=== xv6 Memory Info ===\n");
    printf("Total RAM:     %d KB (%d MB)\n", total_kb, total_kb / 1024);
    printf("Free memory:   %d KB (%d pages)\n", free_kb, free_pages);
    printf("Used memory:   %d KB\n", used_kb);
    printf("Usage:         %d%%\n", (used_kb * 100) / total_kb);
    printf("=======================\n");

    exit(0);
}
