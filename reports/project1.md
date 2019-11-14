Final Report for Project 1: Threads
===================================

## Group Members

* Gregory Uezono <greg.uezono@berkeley.edu>
* Jeff Weiner <jeffantonweiner@berkeley.edu>
* Alex Le-Tu <alexletu@berkeley.edu>
* Kevin Merino <kmerino@berkeley.edu>

## Changes in initial design doc

  - For Task 1, 2, and 3 the code was very similar to how we designed it and we were able to implement most of our design spec with little modifications. Some of these modifications were inspired from our meeting with our TA, Jonathan Murata. We (the group and Jonathan) decided that sorting our lists -since we decided to keep sleeping thread references in a list- would be resource costly. Building on this suggestion we decided that utilizing the max function when selecting the next thread to run would prevent from having to re-sort after donations changed the **efective** priorities. 
  
  - Task 1: We ended up moving most of the logic into the thread.c file in order to tidy up our code and keep functionality where it belongs. In other words since the timer.c file should worry only about timings, on sleep it should call the thread's 'sleep' function and on an interrupt, it should call the 'wake_threads' function to wake up any threads that should wake up. The logic however for these has stayed the same since the original function.
  
  - Task 2: We changed a few more things here but the overall structure remained the same. We maintain a list of locks held by a thread - so in the 'lock_aquire' method, we we add a lock to the list (if we can aquire the lock) else we set the 'blocked_by' to the lock. Upon release, we set that to 'NULL' and we undonate by sifting through all locks currently held, finding the max priority among all waiters. Everything else from initial design is the same.
  
  - Task 3: Similar to task 1 we moved a lot of the functionality to the thread.c file and call them from timer.c However, the functionality remains the same as in the initial design doc.

## **Reflection on the Project**
  - We all sat down on three seperate nights and went through the design doc together. We all focused on task 1 first, then moved on to tasks 2 and 3 seperately. Alex and Jeff were in charge of everything in Task 3 (functions, algorithms, synchronization) , Kevin and Greg took on the tasks in Task 2 (functions, algorithms, synchronization); however, we would all bounce ideas off of each other and brainstorm together. 
  - For the milestone, we mostly focused on getting Tasks 1 and 3 done first before moving on to Task 2 as that seemed like the most challenging aspect of the project. Alex and Jeff coded those parts while Greg and Kevin focused on Task 2 for after the milestone.
  - Overall, the coding aspect of the project was reasonable. We split the tasks up based on who had designed each task and were able to work pretty efficiently. As an added note, we mostly coded the project individually rather than in group sessions which worked well in this case. 
