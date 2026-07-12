#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
sem_init(sem_t *s, int val)
{
  s->lock = 0;
  futex(&s->lock, FUTEX_LOCK, 0);
  s->value = val;
  futex(&s->lock, FUTEX_UNLOCK, 0);
}

void
sem_down(sem_t *s)
{
  while(1){
    futex(&s->lock, FUTEX_LOCK, 0);

    if(s->value > 0){
      s->value--;
      futex(&s->lock, FUTEX_UNLOCK, 0);
      return;
    }

    int val = s->value;
    futex(&s->lock, FUTEX_UNLOCK, 0);
    futex_wait(&s->value, val);
  }
}

void
sem_up(sem_t *s)
{
  futex(&s->lock, FUTEX_LOCK, 0);
  s->value++;
  futex_wake(&s->value);
  futex(&s->lock, FUTEX_UNLOCK, 0);
}
