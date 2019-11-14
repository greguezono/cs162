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
#include "devices/timer.h"
#include "threads/fixed-point.h"
#include "threads/malloc.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/syscall.h"
#endif
#ifdef FILESYS
#include "filesys/directory.h"
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

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* ------------------------------ Our Code Inserted Below here --------------------------------- */

/* List of processes in THREAD_BLOCKED state, specifically due to timer_sleep being called.
   This is an ordered list ordered by increasing sleep time (because the wake times will not
   change once set). */
static struct list sleep_list;

/* ------------------------------ Our Code Inserted Above here --------------------------------- */

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

/* ------------------------------ Our Code Inserted Below here --------------------------------- */

/* estimates the average number of threads ready to run over the past minute. */
static fixed_point_t load_avg;
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

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

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
/* Project 1 */
static bool wake_time_less (const struct list_elem *a,
                            const struct list_elem *b,
                            void *aux UNUSED);
bool priority_less (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void update_load_avg (void);
void update_recent_cpu (struct thread *t, void *aux);
void update_priority (struct thread *t, void *aux);
void update_all_priority (void);
int get_max_priority (void);
int get_original_priority (void);

/* Project 2 */
bool init_exit_status(struct thread *, tid_t);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

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

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  list_init (&sleep_list);
  load_avg = fix_int (0);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */

/* ------------------------------ Our Code Inserted Below here --------------------------------- */

/* set the current directories for the initial threads. Other threads inherit from parent. */
void
directory_init (void)
{
  idle_thread->curr_dir = dir_open_root ();

  initial_thread->curr_dir = dir_open_root ();
}
/* ------------------------------ Our Code Inserted Above here --------------------------------- */


void
thread_start (void)
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
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
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  if (!init_exit_status(t, tid)) {
    return TID_ERROR;
  }
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

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

  /* Add to run queue. */
  thread_unblock (t);

  t->is_donated = false;
  if (thread_current ()->priority < t->priority) {
    thread_yield ();
  }
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
  list_push_back (&ready_list, &t->elem);
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
  if (cur != idle_thread)
    list_push_back (&ready_list, &cur->elem);
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
/* ------------------------------ Our Code Inserted Below here ---------------------------------- */
  enum intr_level old_level = intr_disable ();
  struct thread *t = thread_current ();

  bool yield_ret = false;

  t->original_priority = new_priority;

  if (!t->is_donated || (t->is_donated && new_priority > t->priority))
    t->priority = new_priority;

  if (t->priority < get_max_priority ())
    yield_ret = true;

  intr_set_level (old_level);

  if (yield_ret)
  	thread_yield ();
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
}

