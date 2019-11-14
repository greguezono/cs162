Design Document for Project 3: File System
==========================================

## Group Members

* Gregory Uezono <greg.uezono@berkeley.edu>
* Jeff Weiner <jeffantonweiner@berkeley.edu>
* Alex Le-Tu <alexletu@berkeley.edu>
* Kevin Merino <kmerino@berkeley.edu>

## Task 1: Buffer Cache

#### Data structures and functions
  
  * **Creating cache.c/cache.h**
  
```c
  struct cache_block {
    block_sector_t sector;            //index of block sector held
    bool valid_bit;                   //true if cache block has been accessed
    bool dirty_bit;                   //true if cache block is dirty
    bool ref_bit;                     //true if the block has been accessed recently (one clock cycle)
    int active_readers;               //# of active readers
    int waiting_readers;              //# of waiting readers
    int active_writers;               //# of active writers
    int waiting_writers;              //# of waiting writers
    bool flush_or_load;               //true if cache is flushing or loading
    struct condition okToWrite;       //cond for writers
    struct condition oktoRead;        //cond for readers
    struct lock;                      //lock for ready condition variable 
    uint8_t cached_data[];            //stores BLOCK_SECTOR_SIZE bytes of data
  };
  
  struct read_ahead_elem {
    block_sector_t sector;            //sector to be read
    struct list_elem queue_elem;      //list_elem to hold in read_ahead's queue
  };
  
  struct read_ahead {
    struct list queue;                //list containing sectors to be read into cache
    struct lock queue_lock;           //lock for condition variable 
    struct condition read_ahead_cond; //cond variable for queue and 
   };
  
  static struct cache_block buffer[64]; //array of cache_block
  static struct lock cache_lock;        //control for cache_block array
  static int clock_hand;                //current location of clock hand
  
  static int cache_hits;
  static int cache_misses;
  
  cache_init (void);                                          //initializes statics and creates read ahead/flush threads
  
  int get_cache_hit_rate (void);                              //returns hit rate of cache
  
  void cache_read (block_sector_t sector, void * buffer);     //reads a block size sector into buffer and cache (similar to                                                                   block read)
  
  void cache_write (block_sector_t sector, void * buffer);    //writes a block sized buffer to cache, stores sector info                                                                       (similar to block write)
  
  void cache_read (block_sector_t sector, void * buffer, int size);     //reads size from sector into buffer and cache
  
  void cache_write (block_sector_t sector, void * buffer, int size);    //writes size from buffer to cache, stores sector info
  
  int check_cache_for_block (block_sector_t sector);          //if in cache returns index within cache, else return -1 if
  
  struct cache_block get_cache_block (int index);             //returns cache_block at index within cache
  
  int push_cache_block (block_sector_t sector, int (*replacement_policy)(void));              
                                                              //reads from sector and populates a cache_block using                                                                           replacement policy to choose which cache_block to eject
                                                              //Returns index that we pushed into

  int pop_cache_block (int (*replacement_policy)(void));      //using replacement policy, clears a cache_block returning index
      
  void flush_cache_block (struct cache_block);                //flushes sector to disk
  
  void flush_routine (void *);                                //periodic write-behind routine for helper thread
  
  void init_read_ahead (struct read_ahead *ptr);              //initializes the read ahead queue
  
  void cache_read_ahead (block_sector_t sector);              //reads sector asynchroniously into cache
  
  void read_ahead_routine (void *);                           //asyncrhonious read-ahead routine for helper thread
```
  * **Modifying in inode.c**
```c
    bool inode_create (block_sector_t sector, off_t length);  //replace block_write logic with parts cache_write
    
    struct inode * inode_open (block_sector_t sector);        //replace block_read logic parts with cache_read
    
    off_t inode_read_at (...);                                //replace block_read logic parts with cache_read & replace                                                                     bounce buffer logic. Also add readahead logic
    
    off_t inode_write_at (...);                               //replace block_write logic with parts cache_write & replace                                                                   bounce buffer logic
```
  * **Modifying in filesys.c**
