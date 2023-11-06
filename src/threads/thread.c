#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

static struct list blocked_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */
fixed_point load_avg;

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  
  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  
  list_init (&blocked_list);
  initial_thread->niceness = NICENESS_DEFAULT;
  initial_thread->cpu_recent = CPU_RECENT_DEFAULT;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);
  load_avg = LOAD_AVG_DEFAULT;

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

void thread_sleep (int64_t ticks)
{
  enum intr_level old = intr_disable();
  struct thread *t;
  t = thread_current();
  
  ASSERT(t != idle_thread);

  t->wake = ticks;

  list_push_back(&blocked_list, &t->elem);
  thread_block();
  
  intr_set_level(old);
}

void thread_wake (int64_t ticks)
{
  struct list_elem *l = list_begin(&blocked_list);

  while (l != list_end(&blocked_list)){
    struct thread *t = list_entry (l, struct thread, elem);
    if (t->wake <= ticks){
      l = list_remove(l);
      thread_unblock (t);
    }
    else
      l = list_next(l);
    }
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* initialize pcb*/
  t->parent = thread_current ();

  t->pcb = palloc_get_page (0);

  if (t->pcb == NULL) {
    return TID_ERROR;
  }

  t->pcb->fd_table = palloc_get_page (PAL_ZERO);

  if (t->pcb->fd_table == NULL) {
    palloc_free_page (t->pcb);
    return TID_ERROR;
  }

  t->pcb->fd_cnt = 2;
  t->pcb->file_ex = NULL;
  t->pcb->num_exit = -1;
  t->pcb->exited = false;
  t->pcb->loaded = false;

  sema_init (&(t->pcb->semaphore_wait), 0);
  sema_init (&(t->pcb->semaphore_load), 0);

  list_push_back (&(t->parent->list_child), &(t->elem_child));
  /* Add to run queue. */
  thread_unblock (t);

  // Make sure currently running thread is maximum priority after unblocking
  thread_compare_first();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);

  /* Change the list ordering method to ordered insertion based on priority */
  list_insert_ordered(&ready_list, &t->elem, thread_cmp_priority, 0);
  //list_push_back (&ready_list, &t->elem);
  
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) {
    //list_push_back (&ready_list, &cur->elem);
    // Similar to thread_yield, change thread insertion to ready list into ordered listing based on priority
    list_insert_ordered(&ready_list, &cur->elem, thread_cmp_priority, 0);
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if(thread_mlfqs)
    return;
  
  thread_current ()->start_priority = new_priority;
  // Make sure current thead is maximum priority after updating
  update_priority();
  thread_compare_first();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  enum intr_level level_before = intr_disable();
  struct thread *t;
  t = thread_current();

  t->niceness = nice;
  mlfqs_compute_priority(t);
  thread_compare_first();

  intr_set_level(level_before);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  enum intr_level level_before = intr_disable();
  struct thread *t;
  t = thread_current();
  int current_niceness = t->niceness;
  intr_set_level(level_before);
  return current_niceness;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  enum intr_level level_before = intr_disable();
  fixed_point mul_fixed_point = fixed_point_mul_int(load_avg, 100);
  int return_value = fixed_point_to_int_toNearest(mul_fixed_point);
//  fixed_point return_value = fixed_point_mul_int(load_avg, 100);
  intr_set_level(level_before);
  return return_value;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  enum intr_level level_before = intr_disable();
  struct thread *t;
  t = thread_current();
  fixed_point cur_recent_cpu = t->cpu_recent;
  fixed_point mul_fixed_point = fixed_point_mul_int(cur_recent_cpu, 100);
  int return_value = fixed_point_to_int_toNearest(mul_fixed_point);
  intr_set_level(level_before);
  return return_value;
}

// For MLFQS Advanced Scheduling

// Compute the priority of thread
void mlfqs_compute_priority(struct thread *t){
  if (t != idle_thread){
    // priority = PRI_MAX - (cpu_recent / 4) - (nice * 2)
    fixed_point cpu_recent_div = fixed_point_div_int(t->cpu_recent, -4);
    int tmp_priority = PRI_MAX - (t->niceness * 2);
    fixed_point new_priority_fip = fixed_point_sum_int(cpu_recent_div, tmp_priority);
    int new_priority = fixed_point_to_int_toNearest(new_priority_fip);
    if (new_priority > PRI_MAX)
      t->priority = PRI_MAX;
    else if (new_priority < PRI_MIN)
      t->priority = PRI_MIN;
    else
      t->priority = new_priority;
  }
  return;
}

// Compute the recent_cpu value of thread
// void mlfqs_compute_recent_cpu(struct thread *t){
//   if (t != idle_thread){
//     // cpu_recent = (2 * load_avg) / (2 * load_avg + 1) * cpu_recent + niceness
//     fixed_point load_avg_2 = fixed_point_mul_int(load_avg, 2);
//     fixed_point load_avg_2_1 = fixed_point_sum_int(load_avg_2, 1);
//     fixed_point load_avg_ratio = fixed_point_div_fip(load_avg_2, load_avg_2_1);
//     fixed_point load_avg_ratio_rec = fixed_point_mul_fip(load_avg_ratio, t->cpu_recent);
//     fixed_point new_value_fip = fixed_point_sum_int(load_avg_ratio_rec, t->niceness);
//     t->cpu_recent = new_value_fip;
//   }
//   return;
// } 