/* Returns the current thread's (effective) priority. */
int
thread_get_priority (void)
{
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  return thread_current ()->priority;
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice)
{
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  struct thread *t = thread_current ();
  t->nice = nice;
  update_priority (t, NULL);
  if (t->priority < get_max_priority ())
  	thread_yield ();
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  struct thread *cur = thread_current ();
  return cur->nice;
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void)
{
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  return fix_round (fix_scale (load_avg, 100));
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  struct thread *cur = thread_current ();
  return fix_round (fix_scale (cur->recent_cpu, 100));
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

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

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  /* Project 1 */
  t->is_donated = false;
  t->original_priority = t->priority;
  list_init (&(t->locks));
  t->blocked_by = NULL;

  if (thread_mlfqs) {
  	if (t == initial_thread) {
  		t->nice = 0;
  		t->recent_cpu = fix_int(0);
  	} else {
  		t->nice = thread_get_nice();
  		t->recent_cpu = thread_current()->recent_cpu;
  	}
  	update_priority(t, NULL);
  }

  /* Project 2 */
  list_init (&(t->children));
  lock_init (&(t->children_lock));

  /* Project 3 */
  t->fd_table = NULL;
  t->fd_table_size = 0;
  t->fd_table_allocated_size = 0;

  /* inherit parents current directory */
  if (initial_thread->curr_dir)
    t->curr_dir = dir_open_cwd ();
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

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
  else {
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
    struct list_elem *max_prior = list_max(&ready_list, priority_less, NULL);
    list_remove (max_prior);
    return list_entry (max_prior, struct thread, elem);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
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

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
/* Project 1 */
void
thread_sleep (int64_t ticks)
{
  struct thread *cur = thread_current ();
  cur->wake_time = timer_ticks () + ticks;
  enum intr_level old = intr_disable ();
  list_insert_ordered(&sleep_list, &cur->sleep_elem, wake_time_less, NULL);
  thread_block ();
  intr_set_level (old);
}

void
thread_wake (void)
{
  if (list_empty (&sleep_list))
    return;

  struct list_elem *e;

  for (e = list_begin (&sleep_list); e != list_end (&sleep_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, sleep_elem);
    if (t->wake_time <= timer_ticks ()) {
      enum intr_level old = intr_disable ();
      list_remove (e);
      thread_unblock (t);
      intr_set_level (old);
    } else {
      return;
    }
  }
}

static bool
wake_time_less (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct thread *t_a = list_entry (a, struct thread, sleep_elem);
  struct thread *t_b = list_entry (b, struct thread, sleep_elem);
  return t_a->wake_time < t_b->wake_time;
}

bool
priority_less (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct thread *t_a = list_entry (a, struct thread, elem);
  struct thread *t_b = list_entry (b, struct thread, elem);
  return t_a->priority < t_b->priority;
}

void
update_load_avg (void)
{
  struct thread *cur = thread_current ();

  fixed_point_t fix_59_60 = fix_frac (59, 60);
  fixed_point_t fix_1_60 = fix_frac (1, 60);

  int ready_threads = cur != idle_thread ? list_size(&ready_list) + 1 : list_size(&ready_list);
  load_avg = fix_add (fix_mul (fix_59_60 , load_avg), fix_mul (fix_1_60, fix_int (ready_threads)));
}

void
update_recent_cpu (struct thread *t, void *aux UNUSED)
{
	fixed_point_t double_load = fix_scale (load_avg, 2);
	fixed_point_t fix_nice = fix_int(t->nice);
  fixed_point_t temp = fix_div (double_load, fix_add (double_load, fix_int(1)));
	t->recent_cpu = fix_add (fix_mul (temp, t->recent_cpu), fix_nice);
}

void
update_all_recent_cpu (void)
{
	thread_foreach (update_recent_cpu, NULL);
}

void
update_priority (struct thread *t, void *aux UNUSED)
{
	fixed_point_t cpu_4 = fix_unscale (t->recent_cpu, 4);
	fixed_point_t nice_2 = fix_int (t->nice * 2);
	fixed_point_t prio_max = fix_int(PRI_MAX);
	fixed_point_t result = fix_sub (fix_sub (prio_max , cpu_4), nice_2);
	t->priority = fix_trunc(result);
}

void
update_all_priority (void)
{
	thread_foreach (update_priority, NULL);
}

int
get_max_priority (void)
{
  enum intr_level old_level = intr_disable ();
  struct list_elem *e = list_max (&ready_list, priority_less, NULL);
  intr_set_level (old_level);
  return list_entry (e, struct thread, elem)->priority;
}

int
get_original_priority (void)
{
  struct thread *t = thread_current ();
  return t->original_priority;
}

/* Donating to thread t */
void
donate_priority (struct thread *t, int new_priority)
{
  t->priority = new_priority;
  t->is_donated = true;

  if (t->blocked_by) {
    struct thread *waiting_on = t->blocked_by->holder;
    if (waiting_on && waiting_on->priority < new_priority)
      donate_priority (waiting_on, new_priority);
  }
}

/* Called when releasing a lock, should assume the next highest priority of those waiting
   on current thread, or back to og if none */
void
un_donate_priority (void)
{
  struct thread *t = thread_current ();
  if (list_empty (&(t->locks))) {
    t->priority = t->original_priority;
    t->is_donated = false;
  } else {
      struct list_elem *e;

      int max_priority = -1;

      for (e = list_begin (&t->locks); e != list_end (&t->locks); e = list_next (e)) {
          struct lock *lock = list_entry (e, struct lock, lock_elem);
          int priority = get_highest_waiter (lock);
          max_priority = priority > max_priority ? priority : max_priority;
        }
      t->priority = max_priority > t->original_priority ? max_priority : t->original_priority;
  }
}

/* Project 2 */
bool
init_exit_status(struct thread *t, tid_t tid)
{
  struct exit_status *exit_status;
  exit_status = (struct exit_status *) malloc(sizeof(struct exit_status));
  if (exit_status == NULL) {
    return false;
  }
  exit_status->exit_elem.next = NULL;
  exit_status->exit_elem.prev = NULL;
  t->exit_status = exit_status;
  exit_status->tid = tid;
  exit_status->exit_code = 0;
  exit_status->ref_count = 2;
  lock_init (&(exit_status->ref_lock));
  sema_init (&(exit_status->sema), 0);
  return true;
}

/* Project 3 */
/* Check if fd exists for this thread */
bool
thread_valid_fd(struct thread *t, int fd)
{
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO) {
    return true;
  }
  if(t == NULL || fd < 0) {
    return false;
  }
  /* Check if fd is in fd_table been allocated yet */
  if (fd >= t->fd_table_allocated_size || t->fd_table[fd] == NULL) {
    return false;
  }
  return true;
}

/* Add a file to the fd_table. Returns the fd number associated with the file */
int
thread_add_fd(struct thread *t, struct file *file)
{
  if (t == NULL || file == NULL) {
    exit(-1);
  }
  /* allocate a new fd_table (default size is 8). Return 2 since STDIN and STDOUT taken */
  if (t->fd_table == NULL) {
    t->fd_table_allocated_size = 8;
    struct file **new_fd_table = (struct file **) malloc(8 * sizeof(struct file *));
    if (new_fd_table == NULL) {
      return -1;
    }
    memset (new_fd_table, 0, 8 * sizeof(struct file *));
    t->fd_table_size = 2;
    t->fd_table = new_fd_table;
    t->fd_table[2] = file;
    t->fd_table_size++;
    return 2;
  } else if (t->fd_table_size < t->fd_table_allocated_size) {
    /* Iterate through fd table, allocate first open spot for file*/
    int i;
    for (i = 2; i < t->fd_table_allocated_size; i++) {
      if (t->fd_table[i] == NULL) {
        t->fd_table[i] = file;
        t->fd_table_size++;
        return i;
      }
    }
  } else { /*space in fd_table is full, double the size of the fd_table */
    t->fd_table_allocated_size *= 2;
    struct file **new_fd_table = (struct file **)
                                malloc(t->fd_table_allocated_size * sizeof(struct file *));
    memcpy(new_fd_table, t->fd_table, t->fd_table_size * sizeof(struct file *));
    memset(&new_fd_table[t->fd_table_size], 0, t->fd_table_size * sizeof(struct file *));
    /* free used memory and set new fd_table */
    free(t->fd_table);
    t->fd_table = new_fd_table;
    t->fd_table[t->fd_table_size] = file;
    t->fd_table_size++;
    return t->fd_table_size - 1;
  }
  /* Should never reach here */
  return -1;
}

void
thread_remove_fd (struct thread* t, int fd)
{
  if (!thread_valid_fd (t, fd)) {
    exit(-1);
  }

  t->fd_table[fd] = NULL;
  t->fd_table_size--;
}

struct file *
thread_get_file(struct thread* t, int fd)
{
  if (!thread_valid_fd(t, fd)) {
    return NULL;
  }
  return t->fd_table[fd];
}

/* ------------------------------ Our Code Inserted Above here ---------------------------------- */
