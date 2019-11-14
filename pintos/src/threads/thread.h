#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
#include "threads/fixed-point.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
/* Project 1 */
    int wake_time;                    /* Wake time, set during timer_sleep, set to -1 initially */
    struct list_elem sleep_elem;      /* List element for recording in the sleeping list */
    fixed_point_t recent_cpu;         /* MLFQS recent cpu quantity */
    int nice;                         /* MLFQS nice quantity */
    struct list locks;                /* list of locks currently held by the thread */
    int original_priority;            /* Original priority of the thread before any donations */
    int is_donated;                   /* Boolean indicating whether current priority is donated */
    struct lock *blocked_by;          /* Pointer to the lock currently blocking the thread */

/* Project 2 */
    struct exit_status *exit_status;  /* Exit status of this thread */
    struct list children;             /* List of child processes' exit status */
    struct lock children_lock;        /* Lock on child exit status list  */
    struct file *executable_file;     /* Used to deny/reallow writes to executable files */

/* Project 3 */
    struct dir *curr_dir;
    struct file **fd_table;           /* File descriptor table for this thread */
    int fd_table_size;                /* Current number of files in the fd table */
    int fd_table_allocated_size;      /* Current allocated size for the fd table */
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
  /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
/* Project 2 */
struct load_status {
  struct semaphore sema;
  char *cmdline;
  bool loaded;
  struct thread *parent_t;
};

struct exit_status {
  tid_t tid;                          /* Child thread id */
  int exit_code;                      /* Child exit code */
  int ref_count;                      /* Number of active threads pointing to this struct */
  struct lock ref_lock;               /* Protects ref_count */
  struct semaphore sema;              /* Used by parent to sleep and by child to wake up parent */
  struct list_elem exit_elem;         /* Children exit status list elem */
};
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
/* Project 1 */
void thread_sleep (int64_t ticks);
void thread_wake (void);
void update_load_avg (void);
void update_recent_cpu (struct thread *t, void *aux);
void update_all_recent_cpu (void);
bool priority_less (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void update_priority (struct thread *t, void *aux);
void update_all_priority (void);
int get_max_priority (void);
int get_original_priority (void);
void donate_priority (struct thread *t, int new_priority);
void un_donate_priority (void);

/* Project 2 */
bool init_exit_status(struct thread *, tid_t);

/* Project 3 */
bool thread_valid_fd(struct thread *, int);
int thread_add_fd(struct thread*, struct file *);
void thread_remove_fd(struct thread*, int);
struct file *thread_get_file(struct thread *, int);
void directory_init(void);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

#endif /* threads/thread.h */
