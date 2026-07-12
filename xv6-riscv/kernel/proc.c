#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

void
proc_vma_init(struct proc *p)
{
    memset(p->vmas, 0, sizeof(p->vmas));
}

static void
vma_writeback(struct vma *v, pagetable_t pagetable, uint64 start, uint64 end)
{
    if(v->f == 0)
        return;
    if((v->flags & MAP_SHARED) == 0)
        return;
    if((v->prot & PROT_WRITE) == 0)
        return;

    for(uint64 a = start; a < end; a += PGSIZE){
        pte_t *pte = walk(pagetable, a, 0);
        if(pte == 0 || (*pte & PTE_V) == 0)
            continue;

        uint64 pa = PTE2PA(*pte);
        uint64 off = v->off + (a - v->addr);
        begin_op();
        ilock(v->f->ip);
        writei(v->f->ip, 1, pa, off, PGSIZE);
        iunlock(v->f->ip);
        end_op();
    }
}

int
proc_munmap(struct proc *p, uint64 addr, uint64 len)
{
    uint64 end = addr + len;
    int changed = 0;
    if(addr % PGSIZE || len == 0 || end < addr)
        return -1;

    for(int i = 0; i < MAXVMA; i++){
        struct vma *v = &p->vmas[i];
        if(!v->used)
            continue;

        uint64 vstart = v->addr;
        uint64 vend = v->addr + v->len;
        if(end <= vstart || addr >= vend)
            continue;

        uint64 unmap_start = addr > vstart ? addr : vstart;
        uint64 unmap_end = end < vend ? end : vend;

        if(unmap_start > vstart && unmap_end < vend)
            return -1;

        vma_writeback(v, p->pagetable, unmap_start, unmap_end);
        uvmunmap(p->pagetable, unmap_start, (unmap_end - unmap_start) / PGSIZE, 1);
        changed = 1;

        if(unmap_start == vstart && unmap_end == vend){
            if(v->f)
                fileclose(v->f);
            memset(v, 0, sizeof(*v));
        } else if(unmap_start == vstart){
            v->addr = unmap_end;
            v->off += unmap_end - unmap_start;
            v->len = vend - unmap_end;
        } else if(unmap_end == vend){
            v->len = unmap_start - vstart;
        }
    }

    return changed ? 0 : -1;
}

void
proc_unmap_vmas(struct proc *p, pagetable_t pagetable, int close_files)
{
    for(int i = 0; i < MAXVMA; i++){
        struct vma *v = &p->vmas[i];
        if(!v->used)
            continue;

        if(pagetable){
            vma_writeback(v, pagetable, v->addr, v->addr + v->len);
            uvmunmap(pagetable, v->addr, v->len / PGSIZE, 1);
        }

        if(close_files && v->f)
            fileclose(v->f);
        memset(v, 0, sizeof(*v));
    }
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        char *pa = kalloc();
        if (pa == 0)
            panic("kalloc");
        uint64 va = KSTACK((int)(p - proc));
        kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
}

// initialize the proc table.
void procinit(void)
{
    struct proc *p;

    initlock(&pid_lock, "nextpid");
    initlock(&wait_lock, "wait_lock");
    for (p = proc; p < &proc[NPROC]; p++) {
        initlock(&p->lock, "proc");
        p->state = UNUSED;
        p->kstack = KSTACK((int)(p - proc));
    }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
    int id = r_tp();
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *mycpu(void)
{
    int id = cpuid();
    struct cpu *c = &cpus[id];
    return c;
}

// Return the current struct proc *, or zero if none.
struct proc *myproc(void)
{
    push_off();
    struct cpu *c = mycpu();
    struct proc *p = c->proc;
    pop_off();
    return p;
}

int allocpid()
{
    int pid;

    acquire(&pid_lock);
    pid = nextpid;
    nextpid = nextpid + 1;
    release(&pid_lock);

    return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *allocproc(void)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state == UNUSED) {
            goto found;
        } else {
            release(&p->lock);
        }
    }
    return 0;

