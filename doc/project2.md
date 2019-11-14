Design Document for Project 2: User Programs
============================================

## Group Members

* Gregory Uezono <greg.uezono@berkeley.edu>
* Jeff Weiner <jeffantonweiner@berkeley.edu>
* Alex Le-Tu <alexletu@berkeley.edu>
* Kevin Merino <kmerino@berkeley.edu>

## 1.1 Task 1: Argument Passing

#### Data structures and functions
  
  * **Adding to process.c**
    * `void parse_cmd_line (char *cmd_line, char **stack_ptr)` parses cmd_line storing args into the stack pointer
    * (method 1) `void count_bytes_and_args (char *cmd_line, &int bytes, &int argc)` counts bytes necessary to store argc arguments from cmd_line
    * (method 2) `char* tokenize_backwards (char *string, char *delims)` tokenizes input string in reverse order
  
  * **Modifying in process.c**
    * `bool load (const char *file_name, void (**eip) (void), void **esp)` Modify to parse args
    * `tid_tid_t process_execute(char *file_name)` Modify to parse args
    * `static void start_process (void *file_name_)` Add a semaphore for synchronization from parent process 
  
  * **Modifying in exception.c**
    * (method 2) `static void page_fault (struct intr_frame *)` May need to modify for overflow page from args

#### Algorithms
  * Method 1: Forward scan parse starting `bool load (const char *file_name, void (**eip) (void), void **esp)`
    1. call `void parse_cmd_line (char *cmd_line, char **stack_ptr)` to set the stack up:
    2. `parse_cmd_line` will call `count_bytes_and_args (char *cmd_line, &int bytes, &int argc)`. Bytes represents how many bytes are required to store all cmd args as individual null-terminated strings. Then calculate the space required to store `(argc + 1)` pointers (last should be `NULL`), add this value with `bytes` and call this `arg_beginning`.
    3. Check to see if we would run out of space on stack with these args.
    4. Move the stack ptr down `bytes` values, this should leave room for the `argc` above .
    5. Using the pointer at the base address + `bytes` use `strtok_r (char *s, const char *delimiters, char **save_ptr)` to copy tokens from the cmd_line, to the stack. We then increment the next pointer by `str_length(token)`. Repeat until the `cmd_line` has been processed. 
    6. From this new pointer (which is now below the individual strings), create pointers to the above strings. Can do this with base pointer + `bytes` and add 4 each time. Because we will be doing this many times it is beneficial to define a function that does this for us. **Note** We may have to have a fill byte between these two in order to preserve word alignment. 
    7. Finally we return to `arg_beginning - 4` (right below all strings and arg pointers). Here we write 4 bytes addressed to `arg_beginning`, this is `argv` for the process.
    8. We decrement again by 4 and write 4 bytes, `argc` indicating `argc` for the process and set the return address. 
    
  * Method 2: Backward scan parse starting in `bool load (const char *file_name, void (**eip) (void), void **esp)` 
    1. All changes are the same as method 1 except we parse backward through `cmd_line`
    2. Instead of counting the space required before hand. Use `char* tokenize_backwards (char *string, char *delims)` inserting strings at the base of the stack and decrementing downards.
    3. Once `cmd_line` has been processed, the reset follows as method 1. However, we may have to modify `page_fault` if we go out of bounds on the stack at any point during processing.
    
#### Synchronization
  1. We have to ensure that the parent process spawing this new one does not resume execution until after it is sure that the child process has successfully loaded. To solve this the process calling `process_execute` can use a semaphore while calling `process_execute`. The child can then release this in `start_process` _after_ calling `load`. 
  2. In Pintos, process_execute creates a process in a separate address space, so any subsequent calls to process_execute should be safe from any race conditions/shared addresses.
	3. `thread_create` disables interrupts
  4. Not that this should be a problem due to 1. but `strtok_r` is interesting because successive calls from multiple sources are safe because we are able to save the context of the current string being tokenized in its `char **save_ptr`.

#### Rationale
- Naming the thread after the executable file seemed to be the most logical choice as the command could be easily accessible in other function calls.
- After looking at page 16 in the spec we figured this would be the best way to allocate the space on the stack and field the values while parsing the data at the same time. Since we need to start at the bottom of the user stack and parse arguments up the user stack, we figured the cleanest way is to use the library fn strtok_r and scan forward since we would know in advance how much space to allocate, where to start in the stack, and if the user stack would exceed the given page size. This optimization, however, includes more overhead due to having to scan twice over the cmd_line. We ultimately chose this implementation as  the kernel should always be as safe as possible when avoiding overflows and malicious users.
- We believe that method 1 is the cleanest implementation because it takes advantage of a library function `strtok_r` rather than having us build our own tokenizer. In addition while we would have to do two passes over `cmd_line`, the time complexity is still linear and `cmd_lines` (under assumption) should not exceed a page in size. 
- We can use macros for a lot of these repeated operations (also in task 2 and 3 for syscalls) such as loading 4 bytes on and off the stack.

