#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NBUF 2
#define NITEMS 4
int n_produce = -1, n_consume = -1;
/* shared buffer */
int buffer[NBUF];
int in = 0;
int out = 0;

/* semaphores */
sem_t mutex;
sem_t empty;
sem_t full;


void produce(int item) {
    // 1. Wait for at least one empty slot
    sem_down(&empty); 
    
    // 2. Lock the buffer for exclusive access
    sem_down(&mutex); 

    // 3. Insert the item
    buffer[in] = item;
    in = (in + 1) % NBUF;

    // 4. Release the buffer lock
    sem_up(&mutex); 
    
    // 5. Signal that there is a new full slot available
    sem_up(&full); 
}

int consume() {
    // 1. Wait for at least one item to be in the buffer
    sem_down(&full); 
    
    // 2. Lock the buffer for exclusive access
    sem_down(&mutex); 

    // 3. Remove the item
    int ret = buffer[out]; 
    out = (out + 1) % NBUF;

    // 4. Release the buffer lock
    sem_up(&mutex); 
    
    // 5. Signal that a new empty slot has opened up
    sem_up(&empty); 

    return ret;
}

int print_lock = 0;

void producer(int *arg)
{
    int tid = *arg;
    for (int i = 0; i < n_produce; i++) {
        produce(tid * 10 + i);
        futex(&print_lock , FUTEX_LOCK , 0);
        printf("P[%d]: produced %d\n", tid, tid * 10 + i);
        futex(&print_lock , FUTEX_UNLOCK , 0);
    }

    exit(0);
}

void consumer(int *arg)
{
    int tid = *arg;
    for (int i = 0; i < n_consume; i++) {
        int consumed = consume();
        futex(&print_lock , FUTEX_LOCK , 0);
        printf("C[%d]: consumed %d\n", tid, consumed);
        futex(&print_lock , FUTEX_UNLOCK , 0);
    }

    exit(0);
}


int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: prod-cons <mode>\n");
        printf("mode: 0 = 1P1C, 1 = 2P1C, 2 = 3P3C\n");
        exit(0);
    }

    int mode = atoi(argv[1]);

    n_produce = n_consume = NITEMS;

    // TODO
    /* initialize semaphores */
    mutex.lock = empty.lock = full.lock = 0;
    sem_init(&mutex, 1);
    sem_init(&empty, NBUF);
    sem_init(&full, 0);

    if (mode == 0) {
        int p = 0, c = 0;
        create_thread(producer, &p);
        create_thread(consumer, &c);

        for (int i = 0; i < 2; i++) {
            join();
        }
    }

    else if (mode == 1) {
	n_consume *= 2;
        int p0 = 0, p1 = 1, c = 0;
        create_thread(producer, &p0);
        create_thread(producer, &p1);
        create_thread(consumer, &c);

        for (int i = 0; i < 3; i++) {
            join();
        }
    }

    else if (mode == 2) {
        int p0 = 0, p1 = 1, p2 = 2, c0 = 0, c1 = 1, c2 = 2;
        create_thread(producer, &p0);
        create_thread(producer, &p1);
        create_thread(producer, &p2);
        create_thread(consumer, &c0);
        create_thread(consumer, &c1);
        create_thread(consumer, &c2);

        for (int i = 0; i < 6; i++) {
            join();
        }
    }

    else {
        printf("Invalid mode\n");
        exit(0);
    }

    exit(0);
}