found:
    p->pid = allocpid();
    p->tgid = p->pid;
    p->state = USED;
    p->thread_count = 1;
    proc_vma_init(p);
    p->priority = 0;
    p->ticks_used = 0;

    // Allocate a trapframe page.
    if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // An empty user page table.
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // Set up new context to start executing at forkret,
    // which returns to user space.
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;

    return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void freeproc(struct proc *p)
{
    if (p->trapframe)
        kfree((void *)p->trapframe);
    p->trapframe = 0;
    if (p->pagetable) {
        if (p->pid == p->tgid) {
            proc_unmap_vmas(p, p->pagetable, 1);
            proc_freepagetable(p->pagetable, p->sz, (int)(p - proc));
        } else {
            proc_unmap_vmas(p, 0, 1);
            uvmunmap(p->pagetable, TRAPFRAME((int)(p - proc)), 1, 0);
        }
    }
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t proc_pagetable(struct proc *p)
{
    pagetable_t pagetable;

    // An empty page table.
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
                 PTE_R | PTE_X) < 0) {
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trapframe page below the trampoline page, for
    // trampoline.S.
    if (mappages(pagetable, TRAPFRAME((int)(p - proc)), PGSIZE,
                 (uint64)(p->trapframe), PTE_R | PTE_W) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz, int idx)
{
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME(idx), 1, 0);
    uvmfree(pagetable, sz);
}

