#include <kernel/types.h>
#include <user/user.h>
#include <kernel/stat.h>

sem_t *s;

void thread_execution(int * arg){
    int tid = arg[0];
    int N = arg[1];

    for(int i = 0 ; i < N ; ++i){
        sem_down(&s[tid]);
        printf("T-%d: hello\n", tid);
        sem_up(&s[(tid+1)%3]);
    }
    exit(0);
}

int main(int argc, char*argv[]){
    if(argc!=2){
        printf("Usage: p4 N\n");
        return 0;
    }

    int N = atoi(argv[1]);

    s = (sem_t *)malloc(sizeof(sem_t) * 3);
    sem_init(&s[0], 1);
    sem_init(&s[1], 0);
    sem_init(&s[2], 0);

    int *arg1 = malloc(sizeof(int)*2);
    int *arg2 = malloc(sizeof(int)*2);
    int *arg3 = malloc(sizeof(int)*2);
    arg1[0] = 0; arg2[0] = 1; arg3[0] = 2;
    arg1[1] = N; arg2[1] = N; arg3[1] = N;

    create_thread(thread_execution , arg1);
    create_thread(thread_execution , arg2);
    create_thread(thread_execution , arg3);

    for(int i = 0 ; i < 3 ; ++i){
        join();
    }

}