## Task 1.2: Process Control Syscalls
#### Functions and Structs:

  * **Adding to thread.h**
     * (add to `struct thread`)	: `struct exit_status` contains info about exit status of current thread.
     * (add to `struct thread`)	: `struct list child_exit_statuses` contains info about exist status for each child of this process.
     * `struct exit_status {`  
		* `tid_t tid`		: for bookkeeping, tracks the tid for this exit status
		* `int exit_status`	: exit status number
		* `list_elem exit_elem`	: list element for `child_exit_statuses`
		* `int ref_count`		: contains how many threads/processes are utilizing this `struct exit_status`. Initialized to 2.
		* `struct lock ref_lock`	: lock to ensure no race conditions while changing `ref_count`
		* `struct semaphore sema`	: semaphore used by parent to `wait`
		`}`
   * **Modifying thread.c**
     * `static void init_thread (struct thread *t, const char *name, int priority)` modify to initialize new parameters above
     * `void thread_exit (void)
   * **Adding to syscall.c**
     * `bool check_address (int start, int size)` returns true if input address and end are within valid virtual memory bounds
     * `int practice (int i)`
     * `void halt (void)`
     * `void exit (int status)`
     * `pid t exec (const char *cmd_line)`
     * `int wait (pid t pid)`
   * **Modifying syscall.c**
     * `static void syscall_handler (struct intr_frame *f UNUSED)` Modify to accomodate other syscalls
   * **Modifying process.c**
     * `int process_wait (tid_t child_tid UNUSED)`
     * `void process_exit (void)`
   
#### Algorithms:
   * **`syscall_handler(struct intr_frame *f)`**
      1. Use a switch to determine which syscall to perform
      2. Once we find the correct syscall, use `check_address` to check arguments are in valid memory from `f->esp`
      3. Obtain necessary arguments per syscall that was selected
      4. If there is a return value from the selected syscall `f->eax =` return value of syscall.
   * **`int practice (int i)`**
      1. return i + 1
   * **`void halt (void)`**
      1. immediately call `shutdown_power_off()` from shutdown.h
   * **`void exit (int status)`**
      1. call `thread_exit ()` (by proxy calls `process_exit` see below)
   * **`pid t exec (const char *cmd_line)`**
      1. use `check_address` of `cmd_line`
      2. call `process_execute (cmd_line)`
      3. if all successful return the new pid, fail, exit with status -1
   * **`int wait (pid t pid)`**
      1. calls `process_wait` (shown from step 2 onward)
      2. The thread should check its `list struct child_exit_statuses` examining each child `pid` and make sure one matches `pid` return -1 if not found
      3. Once found, check that we are not waiting on this `pid` already (however, this shouldn't happen anyways since if we are waiting, we should be blocked.
      4. Once we obtain the correct `struct exit_status`, `es`, the thread should `semadown (es->sema)`
      5. Get the exit code `es->exit_status` call this `exit_status`
      6. Remove `es` the `struct exit_status` from `child_exit_statuses`. Then free `es`.
      7. Update any wait status if necessary
   * **`void process_exit (int status)`** (These following parts are added to the current `process_exit` implementation:
      1. Acquire Lock on `ref_count`, then decrement `ref_count` inside of its `struct exit_status` call this `es`. Finally release the lock on `ref_count`.
      2. update the thread's `exist_status` inside of `es`.
      3. check if `ref_count == 0` (locked), if it is then the thread is the terminating thread utilizing this `struct`. This occurs when the parent terminates before the current one. If so, free this struct.
      4. if `ref_count != 0` (locked) then `semaup (es->sema)` (unblocks waiting parent if any).
      5. For each child `struct exit_status` (call this `child_es`) in `child_exit_statuses`:
        * Acquire lock: `lock_acquire (child_es->ref_lock)`
      	* Decrement `child_es->ref_count` by 1
	* Release lock: `lock_release (child_es->ref_lock)`
	* If `child_es->ref_count == 0` (This occurs when the child terminates before the current one), then free the `struct exit_status child_es`.
      6. The rest of this function can stay the same.
      
#### Synchronization:
   1. One race condition is created when a parent process waits on a child to terminate. This is due to the fact that they must know when they terminate relative to each other to free the `struct exit_status` that connects them. They communicate via `ref_count` which lets them know how many threads are utilizing a given `struct exit_status`. When one terminates, it should decrement this value.  In order to prevent the race condition on `ref_count` we place a lock on it.
   2. Since one of the calls is `exec` and spawns a new process, we have to make sure that the calling process waits until the child is loaded; however, this is handled in task 1.
#### Rationale:
   1. For all but the `wait` syscall the operations were pretty standard because they just called the respective functions.
   2. For `wait` we needed a way for threads to communicate exit statuses between parents and child processes. We added an exit_status struct in order to do this. The parent will have a list of all its children's exit statuses in order to successfully wait. However, this added additional issues of exiting. The exit status struct needs to be present regardless of which process terminates first the parent or the child. In the natural cases when the Parent P waits on a Child C (even exited C), we are able to free the struct as soon as we finish waiting. In the cases where there is no waiting yet we have either P terminates before C or C terminates before P and we must free these structs upon exit. If P terminates after C terminates, we may still need the exit status if we call wait, so P is in charge of freeing the exit struct. If C terminates after P terminates, C may still need access to the exit struct so it will be in charge of freeing the struct. In order to implement this duality, we modified the `process_exit` function and utilize `ref_count` in order to see how many processes are utilizing a given exit struct. We add a lock onto `ref_count` to make sure there is no race condition while modifying it.

## Task 1.3: File Operation Syscalls
#### Functions and Structs:
- lock file_lock
- `struct file_descriptor`
	- int fd
	- struct file *file
	- struct list_elem elem
- `struct list fd_table` 
	- a list of file_descriptors representing the fd_table
	- ordered by fd numbers
- check_address (same as task 2)
- `bool create (const char *file, unsigned initial size)`
- `bool remove (const char *file)`
- `int open (const char *file)`
- `int filesize (int fd)`
- `int read (int fd, void *buffer, unsigned size)`
- `int write (int fd, const void *buffer, unsigned size)`
- `void seek (int fd, unsigned position)`
- `unsigned tell (int fd)`
- `void close (int fd)`

#### Algorithms:
- `bool create (const char *file, unsigned initial size)`
	- acquire file_lock
	- bool ret_status = `filesys_create(file, size)`
	- release file_lock
	- return ret_status
- `bool remove (const char *file)`
	- acquire file_lock
	- bool ret_status = `filesys_remove(file)`
	- release file_lock
	- return ret_status
- `int open (const char *file)`
	- acquire file_lock
	- call `filesys_open(file)`
	- create an fd struct for the file, assign the lowest fd_num available for the file, add the file to the fd table
	- release file_lock
	- return the fd number
- `int filesize (int fd)`
	- acquire file_lock
	- look up the fd on the fd table and get the file
	- size = `file_length(file)`
	- release file_lock
	- return size
- `int read (int fd, void *buffer, unsigned size)`
	- acquire file_lock
	- look up the fd on the fd table and get the file
	- size_read = `file_read(file, buffer, size)`
	- release file_lock
	- return size_read
- `int write (int fd, const void *buffer, unsigned size)`
	- acquire file_lock
	- look up the fd on the fd table and get the file
	- size_written = `file_write(file, buffer, size)`
	- release file_lock
	- return size_written
- `void seek (int fd, unsigned position)`
	- acquire file_lock
	- look up the fd on the fd table and get the file
	- `file_seek(file, position)
	- release file_lock
	- return 
