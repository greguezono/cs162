Final Report for Project 3: File System
=======================================
## Group Members

* Gregory Uezono <greg.uezono@berkeley.edu>
* Jeff Weiner <jeffantonweiner@berkeley.edu>
* Alex Le-Tu <alexletu@berkeley.edu>
* Kevin Merino <kmerino@berkeley.edu>

## Changes in initial design doc

### Task 1: Buffer Cache
#### Direct changes to the spec:
1. We added a condition var over flushing and loading. We did this after we were implementing some of the reader and writer parts and realized that a thread should also have to wait if the block is flushing or loading before accessing it.
2. We did not implement read ahead or write behind because of time constraints and a ton of debugging else where
3. We added reading and writing certain sizes. The starter code just permitted block reads and block writes which was fairly restrictive and had to use bounce buffers on writes. Writes would read in a sector to cache (if cache miss) before any writing to get rid of the need for a bounce buffer
4. We moved any direct use of `struct cache_block`. Because our implementation creates `struct cache_block`s to wrap around our sector data representing blocks, we had to avoid something like `struct cache_block = cache[index]` because the kernel would then copy over all of that onto its stack.
  
#### Some questions that came from the initial design doc review:
**Q: How do we prevent 2 concurrent accesses to the same sector?**
A: We use two levels of granularity for locking, a coarse lock over the entire cache and a fine grain lock for individual cached sectors. In addition we track active/waiting readers and writers. Since we do not block on IO, we can have multiple readers on the same sector or only 1 writer. The fine grain lock allows us to synchroniously update reader/writer counts just as in class. The existance of fine grain locks also allow multiple threads to access the cache at once as long as they are in different sectors, the fine grain lock is just for quickly across the cache.

**Q: How do I prevent a block from reading from an evicted sector (that it may have blocked on earlier)?**
A: We use our sector number, locks, and counts to solve this. A block is evicted when there needs to be room for another block to be read into the cache. There are only two ways that a block could possibly read from an evicted sector. 1) The calling function receives a cache hit when looking for a sector, then another thread evicts the block, or 2) the calling function receives a cache hit on a block that another thread has evicted (or is currently evicting). The protocol that we have implemented for eviction and searching for cache hits solves 1) using sector numbers and loading statuses. In case 1) the main thread will only receive a cache hit after checking a block's sector id; however, this implies that it has acquired the lock on the block. At the same time before releasing it to read or write, it will update the reader/writer counts. All threads can only evict blocks which have no current users. In case 2) we have that we receive a cache hit on an evicted(/ing) block. However, this is not possible because the protocol for eviction is that we must acquire the lock on the block and update its sector number before releasing the lock again. When searching for cache hits we must obtain the lock on a block then compare the sector number. From this we can see that we cannot ever have an old sector number on a block that is loading in a new sector. Sector numbers always reflect the current sector of a cache block though it may still be loading in.

**Q: How do you prevent the same block from being loaded into the cache in 2 different places at the same time?**
A: Similar to the previous question, since we use the fine grain locks to update metadata of the cache, more specifically the sector number in a cache block, we always have current accurate information about the presence of a sector being in the cache (i.e. a sector number in the cache -> that sector is present or loading in the cache). The only way that we could load 2 blocks into the cache at different places would be if 2 processes were attempting to load the block in at the same time; however, we use the fine grain lock to provide single access scans across the cache. If two processes tried to load the same sector, one would acquire the global cache lock, find an index in the cache for eviction and acquire that block lock, update the sector id and loading status, then release the global lock. The second process could then begin its scan; however, by this time the sector id is updated in the cache and it will receive a cache hit, thus not writing to another part of the cache.
    
**Q: Do I block on an I/O operation?**
A: No, we use locks before and after I/O operations to regulate concurrent access to metadata that regulates concurrent access during I/O. We then use condition variables after I/O operations to signal any waiters for I/O operations. This model is heavily based on the readers-writers problem from class.

### Task 2: Extensible Files
#### Direct changes to the spec:
1. In `inode.c`, we added global variables:
    - `PTRS_PER_BLOCK`: the number of pointers per indirect block (128 pointers)
    - `L0_CAPACITY`: capacity of all the direct pointers in an inode
    - `L1_CAPACITY`: capacity of the indirect block in the inode
    - `L2_CAPACITY`: capacity of the double indirect block in the inode
