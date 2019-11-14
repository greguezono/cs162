/* main imports */
#include <stdbool.h>
#include <string.h>
#include "threads/synch.h"
#include "cache.h"
#include "filesys.h"

/* Sets size of cache, stores up to CACHE_SIZE disk sectors in static memory */
#define CACHE_SIZE 64

/**
 * Struct: cache_block
 * -------------------
 *   Represents a wrapper around a disk sector stored in static memory (caching).
 *   Includes various meta-data as well.
 */
struct cache_block {
    block_sector_t sector;                      /* index of block sector held */
    bool valid_bit;                             /* true if cache block has been accessed */
    bool dirty_bit;                             /* true if cache block is dirty */
    bool ref_bit;                               /* true if accessed within one clock cycle */
    int active_readers;                         /* # of active readers */
    int waiting_readers;                        /* # of waiting readers */
    int active_writers;                         /* # of active writers */
    int waiting_writers;                        /* # of waiting writers */
    bool flush_or_load;                         /* true if cache is flushing or loading */
    struct condition okToWrite;                 /* cond for writers */
    struct condition okToRead;                  /* cond for readers */
    struct condition flush_or_load_cond;        /* cond for flushing or loading */
    struct lock block_lock;                     /* lock for ready condition variable */
    uint8_t cached_data[BLOCK_SECTOR_SIZE];     /* stores BLOCK_SECTOR_SIZE bytes of data */
};

static struct cache_block cache[CACHE_SIZE];    /* cache representation, array of cache blocks */
static struct lock cache_lock;                  /* control for cache_block array and clock_hand */
static uint8_t clock_hand;                      /* current location of clock hand [0, CACHE_SIZE) */

static int cache_hits;
static int cache_misses;

/* initializes statics and creates read ahead/flush threads */
void cache_init (void);
/* returns hit rate of cache */
int get_cache_hit_rate (void);
/* reads a block size sector into buffer and cache (similar to block read) */
void cache_read_block (block_sector_t sector, void * buffer);
/* writes a block sized buffer to cache, stores sector info (similar to block write) */
void cache_write_block (block_sector_t sector, const void * buffer);
/* reads size from sector into buffer and cache */
void cache_read_size (block_sector_t sector, void * buffer, off_t size, off_t offset);
/* writes size from buffer to cache, stores sector info */
void cache_write_size (block_sector_t sector, const void * buffer, off_t size, off_t offset);
/* if in cache returns index within cache, else return -1 if */
int check_cache_for_block (block_sector_t sector);
/* reads from sector and populates a cache_block using replacement policy to choose which
cache_block to eject Returns index that we pushed into. */
int push_cache_block (block_sector_t sector, int (*replacement_policy)(void));
/* using replacement policy, clears a cache_block returning index */
int pop_cache_block (int (*replacement_policy)(void), block_sector_t new_sector);
/* flushes sector to disk */
void flush_cache_block (int index);
/* flushes entire cache to disk (call during shutdown) */
void flush_cache (void);
/* Replacement policy using clock algorithm */
int clock_replace (void);
/* gets the index of a certain sector or fills it in if not already in cache */
int cache_fetch_index (block_sector_t sector);

/**
 * Function: cache_init
 * --------------------
 * Initializes cache system using fs_device as its host device to read and write to.
 * Initializes CACHE_SIZE blocks, setting all their data to 0
 * Optionally spawns helpers for read ahead and write behind routines
 */
void
cache_init (void) {
    cache_hits = 0;
    cache_misses = 0;
    clock_hand = 0;
    lock_init (&cache_lock);
    int i;
    for (i = 0; i < CACHE_SIZE; i++) {
        cache[i].sector = -1;
        cache[i].valid_bit = false;
        cache[i].dirty_bit = false;
        cache[i].ref_bit = false;
        cache[i].active_readers = 0;
        cache[i].waiting_readers = 0;
        cache[i].active_writers = 0;
        cache[i].waiting_writers = 0;
        cache[i].flush_or_load = false;
        cond_init (&cache[i].okToWrite);
        cond_init (&cache[i].okToRead);
        cond_init (&cache[i].flush_or_load_cond);
        lock_init (&cache[i].block_lock);
        memset(cache[i].cached_data, 0, sizeof(uint8_t) * BLOCK_SECTOR_SIZE);
    }
}