- `unsigned tell (int fd)`
	- acquire file_lock
	- look up the fd on the fd table and get the file
	- `file_tell(file)`
	- release file_lock
	- return ret_status
- `void close (int fd)`
	- acquire file_lock
	- look up fd on the fd table and get the file
	- `file_close(file)`
	- remove the file_descriptor struct from the fd_table
	- free the file_descriptor struct
	- release file_lock
	- return ret_status
	

#### Synchronization:
- acquire a lock before any file system operation and release it after it is done. This ensures no more than 1 thread will modify a file at a time.

#### Rationale:
- We realized Pintos has a filesystem but no file descriptor table or anything similar. We decided to implement this in the syscall.c file for now as we were told in the spec to not change anything in filesys/ directory for now. This might change in Project 3.
- We considered adding a file to the fd_table when it was created, but realized that a file should only occupy the fd table when open is called.

## 2.1.2 Design Document Additional Questions
   ### 1.   Test Case for invalid stack pointer:
##### Question:
Take a look at the Project 2 test suite in pintos/src/tests/userprog. Some of the test cases
will intentionally provide invalid pointers as syscall arguments, in order to test whether your implementation safely handles the reading and writing of user process memory. Please identify a test case that uses an invalid stack pointer ($esp) when making a syscall. Provide the name of the test and explain how the test works. (Your explanation should be very specific: use line numbers and the actual names of variables when explaining the test case.)
#### Description of sc-bad-sp: 
The test case sc-bad-sp.c in pintos/src/tests/userprog was identified as a test case that uses an invalid stack pointer ($esp) when making a syscall. 
The problem with this test is in line 18, and it works by moving the stack pointer ($esp) to the address -2^(26) = -0x0400 0000 = 0xF (1011) FF FFFF + 1 = 0xF (1100) 00 0000.
According to the spec the top of the stack at PHYS_BASE, which is an address lower than this address. 
This will force us to move the stack pointer to the outside of the stack space, invalidating it. The next assembly line calls an interrupt with code 0x30.