2. Following our TA's suggestion (Jonathan Murata), we allocated 122 direct pointers instead of keeping 444 bytes of unused space in our design doc implementation.
3. We decided to go for the C built list implementation for file descriptor tables in `thread.c`. We found it was easier to implement and keep track of.
4. The logic for the initial `inode_extend` function was good; however, we found that a lot of helper functions were needed in order for the designed `inode_extend` to work:
      1. we changed `inode_extend` to `disk_inode_extend_to_len` and created a `disk_inode_extend_sector`. 
          - `disk_inode_extend_sector` would allocate a new block sector to the disk_inode. This was the **most complicated function** to write as we had to check whether to allocate blocks at the direct, indirect, or double indirect level and handle those subcases accordingly.
          - `disk_inode_extend_to_len` would continuously call `disk_inode_extend_sector` while the space needed for the file < space left in inode. This function would directly change the disk_inode length as well.
5. We found that we needed to create helper functions to index into the direct pointers, indirect blocks, and double indirect blocks:
    - `index_direct`: returned the index into the direct pointers associated with `pos` of the file
    - `index_indirect`: returned the index for the indirect_block pointer associated with `pos` of the file
    - `index_double_indirect_L1`: returned the index for the first level of the double_indirect pointer associated with `pos` of the file (the indirect pointer)
    - `index_double_indirect_L0`: returned the index for the second level of the double_indirect pointer associated with `pos` of the file (the direct pointer). Called it L0 for consistency with `L0_CAPACITY` defined above.
6. We created `allocate_calloced_block` which would calloc a new block using `free_map_allocate` would write the block to disk. We initially did not understand that allocating a new block_sector_t was not as simple as calling `calloc(BLOCK_SECTOR_SIZE)`.
7. We created `allocate_double_indirect_block` which would call `allocate_indirect_block` twice (same as design doc).
8. We found that we were consistently reading disk inodes, indirect blocks, and double indirect blocks from memory so we created the following functions:
    - `get_inode_disk_from_mem`: returned a pointer to the inode's `disk_inode` from memory
    - `get_indirect_block_from_mem`: returned a pointer to the inode's `indirect_block` from memory (same function used for double indirect blocks)
9. We made an optimization to `byte_to_sector` so that it would take in a `disk_inode` instead of an `inode`. The reason for this is that we had to retrieve the `disk_inode` from memory on every call, so we decided to do that outside of the function for simplicity and performance. We also created the following helper functions for `byte_to_sector`
    - `get_sector_from_indirect`: returned the sector associated with some indirect index
    - `get_sector_from_double_indirect`: returned the sector associated with some double indirect indices (L1, L0).
    - direct pointers would just be directly indexed from the `disk_inode`.
10. Finally, we found that we had to create `inode_free_all_sectors` as a helper to `inode_close` and `inode_remove`.
    - the initial implementation would just free all the data associated with `inode->data`
    - with our new `disk_inode` implementation (direct, indirect, double indirect levels), we had to iterate over all allocated sectors and free them. 
    - This was definitely the **second most complicated function** to implement after `inode_extend_sector` as there were a lot of `block_sector_t`'s to keep track of

### Task 3: Subdirectories
#### Direct changes to the spec:
1. The biggest issue we ran into with our design from the design doc was that many of the syscalls and filesys functions we needed to modify had to traverse to the second to last directory rather than the last directory in the file path (i.e. in `mkdir` we need to create the last directory so we cannot traverse into it). To solve this issue without making these operations too messy, we implemented a `path_splice` function in filesys.c which stores the whole file path up to the last segment in one buffer and the last segment in another buffer. This way we can simply call `path_splice` and then `path_traverse` to easily open the second to last directory, while also maintaining the functionality of `path_traverse` alone for functions like `chdir`.
2. We decided to not allow the deletion of directories that are currently being used because we felt that it would be easier to implement a simple check in the `dir_remove` function than it would be to handle the cases that could arise from deleting a thread's current working directory. All that this required was a clause in `dir_remove` that results in failure if the directory's `open_cnt` was greater than or equal to 2 (i.e. a thread currently has the directory open). We implemented a method in inode.c to return an inode's `open_cnt` and added a simple if statement in `dir_remove` and that was it.
3. One of the things we originally did not specify in the design doc were the many situations that we needed to close directories. A lot of the debugging for this task was in relation to the remove syscall because there were directories that were accidentally left open in the traversing process or when setting a new current working directory.

## **Reflection on the Project**
General thoughts for task 1: Syncrhonization was hard to keep track of and we had to draw out the flow of control with a diagram. It makes a lot of sense when you break it down into small pieces and examine each problem in relation to the other like in the reader writer problem, but it's still a ton of meta data to track. In addition there are a lot of possible edge cases to either test or prove cannot happen. The reader/writer problem was also cool to implement pretty much directly right here in the project as well since it seemed like such a simple example in class about checking out books at a library it didn't seem like it would lend itself so directly at first to a problem like building an OS file system.

