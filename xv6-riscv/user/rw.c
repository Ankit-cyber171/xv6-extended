#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NREADERS 3
#define NWRITERS 2
#define NOPS 5

// readers only read shared_data
// writers increment shared_data
int shared_data = 0;

/* synchronization variables */
sem_t mutex;        // protects read_count
sem_t rw_mutex;     // controls access to shared resource

int read_count = 0;

int print_lock = 0;


// TODO: reads shared_data and prints the value
// acquire reader-lock before accessing shared_data
void read_shared_data(int tid) {
    // need print lock here->because multiple readers at one time 
    sem_down(&mutex);
    read_count++;
    if(read_count == 1){
        sem_down(&rw_mutex);    // first reader holds
    }
    sem_up(&mutex);
    futex(&print_lock , FUTEX_LOCK , 0);
    printf("R[%d]: read %d\n", tid, shared_data);
    futex(&print_lock , FUTEX_UNLOCK , 0);
    sem_down(&mutex);
    read_count--;
    if(read_count==0){
        sem_up(&rw_mutex);  // last reader let it go
    }
    sem_up(&mutex);
}

// TODO: increments shared_data and prints the new value
// acquire writer-lock before accessing shared_data
void write_shared_data() {
    // No need for print lock here... why? -> only one writer available at one time
    // sem_down(&mutex);
    sem_down(&rw_mutex);
    shared_data++;
    printf("W: wrote %d\n", shared_data);
    sem_up(&rw_mutex);
    // sem_up(&mutex);
}


void reader(int *arg) {
    int tid = *arg;

    for (int i = 0; i < NOPS; i++) {
        read_shared_data(tid);
    }

    exit(0);
}

void writer(int *arg) {

    for (int i = 0; i < NOPS; i++) {
        write_shared_data();
    }

    exit(0);
}


int main(int argc, char *argv[]) {

    /* TODO: init semaphores */
    mutex.lock = rw_mutex.lock = 0;
    sem_init(&mutex, 1);
    sem_init(&rw_mutex, 1);

    int r0 = 0, r1 = 1, r2 = 2;
    int w0 = 0;

    create_thread(writer, &w0);

    create_thread(reader, &r0);
    create_thread(reader, &r1);
    create_thread(reader, &r2);

    for (int i = 0; i < 4; i++) {
        join();
    }

    printf("Final value of shared_data: %d\n", shared_data);

    exit(0);
}