// Compute the load_avg value for global variable
void mlfqs_calculate_load_avg(void){
  int ready_threads = list_size(&ready_list);
  struct thread *t = thread_current();
  if (t != idle_thread)
    ready_threads++;
  
  //load_avg = (59/60) * load_avg + (1/60) * ready_threads
  fixed_point div_59_60 = fixed_point_div_fip(integer_to_fixed_point(59), integer_to_fixed_point(60));
  fixed_point div_01_60 = fixed_point_div_fip(integer_to_fixed_point(1), integer_to_fixed_point(60));
  fixed_point add_1 = fixed_point_mul_fip(div_59_60, load_avg);
  fixed_point add_2 = fixed_point_mul_int(div_01_60, ready_threads);
  fixed_point final_value = fixed_point_sum_fip(add_1, add_2);
  load_avg = final_value;
}

// Increment the value of recent_cpu of running thread by 1
void mlfqs_increase_recent_cpu(void){
  struct thread *t;
  t = thread_current();
  if(t != idle_thread){
    fixed_point new_recent_cpu = fixed_point_sum_int(t->cpu_recent, 1);
    t->cpu_recent = new_recent_cpu;
  }
}

// Recompute the recent_cpu value for all threads every 4 ticks
void mlfqs_recompute_recent_cpu(void){
  struct list_elem *e;

  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)){
    struct thread *thr = list_entry(e, struct thread, allelem);

    if (thr != idle_thread){
      // cpu_recent = (2 * load_avg) / (2 * load_avg + 1) * cpu_recent + niceness
      fixed_point load_avg_2 = fixed_point_mul_int(load_avg, 2);
      fixed_point load_avg_2_1 = fixed_point_sum_int(load_avg_2, 1);
      fixed_point load_avg_ratio = fixed_point_div_fip(load_avg_2, load_avg_2_1);
      fixed_point load_avg_ratio_rec = fixed_point_mul_fip(load_avg_ratio, thr->cpu_recent);
      fixed_point new_value_fip = fixed_point_sum_int(load_avg_ratio_rec, thr->niceness);
      thr->cpu_recent = new_value_fip;
    }
  }
} 

// Recompute the priority of all threads every second
void mlfqs_recompute_priority(void){
  struct list_elem *e;

  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)){
    struct thread *thr = list_entry(e, struct thread, allelem);
    mlfqs_compute_priority(thr);
  }
  list_sort(&ready_list, thread_cmp_priority, NULL);
}


/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);
  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  
  t->start_priority = priority;
  list_init(&t->donation_list);
  t->wait_for_release = NULL;

  t->niceness = NICENESS_DEFAULT;
  t->cpu_recent = CPU_RECENT_DEFAULT;

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

// Function to compare priority for use in list_insert_ordered as priority comparison function
bool thread_cmp_priority (const struct list_elem *thread1, const struct list_elem *thread2, void *aux UNUSED){
  return list_entry(thread1, struct thread, elem)->priority > list_entry(thread2, struct thread, elem)->priority;
}

void thread_compare_first(void){
  if (list_empty (&ready_list)) return;
  int currentPriority = thread_current()->priority;
  int listFrontPriority = list_entry(list_front(&ready_list), struct thread, elem)->priority;

  if (currentPriority < listFrontPriority)
    thread_yield();
}

bool cmp_donate_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
  return list_entry (a, struct thread, elem_donation)->priority > list_entry (b, struct thread, elem_donation)->priority;
}

void priority_donate(void){
  struct thread *t;
  t = thread_current();
  int d = 0;
  for (d = 0; d < 8; d++){
    if (!t->wait_for_release) break;
    struct thread *lock_holder;
    lock_holder = t->wait_for_release->holder;
    lock_holder->priority = t->priority;
    t = lock_holder;
  }
}

void delete_donation (struct lock *lock){
  struct thread *t;
  t = thread_current ();
  struct list_elem *l;
  for (l = list_begin (&t->donation_list); l != list_end (&t->donation_list); l = list_next (l)){
     struct thread *h;
     h = list_entry (l, struct thread, elem_donation);
     if (h->wait_for_release == lock)
	list_remove (&h->elem_donation);
  }
}

void update_priority (void){
  struct thread *t;
  t = thread_current();
  t->priority = t->start_priority;

  if (!list_empty(&t->donation_list)){
      list_sort (&t->donation_list, cmp_donate_priority, 0);
      struct thread *first;
      first = list_entry (list_front (&t->donation_list), struct thread, elem_donation);
      if (t->priority < first->priority)
	t->priority = first->priority;
      }
}
/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

struct pcb *
get_child_pcb (tid_t child_tid)
{
  struct thread *t = thread_current ();
  struct thread *child_t;
  struct list *child_list = &(t->list_child);
  struct list_elem *e;

  for (e = list_begin (child_list); e != list_end (child_list); e = list_next (e)) 
  {
    child_t = list_entry (e, struct thread, elem_child);
    if (child_t->tid == child_tid) 
      return child_t->pcb;
  }

  return NULL;
}

struct thread *
get_child_thread (tid_t child_tid)
{
  struct thread *t = thread_current ();
  struct thread *child_t;
  struct list *child_list = &(t->list_child);
  struct list_elem *e;

  for (e = list_begin (child_list); e != list_end (child_list); e = list_next (e)) 
  {
    child_t = list_entry (e, struct thread, elem_child);
    if (child_t->tid == child_tid) 
      return child_t;
  }

  return NULL;
}

