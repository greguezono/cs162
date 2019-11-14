Design Document for Project 1: Threads
======================================

## Group Members

* Gregory Uezono <greg.uezono@berkeley.edu>
* Jeff Weiner <jeffantonweiner@berkeley.edu>
* Alex Le-Tu <alexletu@berkeley.edu>
* Kevin Merino <kmerino@berkeley.edu>

# Task 1.1: Efficient Alarm Clock
   - ## 1. Data structures and functions
      - Adding:
         - `struct list sleep_list`: tracks all threads that are asleep
         - `int wake_time`: thread variable that keeps track of when to wake the thread
      - Modifying:
         - `void timer_sleep (int64_t ticks)`
         - `static void timer_interrupt (struct intr_frame *args UNUSED)`
   - ## 2. Algorithms
      - `timer_sleep()`: putting a thread to sleep
         1. Calculate the wake time using `timer_ticks() + ticks` and set `wake_time` for the current thread
         2. Disable interrupts
         3. Block the current thread, and add thread to `sleep_list`
         4. Enable interrupts if enabled prior 
      - `timer_interrupt()`: waking any sleeping threads if necessary when an interupt occurs
         1. Disable interrupts
         2. Scan the threads in the blocked list, specifically the `wake_time` field and compare to `timer_ticks()`
         3. For any threads for which `timer_ticks() > wake_time`, remove from `sleep_list` and unblock the thread
         4. Enable interrupts if enabled prior
   - ## 3. Synchronization:
      - The only possible race conditions available in this portion will be involved in the list modification of `sleep_list` in `timer_sleep()` and `timer_interrupt()`. To solve the race conditions in both of these functions (prevent two threads from accessing/modifying the lists at the same time, we disable interrupts during modification.
   - ## 4. Rationale: We figured that this would be the best way to handle busy waiting because it hits at a few key points:
      1. We do not modify the existing algorithm for selecting the next thread (i.e. the scheduling can remain unmodified to accomodate busy waiting, making it less prone to coding error), we can simply rely on the current implementation.
      2. We do not have to modify any of the existing structure of threads themselves, rather we can add variables such as`wake_time` and a separate `sleep_list` to help us keep track of sleeping threads.
      3. We figured that interrupts would be used to wake threads if necessary, so through this implementation we would only have to modify `timer_sleep()` and `timer_interrupt()`, though we may choose to add helper functions inside of thread.c to help us accomplish these algorithms - such as `thread_sleep()`, and `thread_wake()` (TBD, upon consultation). 
      4. We did consider alternatives such as modifying the yield function, or perhaps creating a wrapper around it, but this could possibly involve dependencies to other functions that use `yield()` and/or later down the line restrict the correct use of `yield()`. Furthermore, we found after attempting to flesh out this method, there wasn't an easy parallel way to 1) instruct the OS to continue to the next available thread, 2) wake existing threads when the proper time (ticks) has elasped.
      5. We didn't really consider other alternatives outside of this since this seems to get the job done and is relatively concise and simple.
      6. A shortcoming may be that the system would need to scan through the list to find the correct threads to wake. As such we would insert in order (using the `list_insert_ordered` function). This may help to reduce the search complexity, though may increase the modifictation (insertion) complexity. However we figured that this trade-off will be worth it in the longterm use of the OS. This can be changed easily though if we find out otherwise.
      7. Another shortcoming to this is that we do have to disable interrupts and atomically access `sleep_list` which may slow down the system; however, the whole implementation will still have increased time savings by avoiding busy waiting.

# Task 1.2: Priority Scheduler
   - ## 1. Data structures and functions
      - Functions (explained below in Algorithms section):
        - `donate_priority(thread *donor, thread *donee, lock *lock)`: function to handle priority donations
        - `undonate_priority(thread *thread, lock *lock)`: function to handle undoing priority donations
      - Structs:
        - Add `original_priority` integer which represents the priority before donation
        - Add `locks_acquired` ordered list, which for nested locks. Represents an ordered list of locks based on priorities that are currently held by the thread.
        - Add `is_donated` boolean, which represents if priority != original_priority
        - Add `blocked_by` pointer to a list of locks, which represents which locks are blocking the thread
      - Lock struct:
        - Add `lock_priority` integer (initially set to -1)
      - Semaphore struct:
        - Modify waiters list so that it is ordered in descending order based on thread priority
      - Condition struct:
         - Modify waiters list so that it is ordered in descending order based on thread priority
      - 
   - ## 2. Algorithms
      – Choosing the next thread to run
        - We won't modify the `next_thread_to_run` function. Instead keep the `ready_list` sorted which will always remove the next highest priority thread. We will do this by:
          - Every time thread is added to `ready_list`, add it w/ `list_insert_ordered` (sort by descending priority)
          - When a thread is unblocking, sort list using `list_sort` function in list.c
          - Donations explained in next sections
      - Acquiring a Lock
          1. Disable interrupts
          2. `Thread A` tries to acquire Lock 1
          3. **IF** lock -> holder != NULL:
            - **IF** `Thread A priority` > `Lock 1 -> holder priority`:
               - Call `donate_priority` with arguments `Thread A`, `Lock 1-> holder`, `Lock 1`
               - Append `Thread A` to waiters list of Lock 1 -> semaphore struct in order
          5. Block `Thread A`
          6. Enable interrupts
      - Releasing a Lock
         1. Disable interrupts
         2. LockA-> holder is chosen by the scheduler
         3. Release the lock:
         4. Pop off of the next highest priority thread from the waiters list of LockA- -> semaphore struct. Call this `ThreadA`
         5. Call `undonate_priority` function with arguments `ThreadA`,  `LockA`
         6. Enable interrupts
      - Do not iterate over the entire list, instead only check up to a certain point. We calculate this point by 
        utilizing the sleep_time.
      - Computing the effective priority/Changing the thread priority
         - `donate_priority(thread *donor, thread *donee, lock *lockA)`
            - This function is only called if `donor priority` > `donee priority` (i.e. priority of the Thread trying to acquire the lock > lock->holder priority)
            - **IF** `donee->is_donated` is True
               - This implies there are nested locks (i.e. the `donee` thread is already being blocked by another lock)
               - Recall `donee->blocked_by` is a pointer to the lock blocking the current thread
               - **IF** `donee->blocked_by-> priority` <  donor->priority
                  - Go into the lock pointed to by `donee->blocked_by` (we’ll call this lockB)
                  - Go into the thead pointed to by lockB-> holder
                  - Change lockB-> holder->priority = donor-> priority
                  - Set lockB->priority = donor-> priority
                  - **IF** `donee -> is_donated` = True
                     - Repeat the if statement above until you reach the thread that holds the lock
            - **ELSE**:
               - There are no nested locks, so code can just proceed
            - Set `donee-> priority` = donor-> priority
            - Set `lockA> priority` = donor -> priority
            - Set `donee-> is_donated` = True
            - Set `donor->blocked_by` = &lockA
        - `Undonate_priority(thread *threadA, lock *lockA)`
            - Go into lockA->holder thread (recall lockA-> holder is the current thread holder of lockA)
            - **IF** lockA->holder->is_donated = True:
            - **IF** blocked_by is not empty:
            - Set the `lock->holder-> priority` = highest lock->priority in the blocked_by list
ELSE:
Set lock->holder->priority back to its original_priority
Remove lockA from lockA->holder locks_acquired list
Remove lockA from threadA -> blocked_by
Add lockA to threadA->locks_acquired list

      - Priority scheduling for semaphores and locks
         - Keep the semaphores' waiters' list of threads ordered from highest to lowest priority. This will ensure the lock and semaphore pops off the next highest priority thread.
      - Priority scheduling for condition variables
         - Keep the conditions' waiters' list of threads ordered from highest to lowest priority. This will ensure the lock and semaphore pops off the next highest priority thread.
   - ## 3. Synchronization
      - We disable interrupts when acquiring and releasing a lock to not have any synchronization issues. These interrupts are enabled at the end of those calls.
      - When multiple threads try to access the same lock, lock_acquire should handle any race conditions.
      -`donate_priority` should handle the case when a thread with higher priority tries to acquire a lock held by a lower priority thread. If that is the case, `donate_priority` will set the `donee's priority` to the `donor's priority`. The function also handles nested locks by checking if the `is_donated` field is True in the donee thread. The `locks_acquired`, `blocked_by`, `is_donated` fields in the thread struct and `lock_priority` in the lock struct are for handling these nested lock cases. Their usage is outlined in the algorithm above.
      - By changing the waiters' list in the structs to an ordered list, we ensure that the next thread that is unblocked by a lock, semaphore or condition is the next highest priority thread for the scheduler to run.
   - ## 4. Rationale
      - We decided to maintain a priority order by keeping our `ready_list` sorted. This will help us know the relative priorities amongst the threads by selecting from the front of the list. This technique will effectively save time when compared to an implementation that actively searches through the list to find the next higher priority. Another reason as to why we decided to keep order via sorting is that we can reuse the pinto’s sorting list function and the list_insert_ordered operation ensures this will be relatively cheap. 
      - Our Lock Acquiring algorithm prevents race conditions by disabling interrupts so that we can finish the operations before leaving our thread-process. During the acquiring of a lock, we append any thread waiting on the lock in-order. We re-enable interrupts as soon as we set the state variables needed for lock ownership to prevent loss of information from ignoring interrupts. We realized that this was an easy way of maintaining accuracy in our code without having to rely on atomics which would come at a high cost (in hardware). 
      - `Is_donated` inside the thread struct isn’t necessary but we felt it would facilitate donation checking instead of always checking whether `priority` == `original_priority` 
      - We initially didn’t want to keep a `blocked_by` pointer to a lock but found it was necessary for nested locks

   
# Task 1.3:  Multi-level Feedback Queue Scheduler (MLFQS)
   - ## 1. Data structures and functions
      - Adding:
         - thread.h:
            - `int nice` (inside of `struct thread`): represents the nice value of a thread
            - `#define NICE_MIN -20`: minimum niceness a thread may have
            - `#define NICE_MAX 20`: maximum niceness a thread may have
            - `#define NICE_DEFAULT 0`: initial thread starts at this value (others inherit from parent threads)
            - `int recent_cpu` (inside of `struct thread`): represents the recent_cpu value defined below
         - thread.c
            - `static int load_avg`:  estimates the average number of threads ready to run over the past minute
            - `struct list priority_ready_lists`: a list of 64 ready lists, one for each priority value representing threads at different priorities that are ready to run in FCFS order
            - `int mlfqs_priority_calculator`: calculates the new priority for a given thread
            - `void interval_update_load_avg`: updates `load_avg` when called (every second)
            - `void interval_update_recent_cpu`: updates all threads' `recent_cpu` values (every second)
            - `void interval_update_priority`: updates all threads' `priority` values (every second)
      - Modifying:
         - thread.c
            - `int thread_get_nice (void)`: gets the nice value of a thread
            - `void thread_set_nice (int nice UNUSED)`: sets the nice value of a thread
            - `tid_t thread_create (const char *name, int priority, thread_func *function, void *aux)`: might have to modify to accomodate ignoring the set priority (alt would be to calculate priority on the fly
            - `void thread_tick (void)`: modify to update values with ticking
            - `int get_priority`: modify to accomodate calculating via formula
            - `int thread_get_recent_cpu(void)`: modify to accomodate using fix-point values
            - `int thread_get_load_avg(void)`: modify to accomodate using fix-point values
       
   - ## 2. Algorithms:
      - Disable interrupts (if not already)
      - Choosing a thread to run:
         - The scheduler will examine the list of list priorities, `priority_ready_lists`. The outer list contains elements which are lists of threads with the same priority. The outter list is ordered by descending priority. The elements of the outter list (inner lists containing threads) are ordered by FCFS or FIFO. The scheduler will select the highest priority thread(s). If there are multiple it uses round-robin FCFS and selects the first thread to reach that current highest priority to run. This is now the active running thread. From here as the system ticks, the following values are updated accordingly to determine the next thread to run:
      - Restore prior interrupts
      - Updating `priority` for threads:
         - priority = PRI_MAX − (recent_cpu/4) − (nice × 2)
         - PRI_MAX for us is defined to be 63
         - where recent_cpu is defined below
      - Updating `recent_cpu` for threads:
         - recent_cpu is initialized to 0 at boot and is updated in **two** ways:
            - the **current** running thread will increment recent_cpu by 1 for each tick that the thread runs
            - all threads will be recalculated **each second** by the formula below:
               - recent_cpu = (2 × load_avg)/(2 × load_avg + 1) × recent_cpu + nice
               - where load_avg is defined below
      - Updating `load_avg` for system:
         - load_avg is initialized to 0 at boot and is updated in **one** way **each second**:
            - load_avg = (59/60) × load_avg + (1/60) × ready_threads
            - where the ready_threads is the number of threads that are either running or ready to run at time of update
(not including the idle thread). This may be calculated on the fly by counting the ready lists or stored and updated as a static variable in thread.c
   - ## 3. Synchronization:
      - `struct list priority_ready_lists`: concurrent access to this may lead to errors so following the pintos built-in code for the base scheduler, we will be disabling interrupts to ensure single access to the new set of ready lists. We believe this will be suffificient, simialr to in task 1 to ensure concurrency. Since the we have converted a long lengthy list into possibly 64 shorter lists, we hope that the scanning and insertions will not increase time complexity.
      - `static int load_avg`: Since our system only runs one thread at a time this shouldn't be an issue; however, generally updating this variable should only occur once across the system (ex: by one single thread on each second). This could be an issue for example if two threads ran concurrently at time 1 second.
   - ## 4. Rationale
      - Possible alternative would be to calculate priorities on the fly based on time and `recent_cpu` and `load_avg values`
      - We considered this route in order to keep the critical sections small, we have to consider other opportunities to keep critical sections minimal as to not stall the CPU on calculations of values above.
      - Another route we considered was to use locks instead of disabling interrupts directly. We could either lock the entire list of lists `struct list priority_ready_lists`, or we could consider having individual locks on internal lists (representing priorities). This would allow multiple threads with different priorities to update different parts of the overall list, minimizing critical time.
# Task 2.1.2:  Design Document Additional Questions
   ### 1.  Test Case for `sema_up()`:
##### Question:
Consider a fully-functional correct implementation of this project, except for a single bug, which exists in the sema_up() function. According to the project requirements, semaphores (and other synchronization variables) must prefer higher-priority threads over lower-priority threads. However, my implementation chooses the highest-priority thread based on the base priority rather than the effective priority. Essentially, priority donations are not taken into account when the semaphore decides which thread to unblock. Please design a test case that can prove the existence of this bug. Pintos test cases contain regular kernel-level code (variables, function calls, if statements, etc) and can print out text. We can compare the expected output with the actual output. If they do not match, then it proves that the implementation contains a bug. You should provide a description of how the test works, as well as the expected output and the actual output.
##### Description of how the test works:
First we will add `printf` statements to the schedule to print the selected thread, `printf` statements to lock aquisitions, `printf` statements to lock releases, and `printf` statements to blockages. Then we generate a test case as follows.
   - Initalize 3 threads: A, B, and C with priorities 10, 20, and 30 respectively
   - Initialize Lock X
   - Thread A attempts to aquire Lock X
   - Thread C attempts to aquire Lock X
   - We then observe how the scheduler chooses which thread to run.
   
If performing correctly, we should observe that Thread A holds Lock X, while Thread C is placed on the waiting list for Lock X.
From here, we know that Thread C has higher priority than Lock A, so we should observe the scheduler choosing to run Thread A instead of B in order to free up the Lock and allow Thread C to run as soon as possible. However, if our system does not take into account effective priorities, it will choose to run Thread B since B has a higher **base** priority than A:

   - Assume from a prior operation thread A is currently running (perhaps it had a higher priority previously then shifted down after aquiring Lock X), and aquires Lock X.
   
   - Thread A base priority 10 | Effective priority 10 | Holding Lock X
   - Thread B base priority 20 | Effective priority 20 |
   - Thread C base priority 30 | Effective priority 30 |
   
   - Scheduler now chooses Thread C (highest priority) and attempts to aquire Lock X. Since Thread A has already aquired Lock X, Thread C is placed on Lock X's waiting list, and Thread C is blocked.
   
   - Thread A base priority 10 | Effective priority 30 | Holding Lock X
   - Thread B base priority 20 | Effective priority 20 |
   - Thread C base priority 30 | Effective priority 30 | Waiting for Lock X (Blocked)
   
   - Scheduler now (should) choose Thread A in order to free up Lock X for thread C.
   
##### Expected Output:
   - `Selected thread A`
   - `Thread A aquires Lock X`
   - `Selected thread C`
   - `Thread C blocked`
   - `Selected thread A`
##### Actual Output:
   - `Selected thread A`
   - `Thread A aquires Lock X`
   - `Selected thread C`
   - `Thread C blocked`
   - `Selected thread B`
   
   ### 2.  MLFQS scheduler:
##### Question:
Suppose threads A, B, and C have nice values of 0, 1, and 2 respectively. Each has a recent_cpu value of 0. Fill in the table below showing the scheduling decision and the recent_cpu and priority values for each thread after each given number of timer ticks. We can use R(A) and P(A) to denote the recent_cpu and priority values of thread A, for brevity. 
   
##### Relevant Formulas:
- priority = PRI_MAX − (recent_cpu/4) − (nice × 2)
   - PRI_MAX = 63
- recent_cpu:
   - initialized to 0
   - On each timer tick, the running thread’s recent_cpu is incremented by 1
   - recalculated for all threads once per second (100 ticks - won't be updated via this method in this example) using:
      - (2 × load_avg)/(2 × load_avg + 1) × recent_cpu + nice
- load_avg = (59/60) × load_avg + (1/60) × ready_threads
   - initialized to 0
   - updates once every second (100 ticks - won't be updated in this example)
   
##### Answer:
Assumed 100 ticks per second as in the spec.

timer ticks | R(A) | R(B) | R(C) | P(A) | P(B) | P(C) | thread to run
------------|------|------|------|------|------|------|--------------
 0          |    0 |    0 |    0 |   63 |   61 |   59 | A
 4          |    4 |    0 |    0 |   62 |   61 |   59 | A
 8          |    8 |    0 |    0 |   61 |   61 |   59 | B
12          |    8 |    4 |    0 |   61 |   60 |   59 | A
16          |   12 |    4 |    0 |   60 |   60 |   59 | B
20          |   12 |    8 |    0 |   60 |   59 |   59 | A
24          |   16 |    8 |    0 |   59 |   59 |   59 | C
28          |   16 |    8 |    4 |   59 |   59 |   58 | B
32          |   16 |   12 |    4 |   59 |   58 |   58 | A
36          |   20 |   12 |    4 |   58 |   58 |   58 | C

   ### 3. MLFQS scheduler (cont.):
##### Question:
Did any ambiguities in the scheduler specification make values in the table (in the previous question) uncertain? If so, what rule did you use to resolve them?

##### Answer:
Ambiguities: There were two ambiguities in the question above. The first being what to do when we have tied highest priorities. For this we used the FCFS round robin rule from lecture. The first thread to reach that highest priority seen currently gets to go first, and so on.

The second ambiguity is the timer frequency, or how many ticks there are in a second. For this, we just used what is built into our spec we are using (100 ticks every second). In this case load average does not change from the initial value of 0, and recent_cpu is not updated for every thread on the first second (i.e. in the example above, recent_cpu only changes on the active running thread through incremental timer ticks.

timer ticks | Ambiguity
------------|-----------
 8          | Choosing between A (61) and B (61)
16          | Choosing between A (60) and B (60)
24          | Choosing between A (59), B (59), and C (59)
28          | Choosing between A (59) and B (59)
36          | Choosing between A (58), B (58), and C (58)
