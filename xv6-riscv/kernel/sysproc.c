#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

struct spinlock futex_lock;

uint64 sys_futex_wait(void)
{
    uint64 uaddr;
    int val;
    argaddr(0 , &uaddr);
    argint(1 , &val);
    acquire(&futex_lock);

    int tmp;
    while (1) {
        if (copyin(myproc()->pagetable, (char*)&tmp, uaddr, sizeof(int)) < 0) {
            release(&futex_lock);
            return -1;
        }

        if (tmp != val)
            break;

        sleep((void*)uaddr, &futex_lock);
    }

    release(&futex_lock);

    return 0;

}

uint64 sys_futex_wake(void)
{
    uint64 uaddr;
    argaddr(0, &uaddr);

    acquire(&futex_lock); //first need to acquire futex_lock to avoid lost_wakeups

    wakeup((void*)uaddr); //will wake everyone sleeping on channel uaddr

    release(&futex_lock);
    return 0;

}

uint64 sys_exit(void)
{
    int n;
    argint(0, &n);
    kexit(n);
    return 0; // not reached
}

uint64 sys_getpid(void)
{
    return myproc()->pid;
}

uint64 sys_fork(void)
{
    return kfork();
}

uint64 sys_wait(void)
{
    uint64 p;
    argaddr(0, &p);
    return kwait(p);
}

uint64 sys_sbrk(void)
{
    uint64 addr;
    int t;
    int n;

    argint(0, &n);
    argint(1, &t);
    addr = myproc()->sz;

    if (t == SBRK_EAGER || n < 0) {
        if (growproc(n) < 0) {
            return -1;
        }
    } else {
        // Lazily allocate memory for this process: increase its memory
        // size but don't allocate memory. If the processes uses the
        // memory, vmfault() will allocate it.
        if (addr + n < addr)
            return -1;
        if (addr + n > TRAPFRAME(NPROC))
            return -1;
        myproc()->sz += n;
        update_sz(myproc()->tgid, myproc()->sz);
    }
    return addr;
}

uint64 sys_pause(void)
{
    int n;
    uint ticks0;

    argint(0, &n);
    if (n < 0)
        n = 0;
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n) {
        if (killed(myproc())) {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

uint64 sys_kill(void)
{
    int pid;

    argint(0, &pid);
    return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void)
{
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}

uint64 sys_clone(void)
{
    uint64 fn;
    uint64 arg;
    uint64 stack;

    argaddr(0, &fn);
    argaddr(1, &arg);
    argaddr(2, &stack);

    return clone(fn, arg, stack);
}

uint64 sys_join(void)
{
    return join();
}