This test works by setting the stack pointer to this invalid address and setting up the interrupt. This interrupt is later picked by syscall_handler within pintos/src/userprog/syscall.c which then dereferences the stack pointer, line 20. 
A return value of -1 should then follow. 

   ### 2.   Test Case for edge case stack pointer:
##### Question:
Please identify a test case that uses a valid stack pointer when making a syscall, but the stack pointer is too close to a page boundary, so some of the syscall arguments are located in invalid memory. (Your implementation should kill the user process in this case.) Provide the name of the test and explain how the test works. (Your explanation should be very specific: use line numbers and the actual names of variables when explaining the test case.)
#### Description of sc-bad-arg: 
The test case sc-bad-arg.c in pintos/src/tests/userprog was identified as a test case that uses a valid stack pointer but has the stack pointer too close to the page boundary. The problem with this test is in line 20, where it sets the stack pointer to address 0xbffffffc. This in itself is not a problem since it is a valid address. However, it then sticks a system call SYS_EXIT which ends in the very top of the stack. This test will then trigger an interrupt which will again be handled by the syscall_handler. This time, line 20 will execute with no issue, since we have a valid stack pointer. On line 21, however, we place an args[1], equivalent to 4($esp) which will result in ($esp) + 4 bytes pushing the stack to the top of the user address space. 
A return value of -1 should then follow.

   ### 3.   Adding a test case:
##### Question:
The current pintos test suit does not provide us with any test cases for checking if our remove implementation works correctly. There are no tests to check if multiple threads attempt to remove the same file. For this test, we could:
 **create a unique text file** and **fork** our process.
In the **child process**, we can **call remove** on this newly made file. 
We then **return the remove’s return value** within the exit code of the child process.
Next, we attempt to remove the file again within the parent process. 
We **evaluate both return status** to ensure the child succeeded, and the parent failed. 

### 4. GDB Question:

1. The thread running this function is called “main” and it has address 0xc000e000. There is another thread present called “idle” with address 0xc0104000. 

    * Main struct thread: {tid = 1, status = THREAD_RUNNING, name = "main", '\000' <repeats 11 times>, stack = 0xc000ee0c "\210", <incomplete sequence \357>, priority = 31, allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}

    * Idle struct thread: {tid = 2, status = THREAD_BLOCKED, name = "idle", '\000' <repeats 11times>, stack = 0xc0104f34 "", priority = 0, allelem = {prev = 0xc000e020, next = 0xc0034b58 <all_list+8>}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}


2. #0  process_execute (file_name=file_name@entry=0xc0007d50 "args-none") at ../../userprog/process.c:32

    * #1  0xc002025e in run_task (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:288
    * #2  0xc00208e4 in run_actions (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:340
    * #3  main () at ../../threads/init.c:133


3. The thread running the start_process function is called “main” and located at address 0xc000e000. The other threads are “idle” and “args-none\000\000\000\000\000\000.” These are the struct threads:

    * MAIN: {tid = 1, status = THREAD_BLOCKED, name = "main", '\000' <repeats 11times>, stack = 0xc000eebc "\001", priority = 31, allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020}, elem = {prev = 0xc0036554 <temporary+4>, next = 0xc003655c <temporary+12>}, pagedir = 0x0, magic = 3446325067} 

    * IDLE {tid = 2, status = THREAD_BLOCKED, name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0, allelem = {prev = 0xc000e020, next = 0xc010a020}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}

    * ARGS-NONE  {tid = 3, status = THREAD_RUNNING, name = "args-none\000\000\000\000\000\000", stack = 0xc010afd4 "", priority = 31, allelem = {prev = 0xc0104020, next = 0xc0034b58 <all_list+8>}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}


4. The thread running start_process is created at line 45 of process.c.

    * Code: tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);


5. The page fault was caused at address  0x0804870c.


6. #0  _start (argc=<error reading variable: can't compute CFA for this frame>, argv=<error reading variable: can't compute CFA for this frame>) at ../../lib/user/entry.c:9


7. This error occurs because the arguments are not set on the stack, because we have not implemented it yet. Even though its args-none, the stack should show argv and argc, but they are not available with the lack of implementation.

