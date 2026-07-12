#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int ticks = uptime();
    int seconds = ticks / 10;
    int minutes = seconds / 60;
    int hours = minutes / 60;

    printf("\n=== xv6 Uptime ===\n");
    printf("Raw ticks:  %d\n", ticks);
    printf("Uptime:     ");
    if(hours > 0)
        printf("%dh ", hours);
    if(minutes > 0)
        printf("%dm ", minutes % 60);
    printf("%ds\n", seconds % 60);
    printf("==================\n");

    exit(0);
}