/**
 * Function: get_cache_hit_rate
 * ----------------------------
 * Computes hit rate of cache system so far as rounded down percentage
 * 
 * returns: cache hit rate
 */
int
get_cache_hit_rate (void) {
    return cache_hits * 100 / (cache_hits + cache_misses);
}

/**
 * Function: cache_read_block
 * --------------------------
 * Reads an entire sector into buffer.
 *
 * block_sector_t sector: sector to be read into buffer
 * void * buffer: receiving buffer
 */
void
cache_read_block (block_sector_t sector, void * buffer) {
    cache_read_size (sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

/**
 * Function: cache_write_block
 * ---------------------------
 * Writes an entire sector from buffer.
 * Also uses a write back scheme so dirty sectors are only written back on flushing
 *
 * block_sector_t sector: sector to write to
 * void * buffer: source buffer
 */
void
cache_write_block (block_sector_t sector, const void * buffer) {
    cache_write_size (sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

/**
 * Function: clock_replace
 * -----------------------
 * Finds the next available sector that is able to be evicted and used to load in new data
 * according to the following algorithm:
 *
 * Algorithm -starting at the current clock_hand (wraps betwen [0, CACHE_SIZE)):
 *      - lock the cache block at cache[clock_hand]
 *      - if cache[clock_hand] is not a valid sector, return this, increment hand, release global
 *      - if cache is loading, flushing, or has any active users, release local, lock, increment hand
 *      - f cache has its ref_bit set, unset it, release local lock, and increment hand
 *      - else -> free cache block, return this, release global lock, increment hand
 *
 * Returns: int index of evicted / free position within cache array
 * 
 * Synchronization assumptions:
 *      - global cache lock is held at start
 *      - local cache locks may or may not be held
 *      - on return the calling function will hold a lock to cache block noted by return index
 */
int
clock_replace () {
    bool found = false;
    int index = clock_hand;
    while (!found) {
        lock_acquire (&cache[clock_hand].block_lock);
        if (!cache[clock_hand].valid_bit) {
            index = clock_hand;
            found = true;
        }
        if (cache[clock_hand].flush_or_load || cache[clock_hand].active_readers +
            cache[clock_hand].waiting_readers + cache[clock_hand].active_writers +
            cache[clock_hand].waiting_writers > 0) {
            lock_release (&cache[clock_hand].block_lock);
        } else if (cache[clock_hand].ref_bit) {
            cache[clock_hand].ref_bit = false;
            lock_release (&cache[clock_hand].block_lock);
        } else {
            index = clock_hand;
            found = true;
        }
        clock_hand = (clock_hand + 1) % CACHE_SIZE;
    }
    return index;
}

/**
 * Function: cache_fetch_index
 * ---------------------------
 * Finds specified sector in cache and returns its index. If not in cache, will read it in
 * and return the index used.
 * 
 * block_sector_t sector: requested sector
 * 
 * Returns: int index of sector's location in cache
 * 
 * Synchronization assumptions:
 *      - global cache lock is held at start
 *      - on return, cache[index].block_lock is held and global cache lock is released
 */
int
cache_fetch_index (block_sector_t sector) {
    int index = check_cache_for_block (sector);
    if (index == -1) {
        cache_misses++;
        index =  push_cache_block (sector, clock_replace);
    } else {
        cache_hits++;
        lock_release (&cache_lock);
    }
    return index;
}

/**
 * Function: cache_read_size
 * -------------------------
 * Reads specified amount of data from cache to buffer. If not in buffer, will read sector
 * into a cache block first.
 * 
 * Algorithm:
 *      - check to see if requested sector is in cache
 *          - if not, read it in from disk to cache
 *      - wait until the block is done reading in or flushing
 *      - wait for any active writers (simultaneous read is OK)
 *      - update meta data and read our data from cache to buffer
 *      - update meta data and signal waiting writers (writers biased)
 * 
 * block_sector_t sector: requested sector
 * void * buffer: destination buffer for read in data
 * off_t size: requested amount of data to read
 * off_t offset: offset location to start reading
 */
void
cache_read_size (block_sector_t sector, void * buffer, off_t size, off_t offset) {
    lock_acquire (&cache_lock);
    int index = cache_fetch_index (sector);
    cache[index].waiting_readers++;
    while (cache[index].flush_or_load) {
        cond_wait (&cache[index].flush_or_load_cond, &cache[index].block_lock);
    }
    while (cache[index].active_writers + cache[index].waiting_writers > 0) {
        cond_wait (&cache[index].okToRead, &cache[index].block_lock);
    }
    cache[index].active_readers++;
    cache[index].waiting_readers--;
    cache[index].ref_bit = true;
    lock_release (&cache[index].block_lock);
    if (buffer)
        memcpy (buffer, cache[index].cached_data + offset, size);
    lock_acquire (&cache[index].block_lock);
    cache[index].active_readers--;
    if (cache[index].active_readers == 0 && cache[index].waiting_writers > 0)
        cond_signal (&cache[index].okToWrite, &cache[index].block_lock);
    lock_release (&cache[index].block_lock);
}

/**
 * Function: cache_write_size
 * --------------------------
 * Writes specified amount of data from buffer to sector (stored sector in cache). 
 * If not in buffer, will read sector into a cache block first prior to writing anything
 * Writes will be flushed to disk on eviction of cache block or shutdown.
 * 
 * Algorithm:
 *      - check to see if requested sector is in cache
 *          - if not, read it in from disk to cache (prevents the need of a bounce buffer)
 *      - wait until the block is done reading in or flushing
 *      - wait for any active writers or readers to the cache block
 *      - update meta data and write our data from buffer to cache
 *      - update meta data and signal waiting writers, if none then broadcast readers (writer bias)
 * 
 * block_sector_t sector: requested sector
 * void * buffer: source buffer for writing data
 * off_t size: requested amount of data to write
 * off_t offset: offset location to start writing
 */
void
cache_write_size (block_sector_t sector, const void * buffer, off_t size, off_t offset) {
    lock_acquire (&cache_lock); /* global */
    int index = cache_fetch_index (sector);
    cache[index].waiting_writers++;
    while (cache[index].flush_or_load) {
        cond_wait (&cache[index].flush_or_load_cond, &cache[index].block_lock);
    }
    while (cache[index].active_writers + cache[index].active_readers > 0) {
        cond_wait (&cache[index].okToWrite, &cache[index].block_lock);
    }

    cache[index].active_writers++;
    cache[index].waiting_writers--;
    cache[index].ref_bit = true;
    cache[index].dirty_bit = true;
    lock_release (&cache[index].block_lock);
    memcpy (cache[index].cached_data + offset, buffer, size);
    lock_acquire (&cache[index].block_lock);
    cache[index].active_writers--;
    if (cache[index].waiting_writers > 0)
        cond_signal (&cache[index].okToWrite, &cache[index].block_lock);
    else
        cond_broadcast (&cache[index].okToRead, &cache[index].block_lock);
    lock_release (&cache[index].block_lock);
}

/**
 * Function: check_cache_for_block
 * -------------------------------
 * Checks cache to see if requested sector is present
 * 
 * block_sector_t sector: requested sector
 * 
 * returns: index of requested sector or -1 if not present in cache
 * 
 * Synchronization assumptions:
 *      - calling fn should have acquired global lock
 *      - if requested sector is found in cache, calling fn is responsible for releasing its lock
 */
int
check_cache_for_block (block_sector_t sector) {
    int index;
    for (index = 0; index < CACHE_SIZE; index ++) {
        lock_acquire (&cache[index].block_lock);
        if (cache[index].sector == sector && cache[index].valid_bit == true) {
            return index;
        } else {
            lock_release (&cache[index].block_lock);
        }
    }
    return -1;
}

/**
 * Function: push_cache_block
 * --------------------------
 * Pushes requested sector into the cache. If no space is left on the cache, will call
 * pop_cache_block to make space. See pop_cache_block and specified replacement policy
 * for more details on the algorithm.
 * 
 * block_sector_t sector: requested sector
 * int (*replacement_policy)(void): replacement policy to use if eviction is necessary
 * 
 * returns: index of requested sector
 * 
 * Synchronization assumptions:
 *      - calling fn should have acquired global lock
 *      - this fn will release the global lock and acquire the local lock on the newly pushed
 *          cache block (caller will be responsible for releasing it)
 *      - this fn does not hold the local lock while reading data from disk to cache in order
 *          to prevent busy waiting of other threads
 */
int
push_cache_block (block_sector_t sector, int (*replacement_policy)(void)) {
    int index = pop_cache_block (replacement_policy, sector);
    cache[index].dirty_bit = false;
    cache[index].ref_bit = false;
    cache[index].flush_or_load = true;
    lock_release (&cache[index].block_lock);
    block_read (fs_device, sector, cache[index].cached_data);
    lock_acquire (&cache[index].block_lock);
    cache[index].valid_bit = true;
    cache[index].flush_or_load = false;
    cond_broadcast (&cache[index].flush_or_load_cond, &cache[index].block_lock);
    return index;
}

/**
 * Function: pop_cache_block
 * -------------------------
 * Pops a sector off the cache specified by the replacement policy. If the block is dirty,
 * calls flush_cache_block first before eviction
 * 
 * int (*replacement_policy)(void): replacement policy to use during eviction
 * block_sector_t new_sector: new requested sector
 * 
 * returns: index of evicted sector
 * 
 * Synchronization assumptions:
 *      - calling fn should have acquired global lock
 *      - this fn will release the global lock and acquire the local lock on the evicted block
 *          (caller will be responsible for releasing it)
 */
int
pop_cache_block (int (*replacement_policy)(void), block_sector_t new_sector) {
    int index = replacement_policy ();
    block_sector_t old_sector = cache[index].sector;
    cache[index].sector = new_sector;
    lock_release (&cache_lock);
    if (cache[index].dirty_bit) {
        cache[index].flush_or_load = true;
        lock_release (&cache[index].block_lock);
        block_write (fs_device, old_sector, cache[index].cached_data);
        lock_acquire (&cache[index].block_lock);
    }
    return index;
}

/**
 * Function: flush_cache_block
 * ---------------------------
 * Flushes specified cache block to disk
 * 
 * int index: specified index of cache_block to flush to disk
 * 
 * Synchronization assumptions:
 *      - calling fn should have acquired local lock on cache[index] block
 *      - this fn releases the local lock on cache[index] while writing to disk to prevent
 *          busy waiting of other threads
 */
void
flush_cache_block (int index) {
    cache[index].flush_or_load = true;
    lock_release (&cache[index].block_lock);
    block_write (fs_device, cache[index].sector, cache[index].cached_data);
    lock_acquire (&cache[index].block_lock);
    cache[index].flush_or_load = false;
    cache[index].dirty_bit = false;
    cond_broadcast (&cache[index].flush_or_load_cond, &cache[index].block_lock);
}

/**
 * Function: flush_cache
 * ---------------------
 * Flushes all dirty sectors of cache to disk (e.g. called during shutdown)
 */
void
flush_cache (void) {
    int index;
    for (index = 0; index < CACHE_SIZE; index++) {
        lock_acquire (&cache[index].block_lock);
        if (cache[index].dirty_bit == true) {
            if (!cache[index].flush_or_load)
                flush_cache_block (index);
        }
        lock_release (&cache[index].block_lock);
    }
}
