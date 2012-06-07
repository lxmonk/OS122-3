#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"



struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

struct file* swapfile_open(char* path);  /* A&T forward-declaration */
int not_sonof_shell_init(); //A&T
int del_swap_file(char*); //A&T

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  if (p->pid > 2) {
      /* A&T create the file name for swapping out */
      safestrcpy(p->pagefile_name, ".page", 6);
      itoa(p->pid, &p->pagefile_name[5]);

      /* A&T open the pagefile and save the fd */
      p->pagefile = swapfile_open(p->pagefile_name);
      K_DEBUG_PRINT(3,"pagefile %x",p->pagefile);
      memset(p->pagefile_addr, UNUSED_VA, sizeof(int) * MAX_SWAP_PAGES);
      memset(p->mempage_addr, UNUSED_VA, sizeof(int) * MAX_PSYC_PAGES);
      memset(p->nfu_arr, 0, sizeof(int) * MAX_PSYC_PAGES);
      p->next_to_swap = 0;

      K_DEBUG_PRINT(3,"memset done.", 999);
      p->pages_in_mem = 0;
      p->swapped_pages = 0;
  }
  K_DEBUG_PRINT(3, "returning p=%x", p);
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  initproc = p;
  if((p->pgdir = setupkvm(kalloc)) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;

  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  proc->sz = sz;
  switchuvm(proc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  K_DEBUG_PRINT(3,"pagefile = %x",proc->pagefile);

  // Allocate process.

  if((np = allocproc()) == 0) {
      K_DEBUG_PRINT(4, "allocproc returned 0", 999);
    return -1;
  }

  K_DEBUG_PRINT(4, "after np = allocproc(). np = %x", np);
  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;
  K_DEBUG_PRINT(3,"pagefile = %x",proc->pagefile);

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);

  pid = np->pid;
  np->state = RUNNABLE;
  safestrcpy(np->name, proc->name, sizeof(proc->name));

  //A&T start
  if (not_shell_init() && not_sonof_shell_init() && (proc->pagefile != 0)) {
      np->swapped_pages = proc->swapped_pages;
      np->pages_in_mem = proc->pages_in_mem;
      /* A&T create the file name for swapping out */
      memmove(proc->pagefile_addr,np->pagefile_addr,MAX_SWAP_PAGES);
      np->pagefile = swapfile_open(np->pagefile_name);
      K_DEBUG_PRINT(3,"pagefile = %x, name = %s , pid = %d",proc->pagefile,proc->name,proc->pid);
      /* if ((proc->pagefile != 0) && (np->pagefile != 0)) { */
      /*     char pagebuffer[PGSIZE]; */

      /*     for(i = 0; i < MAX_SWAP_PAGES;i++) { */
      /*         K_DEBUG_PRINT(3,"copying pagefile proc->pagefile = %x",proc->pagefile); */
      /*         set_f_offset(proc->pagefile,i*PGSIZE); */
      /*         set_f_offset(np->pagefile,i*PGSIZE); */
      /*         panic("fork: inside poisonous loope\n"); */
      /*         if (fileread(proc->pagefile,pagebuffer,1) < 0) */
      /*             panic("fork: unable to read from parent swap file\n"); */
      /*         if (filewrite(np->pagefile,pagebuffer,PGSIZE) < 0) */
      /*             panic("fork: unable to write to child swap file\n"); */
      /*     } */
      /* } else { */
      /*     K_DEBUG_PRINT(3, "not copying pagefile. proc->pagefile=%x, proc->pid=%d, " */
      /*                   "np->pagefile=%x, np->pid=%d", proc->pagefile, proc->pid, */
      /*                   np->pagefile, np->pid); */
      /* } */
   }

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  //A&T delete swap file (unlink)
  if (not_shell_init())
      del_swap_file(proc->pagefile_name);

  iput(proc->cwd);
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

void
register_handler(sighandler_t sighandler)
{
  char* addr = uva2ka(proc->pgdir, (char*)proc->tf->esp);
  if ((proc->tf->esp & 0xFFF) == 0)
    panic("esp_offset == 0");

    /* open a new frame */
  *(int*)(addr + ((proc->tf->esp - 4) & 0xFFF))
          = proc->tf->eip;
  proc->tf->esp -= 4;

    /* update eip */
  proc->tf->eip = (uint)sighandler;
}


//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&cpu->scheduler, proc->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    initlog();
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


uint get_fifo_va(void) {
    uint va;

    va = proc->mempage_addr[proc->next_to_swap];
    proc->mempage_addr[proc->next_to_swap] = UNUSED_VA;
    proc->next_to_swap++;
    proc->next_to_swap %= MAX_PSYC_PAGES;
    return va;
}

uint* get_pagefile_addr(void) {
    return proc->pagefile_addr;
}

pde_t*  get_pgdir() {
    return proc->pgdir;
}

struct file* get_pagefile(void) {
    K_DEBUG_PRINT(3,"filename = %s",proc->pagefile_name);
    K_DEBUG_PRINT(3,"file address = %x",proc->pagefile);
    return proc->pagefile;

}

void dec_swapped_pages_number(void) {
    proc->swapped_pages--;
}
void inc_swapped_pages_number(void) {
    proc->swapped_pages++;
}
int get_swapped_pages_number(void) {
    return proc->swapped_pages;
}

void inc_mapped_pages_number(void) {
    proc->pages_in_mem++;
}
int get_mapped_pages_number(void) {
    K_DEBUG_PRINT(4,"inside get_mapped_pages_number. pid=%d, name=%s\n",
                  proc->pid, proc->name);
    K_DEBUG_PRINT(4, "proc->pages_in_mem=%d\n", proc->pages_in_mem);
    return proc->pages_in_mem;
}

int not_shell_init() {
    int ret;
    ret = ((proc->pid > 2) &&
            !((proc->name[0] == 's') && (proc->name[1] == 'h') &&
              (proc->name[2] == 0)));
    K_DEBUG_PRINT(4,"not_shell_init: pid=%d, name=%s, ret=%d\n",
            proc->pid, proc->name, ret);
    return ret;
}

int not_sonof_shell_init() {
    int ret;

    ret = ((proc->parent->pid > 2) &&
           !((proc->parent->name[0] == 's') && (proc->parent->name[1] == 'h') &&
             (proc->parent->name[2] == 0)));
    K_DEBUG_PRINT(4,"not_shell_init: pid=%d, name=%s, ret=%d\n",
                  proc->parent->pid, proc->parent->name, ret);
    return ret;
}

int add_page_va(uint va) {
    int i;

    for(i = 0; i < MAX_PSYC_PAGES; i++) {
        if (proc->mempage_addr[i] == UNUSED_VA)
            break;
    }

    if (i == MAX_PSYC_PAGES)
        panic("memory full trying to add to mempage_addr\n");

    proc->mempage_addr[i] = va;
    proc->nfu_arr[i] = 0;
    return i;
}