// Set up first user process.
void userinit(void)
{
    struct proc *p;

    p = allocproc();
    initproc = p;

    p->cwd = namei("/");

    p->state = RUNNABLE;

    release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
    uint64 sz;
    struct proc *p = myproc();

    sz = p->sz;
    if (n > 0) {
        if (sz + n > TRAPFRAME(NPROC)) {
            return -1;
        }
        if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
            return -1;
        }
    } else if (n < 0) {
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    update_sz(p->tgid, sz);
    return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int kfork(void)
{
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();

    // If not main thread, cannot fork
    if (p->pid != p->tgid)
        return -1;

    // Allocate process.
    if ((np = allocproc()) == 0) {
        return -1;
    }

    // Copy user memory from parent to child.
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;

    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

    // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;

    // increment reference counts on open file descriptors.
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    for(i = 0; i < MAXVMA; i++){
        if(!p->vmas[i].used)
            continue;
        np->vmas[i] = p->vmas[i];
        if(np->vmas[i].f)
            filedup(np->vmas[i].f);
    }

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;

    release(&np->lock);

    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    np->state = RUNNABLE;
    release(&np->lock);

    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
    struct proc *pp;

    for (pp = proc; pp < &proc[NPROC]; pp++) {
        if (pp->parent == p) {
            // WPTHREAD
            pp->parent = initproc;
            if (pp->tgid == p->pid) {
                acquire(&pp->lock);
                for (int fd = 0; fd < NOFILE; fd++) {
                    if (pp->ofile[fd]) {
                        struct file *f = pp->ofile[fd];
                        fileclose(f);
                        pp->ofile[fd] = 0;
                    }
                }

                if (pp->cwd) {
                    begin_op();
                    iput(pp->cwd);
                    end_op();
                    pp->cwd = 0;
                }

                freeproc(pp);
                release(&pp->lock);

                p->thread_count--;
            } else {
                wakeup(initproc);
            }
        }
    }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void kexit(int status)
{
    struct proc *p = myproc();

    if (p == initproc)
        panic("init exiting");

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    acquire(&wait_lock);

    // Give any children to init.
    reparent(p);

    // Parent might be sleeping in wait().
    wakeup(p->parent);

    acquire(&p->lock);

    p->xstate = status;
    p->state = ZOMBIE;

    release(&wait_lock);

    // Jump into the scheduler, never to return.
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int kwait(uint64 addr)
{
    struct proc *pp;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for (pp = proc; pp < &proc[NPROC]; pp++) {
            if (pp->parent == p) {
                // make sure the child isn't still in exit() or swtch().
                acquire(&pp->lock);

                if (pp->tgid == p->pid) {
                    release(&pp->lock);
                    continue;
                }

                havekids = 1;
                if (pp->state == ZOMBIE) {
                    // Found one.
                    pid = pp->pid;
                    if (addr != 0 &&
                        copyout(p->pagetable, addr, (char *)&pp->xstate,
                                sizeof(pp->xstate)) < 0) {
                        release(&pp->lock);
                        release(&wait_lock);
                        return -1;
                    }
                    freeproc(pp);
                    release(&pp->lock);
                    release(&wait_lock);
                    return pid;
                }
                release(&pp->lock);
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || killed(p)) {
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); // DOC: wait-sleep
    }
}

void scheduler(void)
{
    struct proc *p;
    struct cpu *c = mycpu();

    c->proc = 0;
    for (;;) {
        intr_on();
        intr_off();

        struct proc *chosen = 0;
        int chosen_pri = NMLFQ;

        for (p = proc; p < &proc[NPROC]; p++) {
            acquire(&p->lock);
            if (p->state == RUNNABLE && p->priority < chosen_pri) {
                if (chosen)
                    release(&chosen->lock);
                chosen = p;
                chosen_pri = p->priority;
            } else {
                release(&p->lock);
            }
        }

        if (chosen) {
            chosen->state = RUNNING;
            c->proc = chosen;
            swtch(&c->context, &chosen->context);
            c->proc = 0;
            release(&chosen->lock);
        } else {
            asm volatile("wfi");
        }
    }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
    int intena;
    struct proc *p = myproc();

    if (!holding(&p->lock))
        panic("sched p->lock");
    if (mycpu()->noff != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched RUNNING");
    if (intr_get())
        panic("sched interruptible");

    intena = mycpu()->intena;
    swtch(&p->context, &mycpu()->context);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
}

// Called on each timer interrupt for the running process.
void
mlfq_tick(void)
{
    struct proc *p = myproc();
    if(p == 0)
        return;

    static int mlfq_slice[] = { MLFQ_SLICE0, MLFQ_SLICE1, MLFQ_SLICE2, MLFQ_SLICE3 };

    acquire(&p->lock);
    p->ticks_used++;

    int pri = p->priority;
    if(pri < 0) pri = 0;
    if(pri >= NMLFQ) pri = NMLFQ - 1;

    if(p->ticks_used >= mlfq_slice[pri]){
        if(p->priority < NMLFQ - 1)
            p->priority++;
        p->ticks_used = 0;
    }
    release(&p->lock);
}

// Boost all processes to highest priority to prevent starvation.
void
mlfq_boost(void)
{
    struct proc *p;
    for(p = proc; p < &proc[NPROC]; p++){
        acquire(&p->lock);
        if(p->state != UNUSED){
            p->priority = 0;
            p->ticks_used = 0;
        }
        release(&p->lock);
    }
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
    extern char userret[];
    static int first = 1;
    struct proc *p = myproc();

    // Still holding p->lock from scheduler.
    release(&p->lock);

    if (first) {
        // File system initialization must be run in the context of a
        // regular process (e.g., because it calls sleep), and thus cannot
        // be run from main().
        fsinit(ROOTDEV);

        first = 0;
        // ensure other cores see first=0.
        __sync_synchronize();

        // We can invoke kexec() now that file system is initialized.
        // Put the return value (argc) of kexec into a0.
        p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
        if (p->trapframe->a0 == -1) {
            panic("exec");
        }
    }

    // return to user space, mimicing usertrap()'s return.
    prepare_return();
    uint64 satp = MAKE_SATP(p->pagetable);
    uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
    ((void (*)(uint64))trampoline_userret)((uint64)satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void sleep(void *chan, struct spinlock *lk)
{
    struct proc *p = myproc();

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    acquire(&p->lock); // DOC: sleeplock1
    release(lk);

    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    release(&p->lock);
    acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void wakeup(void *chan)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        if (p != myproc()) {
            acquire(&p->lock);
            if (p->state == SLEEPING && p->chan == chan) {
                p->state = RUNNABLE;
                p->priority = 0;
                p->ticks_used = 0;
            }
            release(&p->lock);
        }
    }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kkill(int pid)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->pid == pid) {
            p->killed = 1;
            if (p->state == SLEEPING) {
                // Wake process from sleep().
                p->state = RUNNABLE;
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}

void setkilled(struct proc *p)
{
    acquire(&p->lock);
    p->killed = 1;
    release(&p->lock);
}

int killed(struct proc *p)
{
    int k;

    acquire(&p->lock);
    k = p->killed;
    release(&p->lock);
    return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
    struct proc *p = myproc();
    if (user_dst) {
        return copyout(p->pagetable, dst, src, len);
    } else {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
    struct proc *p = myproc();
    if (user_src) {
        return copyin(p->pagetable, dst, src, len);
    } else {
        memmove(dst, (char *)src, len);
        return 0;
    }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
    static char *states[] = {
        [UNUSED] "unused",   [USED] "used",      [SLEEPING] "sleep ",
        [RUNNABLE] "runble", [RUNNING] "run   ", [ZOMBIE] "zombie"
    };
    struct proc *p;
    char *state;

    printf("\n");
    for (p = proc; p < &proc[NPROC]; p++) {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        printf("%d %s %s", p->pid, state, p->name);
        printf("\n");
    }
}

void update_sz(int tgid, uint64 sz)
{
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->tgid == tgid) {
            p->sz = sz;
        }
        release(&p->lock);
    }
}

int clone(uint64 fn, uint64 arg, uint64 stack)
{
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();

    // Check if main thread.
    if (p->pid != p->tgid) {
        return -1;
    }

    // Allocate process.
    if ((np = allocproc()) == 0) {
        return -1;
    }

    // Copy user memory from parent to child.
    np->pagetable = p->pagetable;
    np->sz = p->sz;
    mappages(np->pagetable, TRAPFRAME((int)(np - proc)), PGSIZE,
             (uint64)(np->trapframe), PTE_R | PTE_W);

    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

    np->trapframe->sp = stack;
    np->trapframe->a0 = arg;
    np->trapframe->epc = fn;

    // increment reference counts on open file descriptors.
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    for(i = 0; i < MAXVMA; i++){
        if(!p->vmas[i].used)
            continue;
        np->vmas[i] = p->vmas[i];
        if(np->vmas[i].f)
            filedup(np->vmas[i].f);
    }

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;

    release(&np->lock);

    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    np->state = RUNNABLE;
    release(&np->lock);

    p->thread_count += 1;
    np->tgid = p->tgid;

    return pid;
}

int join(void)
{
    struct proc *pp;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for (pp = proc; pp < &proc[NPROC]; pp++) {
            if (pp->parent == p) {
                // make sure the child isn't still in exit() or swtch().
                acquire(&pp->lock);

                if (pp->tgid != p->pid) {
                    release(&pp->lock);
                    continue;
                }

                havekids = 1;
                if (pp->state == ZOMBIE) {
                    // Found one.
                    pid = pp->pid;
                    freeproc(pp);
                    release(&pp->lock);
                    release(&wait_lock);
                    p->thread_count--;
                    return pid;
                }
                release(&pp->lock);
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || killed(p)) {
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); // DOC: wait-sleep
    }
}

int
getprocs(uint64 addr, int max)
{
    struct proc *p;
    struct procinfo info;
    int count = 0;
    struct proc *curp = myproc();

    for(p = proc; p < &proc[NPROC] && count < max; p++){
        acquire(&p->lock);
        if(p->state != UNUSED){
            info.pid = p->pid;
            info.state = p->state;
            info.priority = p->priority;
            info.sz = p->sz;
            safestrcpy(info.name, p->name, sizeof(info.name));
            release(&p->lock);
            if(copyout(curp->pagetable, addr + count * sizeof(info),
                       (char *)&info, sizeof(info)) < 0)
                return -1;
            count++;
        } else {
            release(&p->lock);
        }
    }
    return count;
}

int
setpriority(int pid, int priority)
{
    if(priority < 0 || priority >= NMLFQ)
        return -1;

    struct proc *p;
    for(p = proc; p < &proc[NPROC]; p++){
        acquire(&p->lock);
        if(p->pid == pid){
            p->priority = priority;
            p->ticks_used = 0;
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}