General thoughts for Task 2: Overall, we sat down and thought a lot about how to implement each of the large functions (`inode_extend` in our original design doc, `inode_write_at`, `inode_close`, and `inode_read_at`) and what helper functions each of these functions would need. We then wrote pseudocode for the large functions with the assumption that the helper functions would work, then moved onto designing how the actual helper functions would work. Overall, this was the most design intensive part and it paid off in the end. As a side note, we probably spent a lot less time debugging this project (surprisingly) since we carefully thought about how to implement each part independently of other parts (also this project allowed for more independent code writing than the other projects which also helped). 

General thoughts for task 3: Over all this task was pretty straightforward. Most of our problems stemmed from all of the different file path possibilities (nested, not nested, absolute, relative, etc.), but once the `path_splice` and `path_traverse` functions were implemented correctly, the rest was pretty smooth. GDB was especially useful for this because it made it easy to find different bugs and errors in the file path handling. One regret would be that we tried to implement the subdirectories and complementary syscalls before the buffer cache and extensible files were implemented for the milestone, but this meant that we had to temporarily use fixed sizes for directories and then change them later, and we were confused about what size to make the directories before the inode struct was fully implemented and could implicitly extend the sizes. In hindsight we probably should have followed the spec's suggestion and implemented the tasks in order. 

For who worked on what, Alex mostly worked on task 1 and integrating it to work with the inode implementation (with Greg), as well as debugging various edge cases and errors that arose from the previous project. Greg worked mainly on Task 2 (design and implementation). Jeff worked mostly on Task 3, along with some general debugging. Kevin worked on the tests, as well as helping debug and design all of the tasks.

## **Student Testing Report**
1. **CACHE-WNR**
    - The first test we implemented was 'cache-wnr' that would be used to test our cache's
    writing capabilities. This test was designed to ensure there are no unnecessary reads
    being done by our cache as we *write* to a file. 
    - The way this test works is that we first make a 1KB buffer with all 1's. We create a file
    called 'dbz' of size 100 KB. Here we print create "dbz".
    Before continuing we ensure we created the file (we print open "dbz"), then we do a
    system call to grab the number of reads currently done on our filesystem. This required the
    installement of a new system call so that the kernel may access the filesystem's block and
    recover the number of reads safely. We then proceed to write the 1KB buffer 100 times onto
    the file. We verify we wrote 100KB and continue. A second system call is made to retrieve the
    number of reads that have occurred in this block. We produce a check to see if this number is
    the same then we pass the test. By now we should have printed good cache.
    - **filesys/build/tests/filesys/extended/cache-wnr.output**
        ````
        - Copying tests/filesys/extended/cache-wnr to scratch partition...
        - Copying tests/filesys/extended/tar to scratch partition...
        - qemu -hda /tmp/tbyzsy2vEq.dsk -hdb tmp.dsk -m 4 -net none -nographic -monitor null
        - PiLo hda1
        - Loading............
        - Kernel command line: -q -f extract run cache-wnr
        - Pintos booting with 4,088 kB RAM...
        - 382 pages available in kernel pool.
        - 382 pages available in user pool.
        - Calibrating timer...  425,164,800 loops/s.
        - hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
        - hda1: 197 sectors (98 kB), Pintos OS kernel (20)
        - hda2: 235 sectors (117 kB), Pintos scratch (22)
        - hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"
        - hdb1: 4,096 sectors (2 MB), Pintos file system (21)
        - filesys: using hdb1
        - scratch: using hda2
        - Formatting file system...done.
        - Boot complete.
        - Extracting ustar archive from scratch device into file system...
        - Putting 'cache-wnr' into the file system...
        - Putting 'tar' into the file system...
        - Erasing ustar archive...
        - Executing 'cache-wnr':
        - (cache-wnr) begin
        - (cache-wnr) create "dbz"
        - (cache-wnr) open "dbz"
        - (cache-wnr) good cache
        - (cache-wnr) end
        - cache-wnr: exit(0)
        - Execution of 'cache-wnr' complete.
        - Timer: 104 ticks
        - Thread: 30 idle ticks, 56 kernel ticks, 18 user ticks
        - hdb1 (filesys): 913 reads, 884 writes
        - hda2 (scratch): 234 reads, 2 writes
        - Console: 1068 characters output
        - Keyboard: 0 keys pressed
        - Exception: 0 page faults
        - Powering off...
        ````
    - **filesys/build/tests/filesys/extended/cache-wnr.result**
    ````
    PASS
    ````
    - **Potential Bugs**
    One potential bug that this test will uncover is the apparent bug of reading while
    we are writing. If our cache is reading then we are going to evict the files we need
    thus our cache will be rendered useless. Another bug that this test checks is our cache's
    ability to write accross different cache blocks. If our kernel had a bug where we aren't able
    to write correctly then we would have triggered a kernel panic and our system would have crashed.
    The most important part of this test is the testing for performance, since unnecessary reads
    directly result in wasted memory fetching waits. 