```c
    void filesys_init (bool format);                          //adding initialization of cache
    void filesys_done (void);                                 //adding flushing on shutdown of dirty cache_blocks
```
#### Algorithms
  * Cache Replacement Policy: Clock (approx LRU): 
    1. Let the `current` be the `cache_block` that `clock_hand` points to 
    2. If `current` is being used increment the clock hand
    3. If `current`'s `ref_bit` is set increment the clock hand and set `ref_bit` to `false`
    4. If conditions 2 and 3 are not met, return `current`
    5. Increment `clock_hand`
    
  * Periodic Flush
    1. In `cache_init` spawn a thread to run `flush_routine` which does the following indefinitely:
       1. `timer_sleep ()` for some set interval
       2. Upon waking, for each `cache_block` obtain its lock
          1. If `cache_block` is valid and dirty flush the block's data to disk
          2. Reset the `dirty_bit`
          * Note: may need to wait or skip if there are AW
    
  * Read-Ahead
    1. In `cache_init` spawn a thread to run `read_ahead_routine` which does the following indefinitely:
       1. Aquire the lock for the monitor `read_ahead`
       2. While `read_ahead`'s `queue` is empty, wait on the condition variable `read_ahead_cond`
       3. When not empty, popoff from `queue` and obtain the `next_sector`
       4. Release the lock for the monitor (since we don't access it anymore)
       5. Read data from `next_sector` into cache using `cache_read (next_sector, NULL)` 
          (may need to use temporary buffer depending on `cache_read` implementation)
       6. Free any resources e.g. `read_ahead_elem` and temp buffer if used
      
    2. During `inode_read_at`, `cache_read` the requested `sector`. Then if `sector + 1` is valid, call `cache_read_ahead (sector + 1)` which does the following:
       1. Call `sector + 1`, `next_sector`
       2. Aquire the lock for the monitor `read_ahead`
       3. Create a `struct read_ahead_elem` (call this `elem`) for `next_sector`
       4. Push `elem` onto the `queue`
       5. Call `signal` for the condition variable `read_ahead_cond` in order to let the read_ahead
          dameon to asynchroniously read `next_sector` 
       6. Release the lock for the monitor
  * Reads `void cache_read (block_sector_t sector, void * buffer)`
    1. Aquire `cache_lock` over the entire cache
    2. Call `check_cache_for_block (sector)` which returns the `index` if it exists in cache or -1 if not
    3. If index exists (Cache Hit): Obtain `cache_block` (and its lock), call this `current`. Then goto v.
    4. If index does not exist (Cache Miss):
       1. Call `push_cache_block (sector, replacement_policy)` which does the following:
          1. Call `pop_cache_block (replacement_policy)` which does the following:
             1. Using `replacement_policy` obtains next `index` to eject
             2. Releases `cache_lock` and Obtains the `cache_block` using `index` (also acquire its lock)
             3. Flush if necessary
             4. Returns `index`
          2. Copies `sector` into `cache_block` given by `index` and set the `cache_block`'s fields
          3. Returns `index` 
       2. Obtain `cache_block` via `index`, call this `current`. Then goto v.
    5. While `current` is flushing or loading or there are AW or WW wait on `current`'s condition variable
    6. Increment AR, decrement WR, set `ref_bit` and `valid_bit`, release the lock
    7. Read in from `current`'s data into `buffer`
    8. Obtain the lock for `current`, decrement AR, signal any WR, release lock
    
  * Writes `void cache_write (block_sector_t sector, void * buffer)`
    1. Acquire `cache_lock` over the entire cache
    2. Call `check_cache_for_block (sector)` which returns the `index` if it exists in cache or -1 if not
    3. If index exists (Cache Hit): Obtain `cache_block` (and its lock), call this `current`. Then goto v.
    4. If index does not exist (Cache Miss):
       1. Call `push_cache_block (sector, replacement_policy)` which does the following:
          1. Call `pop_cache_block (replacement_policy)` which does the following:
             1. Using `replacement_policy` obtains next `index` to eject
             2. Releases `cache_lock` and Obtains the `cache_block` using `index` (also acquire its lock)
             3. Flush if necessary
             4. Returns `index`
          2. Get `cache_block` given by `index` and set the `cache_block`'s fields `
          3. Returns `index` 
       2. Obtain `cache_block` via `index`, call this `current`. Then goto v.
    5. While `current` is flushing or loading or there are AW or AR wait on `current`'s condition variable
    6. Increment AW, decrement WW, set `ref_bit` and `valid_bit`, release the lock
    7. Write from `current`s data to `sector` 
    8. Obtain lock for `current`, set `dirty_bit`, decrement AW, signal any WW (if none then WR), release the lock
    
#### Synchronization
  1. `struct read_ahead`'s `queue`: 
     * Uses lock to ensure single access to queue when pushing on another sector to read ahead
     * Uses condition variable to ensure single access to queue when popping off to asyncrhoniously read
  2. `struct list cache_block buffer[64]`:
     * During a routine flush we could acquire the global `cache_lock` before iterating through, but this might not entirely necessary as flushing won't modify any of the buffer's pointers and we will individually lock `cache_block`s while flushing
     * When searching for a sector we acquire the global `cache_lock` so that no `cache_block`s can be replaced during search
     * Read / Write: Same logic as search since we search for a cache hit before any reading and writing
     * Serialized read / write : using the readers/writers problem and using monitors we ensure that no race condtions are involved while accessing a specific sector. (ex. multiple readers may read a sector at once, but only one writer can access a sector at a time)
  3. `struct cache_block`:
     * Any modification or access of a `cache_block` will be contained in acquiring and releasing its individual lock
     * Special cases are during reading and writing in which we also use a condition variable in conjunction with the individual lock as a monitor to regulate concurrent access to a `cache_block`
  
#### Rationale
- We found that since we are including many new elements and aspects, and that a cache system would in essence be another whole structure running along side other memory structures in Pintos, that it would be best to create the majority of this in its own separate files in order to isolate and modularize the logic (cache.c and cache.h).
- Following from this we found that the `struct list cache_block buffer[64]` acted much like storing 'rows' of a cache table, whereas the individual `cache_block` preserves the metadata (ex: dirty, ref, valid bits, as well as current accessors) as if this data formed the 'columns' of a cache table.
- For synchronization we found it useful to have a fine-grained lock on each `cache_block` as well as one global one for the `buffer`. This way multiple processes can access different parts of the `cache` simultaneously without blocking processes while maintaining consistency across their individual accesses.
- For other syncrhonization we found that the use of monitors was extremely useful because there may be multiple processes accessing the same critical section. For `read_ahead` this is useful because multiple threads can add to the `read_ahead` queue. For reading and writing concurrently, as concurrent access to the filesystem is required, we used a similar setup to the readers/writers problem.
- For the readers/writers problem, the current setup is writers-biased, but we are able to change this after some real testing
- We decided to create a function to pass in order to determine the replacement policy. While we will most likely use the clock (LRU Approx) algorithm, we wanted to keep this added flexibility to mimic a real world design decision - for example reading a large sequential segment of data that is larger than the cache in repitition could benefit from MRU policy.
- With the hint from the spec and because we are using our non-busy-wait `timer_sleep`, it made the most sense to spawn two helper threads during initializion that will execute as background processes. One actively asyncrhoniously reads ahead for processes that requested a block. The other uses `timer_sleep` to periodically flush dirty parts of the cache to disk.
- In place of a `struct list` for representing our cache, we could perhaps use the hashtable implementation found in hash.c, in theory this could provide us with better performance on searches, especially since the size of the cache should remain under 64. However one downside to this is that in order to make searches fast, we would insert into the hashtable based on sector id so we would have to remove and reinsert each time we want to swap a new sector in from disk. Another side consideration for this is that in a general programming / design sense elements of hashtables should generally be constant.

## Task 2: Extensible Files

#### Data structures and functions
* **Adding in syscall.c:**
    - `inumber (int fd)`
* **Modifying in syscall.c and thread.h:**
  - We will be modifying our current implementation of the file descriptor table and file descriptor structs. This will result in changes to syscall.c (fd table and fd struct) and thread.h (which will now have a list of files). Explained in Algorithms section.

* **Adding in inode.c:**
````
bool inode_extend (struct inode_disk *inode_disk, off_t length)

block_sector_t allocate_indirect_block (off_t entry)

struct indirect_block { 
  block_sector_t direct[128];              /* 128 pointers to direct pointers to block_sector_t */ 
}
````


* **Modifying in inode.c:**
```c /* Definition of block_sector_t */ 
typedef uint32_t block_sector_t; 
```
```c
/* Contents of on-disk inode. Must be exactly 512 bytes long. */
 struct inode_disk { 
    off_t length;                           /* File size in bytes. */ 
    unsigned magic;                         /* Magic number. */

    bool is_dir;                            /* bool to check if the inode is a directory */
    block_sector_t direct[12];              /* 12 direct pointers to block_sector_t*/ 
    block_sector_t indirect;                /* location of a indirect block */ 
    block_sector_t double_indirect;         /* location of a double indirect block */ 
    uint32_t unused[111];                   /* Not used. */ 
};
```
- We will have 12 direct pointers, 1 indirect pointer, and 1 double indirect pointer. We now have 444 bytes of unused space, which is accounted for in unused[112].
```c
struct inode {
    bool is_dir;                        /* True if inode is for directory */
    struct lock lock_inode;             /* Inode lock */
};
```
- Add is_dir following inode_disk addition. Use lock_inode for race conditions (explained in detail in Synchronization).
```
static block_sector_t byte_to_sector (const struct inode *inode, off_t pos);
off_t  inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
```
- We will need to change these to account for the multiple layers (direct, indirect, double_indirect).
````
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
````
- Change this to account for extending file size by calling `inode_extend`
#### Algorithms
* **thread.h and syscall.c**
  - File descriptor table:
    - We currently have a global fd table in syscall.c and malloc an fd struct everytime a file is opened. We decided that each thread should keep track of its own file descriptor table. There are two ways we thought of going about this:
      1. We could use the Pintos built in hashmap with files as elements.
      
      2. Or we could use the C built in list implementation (a list of pointers to file objects) and index the list directly using the fd number.
    - We believe the second implementation as a list of direct pointers to files would be more space efficient as it would eliminate the need for mallocing fd structs. We also found this implementation to be easier to debug and maintain. These are some of the aspects that we would need to implement in order to go through with this:
      1. Predetermine the size of the fd table in advance.
      2. If we allocate more files than the size of the initial fd table, we will have to resize the list (create a new list of pointers and copy the old list into the new one). Therefore, each thread would need to keep track of the current size of its fd table.
      3. Closing and opening files would be easier as we would directly index the fd table.
* **syscall.c**
  -  `inumber (int fd)`:
````c
    - get the file from the thread's fd_table
    - get the inode from the file using `file_get_inode(file)`
    - get the unique inode number using inode_get_inumber(inode)
    - return the inode number
````
* **inode.c**
 - `bool inode_extend (struct inode_disk *inode_disk, off_t length)`
    - While inode_disk->length < length
      - calloc a new block and set it as calloced_block. This represents the new block_sector_t that is being extended.
      - allocate direct, indirect, or double indirect blocks accordingly (i.e. check if inode_disk->length + (size of a block sector) < direct pointer capacity, indirect pointer capacity, or double indirect pointer capacity).
        - Set direct pointer to calloced_block
        - For indirect, call allocate_indirect_block(calloced_block)
        - For double indirect,  call allocate_indirect_block(calloced_block), then allocate_indirect_block on the indirect block pointer to create a double indirect block.
      - add newly allocated block capacity to inode_disk->length
    - return true if successful, false if not

    
 -  `block_sector_t allocate_indirect_block (off_t calloced_block)`
    - Allocates an indirect block, which is just a struct with 128 pointers to indirect or direct blocks
    - Sets the first entry in the indirect as the calloced_block (a direct pointer to a newly allocated block)
    - Uses cache write to allocate a new sector for the block
    - return newly allocated block_sector_t

#### Synchronization
  - Since each `inode_open` and `inode_close` call modifies the same `open_inodes` list structure, we will need to add a `struct lock lock_open_inodes` in order to avoid race conditions on that list structure.
  - In Project 2, we had a global lock for filesystem operations. For Project 3, we eliminate the need for a global lock by adding a `struct lock lock_inode` inside of each inode. We considered adding a lock inside the `struct file` in file.c, however, we chose this implementation since concurrent reads/writes to the same sector should be blocked while concurrent reads/writes to the same file should be handled by the user (i.e. it is up to the user to add waits for multiple reads/writes to the same file).
  
#### Rationale
- We decided that we needed to have 1 double indirect pointer in addition to the indirect pointer mentioned in discussion to account for the 8MB maximum file size (8MB = `2^23 Bytes of Data`, a single indirect pointer would only allow for `2^18 Bytes of Data`).
- For each thread's file descriptor table, we decided to use the C built in list implementation as it seemed more space efficient (would eliminate the need for mallocing fd structs), easier to maintain/keep track of, and allowed for easier opens/closes by indexing into the list with the fd number directly.
- We decided to zero out all blocks and write zeroed blocks for allocating implicit blocks mentioned in the spec. We figured this approach to be simpler and easier to track, although it is not as efficient.
- We followed the main ideas in Section 11 for the file extension implementation. 


## Task 3: Subdirectories

#### Data structures and functions
* **Adding to thread.c:**
    ```c
    struct thread {
        struct dir *curr_dir;
     }
     ```
     
* **Adding to syscall.c:**
  ```c
  bool chdir (const char *dir);       /* change current working directory */
  
  bool mkdir (const char *dir);       /* make a new directory */
  
  bool isdir (int fd);                /* check if directory or file */
  
  bool readdir (int fd, char *name);  /* read directory */

* **Adding to filesys.c:**
  ```c 
  bool path_traverse (const char *filepath, struct dir **dir);  /*traverses through filepath, relative or                                                                    absolute, and set dir to last segment of                                                                    path if exists. */
  ```
  
* **Modifying in filesys.c**
  ```c
  filesys_open (const char *name);     /*check if we need just root directory, if not use path_traverse */
    
  filesys_remove (const char *name);   /*path_traverse to last segment, only remove directory if not
                                        root and it does not contain anything other than "." and ".." */
  ```

#### Algorithms
  * `path_traverse`: (uses `get_next_part` from spec)
    1. Find first character in filepath
        1. If it is "/", we know it is an absolute path, so open root directory
        2. If not, it is a relative path, so we open the current thread's current working directory
    2. Use `get_next_part` to go through each segment of the path
        1. If the next segment does not exist, fail
        2. If it exists, open the directory and continue
        3. Once we reach the last "/" set passed in dir to last opened directory
   
  * `chdir`:
    1. Check if pointer is valid with `validate_string (dir)`
    2. `path_traverse` to go find if dir exists, and if so go into last segment
    3. Get the `inode` corresponding to the last segment and set the current working directory to it
    
  * `mkdir`:
    1. Check if pointer is valid with `validate_string (dir)`
    2. `path_traverse` to get to the last segment before the new directory
    3. Check if the directory already had a directory with this name
    4. create a new `inode` and add it to this directory (the parent)
    5. Go into the new directory and add "." and ".." directories
    
  * `isdir`:
    1. Check for the `fd` in the current thread's `fd table`
    2. get the inode from the corresponding `fd`
    3. return `inode->isdir` (defined in task 2)
  
  * `readdir`:
    1. Use `isdir (fd)` to verify the `fd` corresponds to a directory
      1. return `false` if not
    2. open the corresponding directory `inode`
    3. call `Dir_readdir (dir, name)`
    
#### Synchronization
- We will add a directory lock in the `struct inode` in order to prevent errors with race conditions in the operations in the directory.c file. This is to prevent conflicts with concurrent and conflicting calls to the same directory. 
  
#### Rationale
- The main component of this task seems to be handling relative filepaths to accomodate hierarchical directories. Because most of the functions being added or changed revolve around this, we decided to implement a `path_traverse` method that handles the absolute path vs relative path issues as well as the path traversal in one step. We thought that most of the synchronization errors could be handled with a simple lock on the inode. The system call `inumber(int fd)` will be handled in task 2.

## 2.1.3 Design Document Additional Question
##### Question: 
For this project, there are 2 optional buffer cache features that you can implement: write-behind
and read-ahead. A buffer cache with write-behind will periodically flush dirty blocks to the filesystem block device, so that if a power outage occurs, the system will not lose as much data. Without
write-behind, a write-back cache only needs to write data to disk when (1) the data is dirty and
gets evicted from the cache, or (2) the system shuts down. A cache with read-ahead will predict
which block the system will need next and fetch it in the background. A read-ahead cache can
greatly improve the performance of sequential file reads and other easily-predictable file access
patterns. Please discuss a possible implementation strategy for write-behind and a strategy for
read-ahead.

##### Write-behind answer: 
Our implementation adds the following functions for the cache system which help with write-behind:
```c
cache_init (void);                                          //initializes statics and creates read ahead/flush threads
void flush_cache_block (struct cache_block);                //flushes sector to disk
void flush_routine (void *);                                //periodic write-behind routine for helper thread
```

* `cache_init (void)` in addition to initializing the cache system will spawn a helper thread to run in the background which runs `void flush_routine (void *)`
* `flush_cache_block (struct cache_block)` flushes the data contained in a `cache_block` to its specified sector
* `flush_routine (void *)` iterates over all `cache_block`s and calls `flush_cache_block` on them if they are dirty, valid, not loading, not flushing, and not being written to. Repeats this flushing periodically with `timer_sleep` by a definied interval.

From above we can implement in pseudocode the write-behind feature:

  1. In `cache_init` spawn a thread to run `flush_routine` which does the following indefinitely:
     1. `timer_sleep ()` for some set interval
     2. Upon waking, for each `cache_block` obtain its lock
        1. If `cache_block` is valid and dirty flush the block's data to disk
        2. Reset the `dirty_bit`
        * Note: may need to wait or skip if there are AW

##### Read-ahead answer: 
Our implementation adds the following structs/functions for the cache system which help with read-ahead:
```c
  struct read_ahead_elem {
    block_sector_t sector;            //sector to be read
    struct list_elem queue_elem;      //list_elem to hold in read_ahead's queue
  };
  
  struct read_ahead {
    struct list queue;                //list containing sectors to be read into cache
    struct lock queue_lock;           //lock for condition variable 
    struct condition read_ahead_cond; //cond variable for queue and 
  };
   
  cache_init (void);                                          //initializes statics and creates read ahead/flush threads
  void init_read_ahead (struct read_ahead *ptr);              //initializes the read ahead queue
  void cache_read_ahead (block_sector_t sector);              //reads sector asynchroniously into cache
  void read_ahead_routine (void *);                           //asyncrhonious read-ahead routine for helper thread
```

* `struct read_ahead` acts as a monitor syncrhonization variable. It has a `struct list` which tracks which block sectors will be asyncrhoniously read into the cache.
* `struct read_ahead_elem` is simply a wrapper around `block_sector_t`s so that they can be inserted into lists
* `cache_init (void)` in addition to initializing the cache system will:
  * spawn a thread to run in the background which runs `void read_ahead_routine (void *)`
  * initialize the read ahead queue
* `cache_read_ahead (block_sector_t sector)` will enque (prefetch) sectors to be read into the cache. This should only be called by inode.c during read calls
* `read_ahead_routine (void *)` simply uses the monitor to deque sectors from the queue and reads them into cache using the already written `cache_read` function

From above we can implement in pseudocode the read-ahead feature:

  1. In `cache_init` initialize the read_ahead_struct spawn a thread to run `read_ahead_routine` which does the following indefinitely:
     1. Aquire the lock for the monitor `read_ahead`
     2. While `read_ahead`'s `queue` is empty, wait on the condition variable `read_ahead_cond`
     3. When not empty, popoff from `queue` and obtain the `next_sector`
     4. Release the lock for the monitor (since we don't access it anymore)
     5. Read data from `next_sector` into cache using `cache_read (next_sector, NULL)` 
        (may need to use temporary buffer depending on `cache_read` implementation)
     6. Free any resources e.g. `read_ahead_elem` and temp buffer if used

##### Relative Path:
  - Find the current directory then iterate through the path, parsing each directory until we reach a file:
    - Find the first non-space character. 
      - If it is a '/' we know the path is an **absolute path** and we should open the root directory.
      - If not, the path is relative and we should open the process working directory.
    - Iterate through the path until the last '/' and return the last opened directory.
