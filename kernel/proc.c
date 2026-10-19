#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
//////////////////////
#include "pstat.h"
///////////////////
#include "defs.h"


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

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
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
void
procinit(void)
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
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
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
static struct proc *
allocproc(void)
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
  p->state = USED;
////////////////////////////////////////////////////
  p->cputime = 0;  // Initialize cputime
////////////////////////////////////////////////////////
  p->priority = 0;  // Initialize priority
///////////////////////////////////////////////////
/////////////////////////2.3
  p->readytime = 0;  // Initialize readytime
//////////////////////////////2.3

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
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
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

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe),
               PTE_R | PTE_W) < 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;
  /////////////////////////2.3
  p->readytime = ticks;  // Process becomes RUNNABLE
  //////////////////////////////2.3

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0) {
    if (sz + n > TRAPFRAME) {
      return -1;
    }
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if (n < 0) {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

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
  ///////////////////2
  np->priority = p->priority;  // Child inherits parent's priority
  ////////////////////////2
  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  /////////////////////////2.3
  np->readytime = ticks;  // Process becomes RUNNABLE
  //////////////////////////////2.3
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
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
int
kwait(uint64 addr)
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

        havekids = 1;
        if (pp->state == ZOMBIE) {
          // Found one.
          pid = pp->pid;
          if (addr != 0 &&
              copyout(p->pagetable, p->sz, addr, (char *)&pp->xstate,
                      sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          pp->parent = 0;
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
    sleep_prepare(p); //DOC: wait-sleep
    release(&wait_lock);
    sleep();
    acquire(&wait_lock);
  }
}

//////////////////////////////////////////////////

// Wait for a child process to exit and return its pid,
// along with its exit status and cpu time usage.
// Return -1 if this process has no children.
// added uint64 ruaddr to the call, 
// a 64 unsigned int, represent the rusage adress 
//  where the caller wants the child's cputime 
int 
kwait2(uint64 addr, uint64 ruaddr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();
  ///////////////////////
  // Temp storage
  struct rusage ru;
  ///////////////////

  acquire(&wait_lock);

  for (;;) {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->parent == p) {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE) {
          // Found one.
          pid = pp->pid;
          if (addr != 0 &&
              copyout(p->pagetable, p->sz, addr, (char *)&pp->xstate,
                      sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
///////////////////////////////////////////
          // Copies the value from the ZOMBIE child
          ru.cputime = pp->cputime;
          // if adrees is not 0, and the copy is less than 0
          if (ruaddr != 0 &&
              // copy data from kernell to user memory. 
              // copys the parent's page table, p->sz is a memory size check
              // ruaddr is the destiny adress in the parents memory
              // (char *)&ru source of the data in the kernel's memory
              // sizeof(ru) asks how many bytes to copy.
              copyout(p->pagetable, p->sz, ruaddr, (char *)&ru,
                      sizeof(ru)) < 0) {
            // If copy failed, realase both locks.
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
///////////////////////////////////////////
          pp->parent = 0;
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
    sleep_prepare(p); //DOC: wait-sleep
    release(&wait_lock);
    sleep();
    acquire(&wait_lock);
  }
}

////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////

// Fill in an array of struct pstat, located in the caller's
// memory at address addr.
// Return the number of processes, or -1 if an error occurred.
int
getprocs(uint64 addr)
{
  // pointer to current slot of the process table
  struct proc *p;
  // the process that called getprocs. 
  struct proc *me = myproc();
  // temp storage for one process's info.
  struct pstat ps;
  // number of processes found so far.
  int proc_found = 0;

  // go through the whole process table
  for (p = proc; p < &proc[NPROC]; p++) {
    // wait_lock is needed to read p->parent safely, and thus needed before p->lock
    acquire(&wait_lock);
    acquire(&p->lock);

    // if the slot is UNUSED, release both locks and skip it
    if (p->state == UNUSED) {
      release(&p->lock);
      release(&wait_lock);
      continue;
    }

    // else, it copies current process's info into the temp struct
    ps.pid = p->pid;
    ps.state = p->state;
    ps.size = p->sz;
    ///////////////////////////////2.2
    // copy the process's priority into the temp struct
    ps.priority = p->priority;
    //////////////////////////////////2.2
    /////////////////////////2.3
    // copy the process's readytime into the temp struct
    ps.readytime = p->readytime;
    //////////////////////////////2.3


    // if process has a parent, store parent's pid in ppid (parent process id)
    if (p->parent)
      ps.ppid = p->parent->pid;
    
    // else default it to 0
    else
      ps.ppid = 0;
    
    // copy the process's name into the temp struct, limited to the size of the name field (16 bytes)
    safestrcpy(ps.name, p->name, sizeof(ps.name));

    // release both locks before copyout and write to user memory
    release(&p->lock);
    release(&wait_lock);

    // copy temp struct to the next available slot of the user's array,
    // Copyout copies from kernel to user memory
    // me->pagetable, tells the kernel where the user's virtual address are located on phys mem
    // me->sz, size of caller's memory, used as a bound to prevent the kernel writing 
    // in memory the caller does not own
    // addr + proc_found * sizeof(ps), calculates the destination address in user memory;
    // addr being the start, sizeof(ps) is the size of the entry, and by multiplying
    // with proc_found, it skips already written entries; 0 for first slot, and so on. 
    // (char *)&ps, address of temp struct in the kernel, (char *) to copy raw bytes
    // sizeof(ps), the bytes to copy
    // if the copy fails (less than 0), return -1 as an error 
    if (copyout(me->pagetable, me->sz, addr + proc_found * sizeof(ps),
                (char *)&ps, sizeof(ps)) < 0)
      return -1;

    // count this process 
    proc_found++;
  }

  // number of processes copied to the user's array
  return proc_found;
}
///////////////////////////////////////////////////

//////////////////////////////////////2.2
// Return the calling process's priority.
int
kgetpriority(void)
{
  // myproc() returns a pointer to the process that made the system call
  struct proc *p = myproc();
  // temp variable to hold the priority while the lock is held.
  int pr;

  // take process's lock.
  acquire(&p->lock);
  // copy the priority into the temp variable.
  pr = p->priority;
  // release the lock. 
  release(&p->lock);
  // return the saved value;
  return pr;
}

// Set the calling process's priority. Valid range is 0 to 49. 
// Return 0 on success, -1 if out of range.
int
ksetpriority(int pr)
{
  // myproc() returns a pointer to the process that made the system call
  struct proc *p = myproc();

  // check range (0 to 49)
  if(pr < 0 || pr > 49)
    // if not in range, return -1
    return -1;

  // take process's lock.
  acquire(&p->lock);
  // store the new priority in the process.
  p->priority = pr;
  // release the lock. 
  release(&p->lock);
  // return 0, meaning success.
  return 0;
}
//////////////////////////////////////2.2
///////////////////////////2.5
// Effective priority: priority plus the time spent waiting as RUNNABLE.
// Capped at 99.
static int
effpriority(struct proc *p)
{
  #if AGING
  // time waiting since the process became RUNNABLE
  int eff = p->priority + (ticks - p->readytime);
  #else
  // aging disabled, so effective priority equals priority
  int eff = p->priority;
  #endif
  // cap at the maximum effective priority
  if (eff > 99)
    eff = 99;
  return eff;
}
///////////////////////////2.5

////////////////////////2.4
#if SCHEDULER == SCHED_RR
///////////////////////////2.4
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;) {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    int found = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Don't re-enable interrupts on release.
        mycpu()->intena = 0;

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if (found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}

///////////////////////////////////2.4
#elif SCHEDULER == SCHED_PRIORITY
// Priority scheduler: runs the RUNNABLE process
// with the highest priority.
void
scheduler(void)
{
  // loop variable over the process table
  struct proc *p;
  // holder of best RUNNABLE process so far
  struct proc *best;
  // pointer to the CPU this scheduler runs on
  struct cpu *c = mycpu();

  // no process is running on this CPU yet
  c->proc = 0;
  for (;;) {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    // no candidate found yet at the start of each scan
    best = 0;
    // go through the whole process table
    for (p = proc; p < &proc[NPROC]; p++) {
      // lock the slot before reading its state and priority
      acquire(&p->lock);
      // candidate must be RUNNABLE.
      // if it is the first one found (best == 0)
      // or with a higher priority than the current best
      // it is replaced.
	///////////////////////////2.5 
      if (p->state == RUNNABLE &&
         //  (best == 0 || p->priority > best->priority)) {
	 (best == 0 || effpriority(p) > effpriority(best))) {
	///////////////////////////2.5
        // the previous best is replaced, and its lock is released
        if (best != 0)
          release(&best->lock);
        // the new best keeps its lock held.
        best = p;
        // skip the release
        continue;
      }
      // not a better candidate, so release its lock
      release(&p->lock);
    }

    // a RUNNABLE process was found
    if (best != 0) {
      // best is still locked, and can be switched to safely
      p = best;
      // Switch to chosen process.  It is the process's job
      // to release its lock and then reacquire it
      // before jumping back to us.
      p->state = RUNNING;
      // record it as the process running on this CPU
      c->proc = p;
      // switch from the scheduler context to the process context
      swtch(&c->context, &p->context);

      // Don't re-enable interrupts on release.
      mycpu()->intena = 0;
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      // release the lock held since the scan
      release(&p->lock);
    } 
    else {
      // nothing to run, wait for an interrupt
      asm volatile("wfi");
    }
  }
}
#endif
///////////////////////////////////2.4


// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
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
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  /////////////////////////2.3
  p->readytime = ticks;  // Process becomes RUNNABLE
  //////////////////////////////2.3
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (__atomic_load_n(&first, __ATOMIC_ACQUIRE)) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    // ensure other cores see first=0.
    __atomic_store_n(&first, 0, __ATOMIC_RELEASE);

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){"/init", 0});
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Register current process as waiting for wakeups on chan.
void
sleep_prepare(void *chan)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  if (chan == 0)
    panic("sleep_prepare: zero chan");
  p->chan = chan;
  release(&p->lock);
}

// Put the thread to sleep.  Assumes sleep_prepare() was called before.
// If the channel registered by sleep_prepare() has been woken up in
// the meantime, do not go to sleep, and instead return immediately.
void
sleep(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  if (p->chan != 0) {
    p->state = SLEEPING;
    sched();
  }
  release(&p->lock);
}

// Wake up all processes sleeping on channel chan.
void
wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->chan == chan) {
      // If the process is waiting for wakeups on this channel,
      // signal that the wakeup happened by clearing p->chan.
      p->chan = 0;

      // If this waiting process has gotten so far as to actually
      // go to sleep, also set it back to RUNNING.
      if (p->state == SLEEPING) {
        p->state = RUNNABLE;
	/////////////////////////2.3
        p->readytime = ticks;  // Process becomes RUNNABLE
	//////////////////////////////2.3
      }
    }
    release(&p->lock);
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING) {
        // Wake process from sleep().
        p->state = RUNNABLE;
	/////////////////////////2.3
        p->readytime = ticks;  // Process becomes RUNNABLE
	//////////////////////////////2.3
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
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
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst) {
    return copyout(p->pagetable, p->sz, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src) {
    return copyin(p->pagetable, p->sz, dst, src, len);
  } else {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
    // clang-format off
    [UNUSED]    = "unused",
    [USED]      = "used",
    [SLEEPING]  = "sleep ",
    [RUNNABLE]  = "runble",
    [RUNNING]   = "run   ",
    [ZOMBIE]    = "zombie"
    // clang-format on
  };
  struct proc *p;
  char *state;

  printk("\n");
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printk("%d %s %s", p->pid, state, p->name);
    printk("\n");
  }
}
