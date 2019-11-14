Final Report for Project 2: Threads
===================================

## Group Members

* Gregory Uezono <greg.uezono@berkeley.edu>
* Jeff Weiner <jeffantonweiner@berkeley.edu>
* Alex Le-Tu <alexletu@berkeley.edu>
* Kevin Merino <kmerino@berkeley.edu>

## Changes in initial design doc:

- **Task 1**: 
  - Following our TA’s (Jonathan) advice, we decided to go with method 1, which involved counting ahead of time the `argc` and number of bytes necessary to store all of the `argv` on the stack. We then moved the stack pointer down and scanned forward through `cmdline` to parse arguments onto the stack.
  - In addition, we created a `struct load_status` in order to pass information from a parent to child process. This includes whether or not the child process successfully loaded (different than being created), and it provides a semaphore to ensure synchronization - the parent should wait until the child fully loads (or fails). 
  
- **Task 2**: 
  - Following our TA’s (Jonathan) advice, we decided to malloc the exit struct since we didn’t want to keep the whole thread around just for accessing the exit struct. The rest of the implementation closely followed our original design doc and section 7 worksheet.

- **Task 3**: 
  - We decided to follow our original design doc and keep a file_lock for file accesses for now, as well as a global fd table. We realize this is inefficient, and we plan on fixing these two issues for Project 3. 
  - Our initial design doc also didn’t account for denying writes to executable files and allowing them after the thread has finished executing. We added an additional `file *executable_file` in our `thread struct` to keep track of this file (in thread.h). Writes are denied when `process_execute` is called and reenabled when `process_exit` is called. 
  - We also realized that our file descriptor struct (`fd_struct`) must take up too much space as we were unable to pass the `multi-oom.c` test. We will most likely completely change our current file descriptor table, file descriptor, and file system operations for Project 3 so we decided not to worry about that for now.


## **Reflection on the Project:**
  - We all worked on all tasks and finished the project together. We coded individually for initial implementations of each Task and used paired programming to debug the tasks once we had working implementations. Jeff and Kevin implemented the two Student Tests.
  - We finished Tasks 1 and 3 early for the Code Milestone and procrastinated on Task 2 as we thought it would be easy. We managed to get all tests passing locally (except for multi-oom) when we ran `make check`; however, we ran into an issue where the autograder would output `Run didn't produce any output` for all tests. This took hours to debug and we found it was due to us waking up the parent thread (on the `load_status struct` before correctly setting up the `exit_status`). For Project 3 we will start earlier so we don't need to use a slip day on a small detail.
  
  ## **Student Testing Report:**
  
  - **wait-not-child**:
  
    - This test is similar to the wait-bad-pid test, except instead of a fully invalid pid, we check that a process will error upon attempting to wait on a pid that is valid, but not the child of the waiting process. 
    - This test works by executing a file which spawns a new process that gets caught in an infinite loop, to ensure that it does not terminate before the next step occurs. Then, we spawn another process, which executes a file taking in the pid from the looping process, and then attempts to wait on it. The wait should fail, returning exit code -1, ending the test. 
    - The expected output of the test should include a begin statement for the original test, then a message that indicates that the child-loop is running, a message from the waiting process stating that the wait failed (status = -1) along with its exit code, and then the wait-not-child end statement and exit code. This ensures that the child-loop is still active while the other process attempts to wait and does not exit preceding the wait attempt.
    - OUTPUT: 
    ````
      - Copying tests/userprog/wait-not-child to scratch partition...
      - Copying tests/userprog/child-loop to scratch partition...
      - Copying tests/userprog/wait-helper to scratch partition...
      - qemu -hda /tmp/owRX3PuhvJ.dsk -m 4 -net none -nographic -monitor null
      - PiLo hda1
      - Loading...........
      - Kernel command line: -q -f extract run wait-not-child
      - Pintos booting with 4,088 kB RAM...
      - 382 pages available in kernel pool.
      - 382 pages available in user pool.
      - Calibrating timer...  421,888,000 loops/s.
      - hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
      - hda1: 177 sectors (88 kB), Pintos OS kernel (20)
      - hda2: 4,096 sectors (2 MB), Pintos file system (21)
      - hda3: 297 sectors (148 kB), Pintos scratch (22)
      - filesys: using hda2
      - scratch: using hda3
      - Formatting file system...done.
      - Boot complete.
      - Extracting ustar archive from scratch device into file system...
      - Putting 'wait-not-child' into the file system...
      - Putting 'child-loop' into the file system...
      - Putting 'wait-helper' into the file system...
      - Erasing ustar archive...
      - Executing 'wait-not-child':
      - (wait-not-child) begin
      - (child-loop) running
      - (wait-helper) status = -1
      - wait-helper: exit(-1)
      - (wait-not-child) end
      - wait-not-child: exit(0)
      - Execution of 'wait-not-child' complete.
      - Timer: 81 ticks
      - Thread: 30 idle ticks, 38 kernel ticks, 13 user ticks
      - hda2 (filesys): 204 reads, 602 writes
      - hda3 (scratch): 296 reads, 2 writes
      - Console: 1084 characters output
      - Keyboard: 0 keys pressed
      - Exception: 0 page faults
      - Powering off...
    ````
    - RESULT: 
        - PASS
        
    - Potential errors
      - If your kernel had a bug where upon creating a new process, the current process' children were copied over to the new process, this would potentially cause the process attempting to wait to successfully wait, printing out a nonnegative status number, and causing the test to wait indefinitely, outputing nothing (or timing out). 
      - If the kernel has a bug in which the wait call fails but does not exit correctly, the original process would get caught waiting on it, as the outer process must wait for its second spawned child to complete before terminating, so the test would once again wait indefinitely. 
      
