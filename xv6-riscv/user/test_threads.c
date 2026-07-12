#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static int passed = 0;
static int failed = 0;
static int print_lock = 0;

void
check(int cond, char *msg)
{
    futex(&print_lock, FUTEX_LOCK, 0);
    if(cond){
        printf("  [PASS] %s\n", msg);
        passed++;
    } else {
        printf("  [FAIL] %s\n", msg);
        failed++;
    }
    futex(&print_lock, FUTEX_UNLOCK, 0);
}

int t1_done = 0;

void
thread1_fn(int *arg)
{
    t1_done = 1;
    exit(0);
}

void
test_basic_thread(void)
{
    printf("\nTest 1: Basic Thread Create/Join\n");

    t1_done = 0;
    int tid = create_thread(thread1_fn, 0);
    check(tid > 0, "create_thread returned valid TID");
    join();
    check(t1_done == 1, "Thread executed and set flag");
}

#define NTHREADS 5
int thread_flags[NTHREADS];

void
multi_fn(int *arg)
{
    int idx = *arg;
    thread_flags[idx] = 1;
    exit(0);
}

void
test_multi_threads(void)
{
    printf("\nTest 2: Multiple Threads\n");

    for(int i = 0; i < NTHREADS; i++)
        thread_flags[i] = 0;

    for(int i = 0; i < NTHREADS; i++){
        create_thread(multi_fn, &i);
        pause(1);
    }

    for(int i = 0; i < NTHREADS; i++)
        join();

    int ok = 1;
    for(int i = 0; i < NTHREADS; i++){
        if(!thread_flags[i]) ok = 0;
    }
    check(ok, "All 5 threads executed successfully");
}

#define NINC 1000
int counter = 0;
int mutex_lock = 0;

void
inc_fn(int *arg)
{
    for(int i = 0; i < NINC; i++){
        futex(&mutex_lock, FUTEX_LOCK, 1);
        counter++;
        futex(&mutex_lock, FUTEX_UNLOCK, 1);
    }
    exit(0);
}

void
test_mutex(void)
{
    printf("\nTest 3: Mutex Correctness\n");

    counter = 0;
    mutex_lock = 0;
    int nthreads = 4;

    for(int i = 0; i < nthreads; i++)
        create_thread(inc_fn, 0);

    for(int i = 0; i < nthreads; i++)
        join();

    check(counter == nthreads * NINC,
          "4 threads x 1000 increments with mutex = 4000");
    if(counter != nthreads * NINC)
        printf("    (got %d, expected %d)\n", counter, nthreads * NINC);
}

#define BUF_SIZE 3
#define NPROD 6
int sem_buffer[BUF_SIZE];
int sem_in = 0, sem_out = 0;
int items_consumed = 0;
sem_t sem_mutex_pc, sem_empty, sem_full;
int pc_lock = 0;

void
sem_producer(int *arg)
{
    for(int i = 0; i < NPROD; i++){
        sem_down(&sem_empty);
        sem_down(&sem_mutex_pc);
        sem_buffer[sem_in] = i + 1;
        sem_in = (sem_in + 1) % BUF_SIZE;
        sem_up(&sem_mutex_pc);
        sem_up(&sem_full);
    }
    exit(0);
}

void
sem_consumer(int *arg)
{
    for(int i = 0; i < NPROD; i++){
        sem_down(&sem_full);
        sem_down(&sem_mutex_pc);
        int val = sem_buffer[sem_out];
        sem_out = (sem_out + 1) % BUF_SIZE;
        (void)val;
        futex(&pc_lock, FUTEX_LOCK, 0);
        items_consumed++;
        futex(&pc_lock, FUTEX_UNLOCK, 0);
        sem_up(&sem_mutex_pc);
        sem_up(&sem_empty);
    }
    exit(0);
}

void
test_semaphore(void)
{
    printf("\nTest 4: Semaphore Producer-Consumer\n");

    sem_in = sem_out = 0;
    items_consumed = 0;
    sem_mutex_pc.lock = sem_empty.lock = sem_full.lock = 0;
    pc_lock = 0;
    sem_init(&sem_mutex_pc, 1);
    sem_init(&sem_empty, BUF_SIZE);
    sem_init(&sem_full, 0);

    create_thread(sem_producer, 0);
    create_thread(sem_consumer, 0);

    join();
    join();

    check(items_consumed == NPROD,
          "Consumer received all 6 items from producer");
}

int spin_counter = 0;
int spin_lock_var = 0;

void
spin_fn(int *arg)
{
    for(int i = 0; i < 500; i++){
        futex(&spin_lock_var, FUTEX_LOCK, 0);
        spin_counter++;
        futex(&spin_lock_var, FUTEX_UNLOCK, 0);
    }
    exit(0);
}

void
test_spinlock(void)
{
    printf("\nTest 5: Spinlock\n");

    spin_counter = 0;
    spin_lock_var = 0;

    create_thread(spin_fn, 0);
    create_thread(spin_fn, 0);

    join();
    join();

    check(spin_counter == 1000, "2 threads x 500 increments with spinlock = 1000");
    if(spin_counter != 1000)
        printf("    (got %d, expected 1000)\n", spin_counter);
}

int
main(int argc, char *argv[])
{
    printf("==============================\n");
    printf("   Threading Test Suite\n");
    printf("==============================\n");

    test_basic_thread();
    test_multi_threads();
    test_mutex();
    test_semaphore();
    test_spinlock();

    printf("\n--- Results: %d passed, %d failed ---\n\n", passed, failed);
    exit(failed > 0 ? 1 : 0);
}