2. **CACHE-HITRATE**
    - This test, 'cache-HitRate' was built to satisfy the verification of our cache's functionality.
    Since PintOS does not offer any tests for our cache this test plays a crucial role in verifying
    the performance boost of a cache. 
    - The way this test works is very similar to how the spec proposes we build it. We first make a file
    sized 33 KB, called DB. Here our program should print create 'db'.
    We utilize a 1KB buffer filled entirely with 1's. We ensure we are able to open DB correctly and proceed to fill the DB file with the buffer. Here our program should print open 'db' followed by "wrote all 1s". 
    Next we close the file and call a systemcall to clear the cache. This systemcall required additional modifications to our systemcall files, and utilizes the methods offered by our cache.c file. We then open DB again and read sequentially. Here we print open "db", read "db".
    We take a measurement of our current hit rate and close DB. Once again we open DB and begin to read
    sequentially. To which we print open "db", read "db".
    We take a second measurement of our current hit rate and close DB. 
    We expect a boost from the firt hit rate compared to the second since our cache should have cached the file by then. Here we use another CHECK to verify we indeed experienced an increase in performance to which we print "good cache".
    - **filesys/build/tests/filesys/extended/cache-hitRate.output**
    ````
    - Copying tests/filesys/extended/cache-hitRate to scratch partition...
    -Copying tests/filesys/extended/tar to scratch partition...
    -qemu -hda /tmp/z2l3zebN3b.dsk -hdb tmp.dsk -m 4 -net none -nographic -monitor null
    -PiLo hda1
    -Loading............
    -Kernel command line: -q -f extract run cache-hitRate
    -Pintos booting with 4,088 kB RAM...
    -382 pages available in kernel pool.
    -382 pages available in user pool.
    -Calibrating timer...  419,020,800 loops/s.
    -hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
    -hda1: 197 sectors (98 kB), Pintos OS kernel (20)
    -hda2: 237 sectors (118 kB), Pintos scratch (22)
    -hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"
    -hdb1: 4,096 sectors (2 MB), Pintos file system (21)
    -filesys: using hdb1
    -scratch: using hda2
    -Formatting file system...done.
    -Boot complete.
    -Extracting ustar archive from scratch device into file system...
    -Putting 'cache-hitRate' into the file system...
    -Putting 'tar' into the file system...
    -Erasing ustar archive...
    -Executing 'cache-hitRate':
    -(cache-hitRate) begin
    -(cache-hitRate) create "db"
    -(cache-hitRate) open "db"
    -(cache-hitRate) wrote all 1s
    -(cache-hitRate) open "db"
    -(cache-hitRate) read "db"
    -(cache-hitRate) open "db"
    -(cache-hitRate) read "db"
    -(cache-hitRate) good cache
    -(cache-hitRate) end
    -cache-hitRate: exit(0)
    -Execution of 'cache-hitRate' complete.
    -Timer: 620 ticks
    -Thread: 30 idle ticks, 46 kernel ticks, 544 user ticks
    -hdb1 (filesys): 785 reads, 619 writes
    -hda2 (scratch): 236 reads, 2 writes
    -Console: 1240 characters output
    -Keyboard: 0 keys pressed
    -Exception: 0 page faults
    -Powering off...
    ````
    - **filesys/build/tests/filesys/extended/cache-hitRate.result**
    ````
    PASS
    ````
    - **Potential Bugs**
    This test reveals if a potential bug in our cache implementation. This would affect
    the performance of our system because we are doing the extra work of maintaining a
    cache without receiving any of the performence. If system is unable to recycle
    the cached version of the file then we will not be able to utilize any of the features
    of the cache. Another potential bug this test reveals is the kernel's ability to recover
    from a cache miss, this also tests that the cache is able to maintain correct functionality
    when presented with new data. If this test failed into a kernel panic, this would imply
    that we our cache misses are forcing us to grab erraneous data instead of correctly requesting
    the disk for it.   
3. We had a better experience with this test suite because the project
    spec this a really good job of explaining how the filesystem tests are situated. 
    The PintOS system should allow for pausing along our program execution, similar to cgdb.
    This would be specially helpful in a large scale project such as pintOS. We would also
    like for this testsuite to allow for easier file inclusion. We feel like the current way
    is very convoluted and can be a major source for bugs. As we built these test suites,
    we forced ourselves to walk over the code to predict what a result should look like. This is
    helpful as we felt like the implementation of systemcalls were specially clear when finishing 
    this part of the project.