- **remove-used**:
  - The test remove-used is used to test that the remove function will not allow for a file currently being used to be deleted. In this test we are testing   the file synchronization accross threads as well as the permissions as it pertains to deleting other thread’s files. 
  - The test remove-used opens a file (sample.txt) and executes another C program, remove-helper. If we aren’t able to open the file then we fail the test.   Assuming this stage works, the main program will begin writting to the file a buffer from another file (sample.inc) so that the it is busy utilizing      the file and accessing memory for a short period of time. Concurerntly, we expect the remove-helper to remove the file, which should fail by returning    a -1. The output of this program should be minimal as most msg were used to flag errors. If the open method fails, we msg “fail” else we continue and     open the helper method. We expect the main program to return with exit(0) to indicate no issues occurred. The concurrent program should return -1         indicating that the value of removing was -1. 
  - OUTPUT:
    ````
    - Copying tests/userprog/remove-used to scratch partition...
    - Copying ../../tests/userprog/sample.txt to scratch partition...
    - Copying tests/userprog/remove-helper to scratch partition...
    - qemu -hda /tmp/PH6ldqMrmA.dsk -m 4 -net none -nographic -monitor null
    - PiLo hda1
    - Loading...........
    - Kernel command line: -q -f extract run remove-used
    - Pintos booting with 4,088 kB RAM...
    - 382 pages available in kernel pool.
    - 382 pages available in user pool.
    - Calibrating timer...  209,510,400 loops/s.
    - hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
    - hda1: 177 sectors (88 kB), Pintos OS kernel (20)
    - hda2: 4,096 sectors (2 MB), Pintos file system (21)
    - hda3: 203 sectors (101 kB), Pintos scratch (22)
    - filesys: using hda2
    - scratch: using hda3
    - Formatting file system...done.
    - Boot complete.
    - Extracting ustar archive from scratch device into file system...
    - Putting 'remove-used' into the file system...
    - Putting 'sample.txt' into the file system...
    - Putting 'remove-helper' into the file system...
    - Erasing ustar archive...
    - Executing 'remove-used':
    - (remove-used) begin
    - (remove-used) end
    - remove-used: exit(0)
    - remove-helper: exit(-1)
    - Execution of 'remove-used' complete.
    - Timer: 62 ticks
    - Thread: 30 idle ticks, 31 kernel ticks, 1 user ticks
    - hda2 (filesys): 173 reads, 418 writes
    - hda3 (scratch): 202 reads, 2 writes
    - Console: 1019 characters output
    - Keyboard: 0 keys pressed
    - Exception: 0 page faults
    - Powering off...
    ````
  - RESULT: 
    - PASS
  - Potential Errors: 
    - A potential kernel bug that would be caused if our kernel allowed another thread to remove a file while it’s being written to would be writting/          reading to an invalid portion of memory. This could be really destructive specially with other processes requesting memory. Instead our kernel should     fail any call from a thread removing another’s resources.
    - Another potential kernel bug thatt would be caused if our kernel didn’t fail a removing call would be the deletion of executable files while being        utilized. This would could also cause harmful damage to the kernel as it can be placed in a corrupt state of execution where we lose the previously       saved registeres and thus lose the memory from previous caller. Instead our kernel should be checking for this condition and blocking any call            attempting this.
  - We thought writing tests for Pintos was initially complicated because the Makefile was very confusing to navigate. Once we got the make file                configuration we were able to build the tests fairly fast. It was really helpful to have so many tests to resort to when writing our own that
  way we could model our tests in the same style. The Pintos testing system is very useful and could benefit from variable outputs. It would be helpful
  to be able to write a few alternative valid solutions, specially when testing concurrent programs. When writing these test cases we learned a lot
  about MakeFiles (some of us didn't have much experience with them). We also ran into some kernel panics while running our tests which was helpful
  in debugging larger bugs in our project tasks.
