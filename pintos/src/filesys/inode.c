#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
/* included files */
#include "threads/synch.h"
#include "cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
/* Set global constants here. PTRS_PER_BLOCK = number of pointers per indirect block.
  L0_CAPACITY = capacity for all direct pointers
  L1_CAPACITY = capacity for indirect pointer
  L2_CAPACITY = capacity for double indirect pointer */
#define PTRS_PER_BLOCK (BLOCK_SECTOR_SIZE / 4)
#define L0_CAPACITY (122 * BLOCK_SECTOR_SIZE)
#define L1_CAPACITY (PTRS_PER_BLOCK * BLOCK_SECTOR_SIZE)
#define L2_CAPACITY (PTRS_PER_BLOCK * PTRS_PER_BLOCK * BLOCK_SECTOR_SIZE)
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t sector;              /* Sector location of this inode_disk struct */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
    bool is_dir;                        /* true = inode_disk is a directory
                                           false = inode_disk is a file */
    block_sector_t direct[122];         /* direct pointers to sectors in memory */
    block_sector_t indirect;            /* indirect sector location */
    block_sector_t double_indirect;     /* double indirect sector */
  };

/* indirect block struct to be inserted into memory */
struct indirect_block {
  block_sector_t ptrs[PTRS_PER_BLOCK];
};

/* Helper functions for new inode (defined and explained at bottom) */
void inode_lock(struct inode *);
void inode_unlock(struct inode *);
void open_inodes_lock(void);
void open_inodes_unlock(void);
block_sector_t get_sector_from_indirect (block_sector_t, off_t);
block_sector_t get_sector_from_double_indirect (block_sector_t, off_t, off_t);
off_t index_direct(off_t);
off_t index_indirect(off_t);
off_t index_double_indirect_L1(off_t);
off_t index_double_indirect_L0(off_t);
block_sector_t allocate_calloced_block (void);
block_sector_t allocate_indirect_block (block_sector_t);
block_sector_t allocate_double_indirect_block (block_sector_t);
bool disk_inode_extend_to_len(struct inode_disk *, off_t);
bool disk_inode_extend_sector(struct inode_disk *);
struct indirect_block *get_indirect_block_from_mem(block_sector_t);
struct inode_disk *get_inode_disk_from_mem(block_sector_t);
void inode_free_all_sectors(struct inode *);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
    off_t length;                       /* File size in bytes. */
    bool is_dir;                        /* true = inode is a directory
                                           false = inode is a file */
    struct lock lock_inode;             /* inode lock for inode metadata accesses */
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos)
{
  ASSERT (inode_disk != NULL);
/* ------------------------------ Our Code Inserted Below here --------------------------------- */

  /* Case 1: direct pointer */
  if (pos < L0_CAPACITY) {
    off_t index = index_direct(pos);
    return inode_disk->direct[index];

  /* Case 2: indirect pointer */
  } else if (pos < L0_CAPACITY + L1_CAPACITY) {
    off_t index = index_indirect(pos);
    return get_sector_from_indirect(inode_disk->indirect, index);

  /* Case 3: double indirect pointer */
  } else if (pos < L0_CAPACITY + L1_CAPACITY + L2_CAPACITY) {
    off_t index_L1 = index_double_indirect_L1(pos);
    off_t index_L0 = index_double_indirect_L0(pos);
    return get_sector_from_double_indirect (inode_disk->double_indirect, index_L1, index_L0);
  }

  /* Case 4: failure case */
  return -1;
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
static struct lock lock_open_inodes;
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

/* Initializes the inode module. */
void
inode_init (void)
{
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  cache_init ();
  list_init (&open_inodes);
  lock_init(&lock_open_inodes);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL) {
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
    disk_inode->length = 0;
    success = disk_inode_extend_to_len(disk_inode, length);
    disk_inode->magic = INODE_MAGIC;
    disk_inode->sector = sector;
    disk_inode->is_dir = is_dir;
    /* Check before writing the disk_inode to memory */
    if (!success) {
      free(disk_inode);
      return false;
    }
    cache_write_block (sector, disk_inode);
    free(disk_inode);
  }
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. Acquire lock before */
  open_inodes_lock();
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          open_inodes_unlock();
          inode_reopen (inode);
          return inode;
        }
    }
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  lock_init(&inode->lock_inode);
  list_push_front (&open_inodes, &inode->elem);
  open_inodes_unlock();

  /* Instead of reading data into inode->data, read the data into an inode_disk. Reset length and
  is_dir as it might have changed. */
  struct inode_disk *inode_disk;
  inode_disk = malloc(sizeof(struct inode_disk));
  if (inode_disk == NULL) {
    return NULL;
  }
  cache_read_block (inode->sector, inode_disk);
  inode->is_dir = inode_disk->is_dir;
  inode->length = inode_disk->length;
  free(inode_disk);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL){
    inode_lock(inode);
    inode->open_cnt++;
    inode_unlock(inode);
  }

  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  inode_lock(inode);
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list, acquire and release lock. */
      open_inodes_lock();
      list_remove (&inode->elem);
      open_inodes_unlock();

      /* Deallocate blocks if removed. */
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
      /* If inode to be removed, need to free all data sectors associated with that inode */
      if (inode->removed) {
          inode_free_all_sectors(inode);
      }
      inode_unlock(inode);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
      free (inode);
      return;
    }
  inode_unlock(inode);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode_lock(inode);
  if (inode->open_cnt > 0) {
    inode->removed = true;
  } else {
    inode_free_all_sectors(inode);
  }
  inode_unlock(inode);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  /* Error check */
  if (offset >= inode->length) {
    return 0;
  }
  /* retrieve inode_disk from memory */
  struct inode_disk *inode_disk = get_inode_disk_from_mem(inode->sector);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_disk, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read_block (sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read chunk_size of the sector. */
          cache_read_size(sector_idx, buffer + bytes_read, chunk_size, sector_ofs);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  free(inode_disk);
/* ------------------------------ Our Code Inserted Below here --------------------------------- */

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  /* Retrieve inode_disk from disk */
  struct inode_disk *inode_disk = get_inode_disk_from_mem(inode->sector);

  /* Acquire lock before accessing metadata associated with inode */
  inode_lock(inode);

  /* If the length is less than the length needed for the write, extend the inode_disk */
  bool extended = false;
  if (inode_disk->length < offset + size) {
    extended = true;
    bool check = disk_inode_extend_to_len(inode_disk, offset + size);
    if (!check) {
      inode_unlock(inode);
      free(inode_disk);
      return 0;
    }
  } else {
    inode_unlock(inode);
  }

/* ------------------------------ Our Code Inserted Above here --------------------------------- */
  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_disk, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in sector (no longer need bytes left in inode since we added extension) */
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < sector_left ? size : sector_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write_block (sector_idx, buffer + bytes_written);
        }
      else
        {
          /* Write chunk_size of the sector to disk. */
          cache_write_size(sector_idx, buffer + bytes_written, chunk_size, sector_ofs);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
      /* Update inode memory length if the inode_disk was extended */
      if (inode->length < offset) {
        inode->length = offset;
      }
    }

  if (extended) {
    inode_unlock(inode);
  }
  /* Write the inode_disk to disk */
  cache_write_block (inode->sector, inode_disk);
  free(inode_disk);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. Added locks to implementation. */
void
inode_deny_write (struct inode *inode)
{
  inode_lock(inode);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode_unlock(inode);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. Added locks to implementation. */
void
inode_allow_write (struct inode *inode)
{
  inode_lock(inode);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  inode_unlock(inode);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->length;
}

/* -------------------------- Helper Functions Inserted Below here ---------------------------- */
/* Functions for inode and open_inodes list locks */
void
inode_lock(struct inode *inode) {
  lock_acquire(&inode->lock_inode);
}

void
inode_unlock(struct inode *inode) {
  lock_release(&inode->lock_inode);
}

void
open_inodes_lock(void) {
  lock_acquire(&lock_open_inodes);
}

void
open_inodes_unlock(void) {
  lock_release(&lock_open_inodes);
}

/* Function for directory */
bool
inode_is_dir(struct inode *inode) {
  return (inode != NULL) && (inode->is_dir);
}

/* Functions for retrieving block sector number from an indirect/double indirect block */
block_sector_t
get_sector_from_indirect (block_sector_t block, off_t index) {
  /* Retrieve indirect block from mem */
  struct indirect_block *indirect_block = get_indirect_block_from_mem(block);

  /* retrieve and return the sector associated with index */
  block_sector_t sector = indirect_block->ptrs[index];
  free(indirect_block);
  return sector;
}

block_sector_t
get_sector_from_double_indirect (block_sector_t block, off_t index_L1,
                                                off_t index_L0){
  return get_sector_from_indirect(
                      get_sector_from_indirect(block, index_L1), index_L0);
}

/* Functions for retrieving index from an direct/indirect/double indirect block. Used
'/' as integer floor operation. */
/* find index for a direct pointer */
off_t
index_direct(off_t pos) {
  return pos / BLOCK_SECTOR_SIZE;
}

/* find index for an indirect pointer */
off_t
index_indirect(off_t pos) {
  return (pos - L0_CAPACITY) / BLOCK_SECTOR_SIZE;
}

/* find index for the indirect pointer of a double indirect pointer */
off_t
index_double_indirect_L1(off_t pos) {
  return (pos - L0_CAPACITY - L1_CAPACITY) / L1_CAPACITY;
}
/* find index for the direct pointer of a double indirect pointer */
off_t
index_double_indirect_L0(off_t pos) {
  return ((pos - L0_CAPACITY) % L1_CAPACITY) / BLOCK_SECTOR_SIZE;
}

/* Functions for allocating new block sectors (direct/indirect/double indirect) */
block_sector_t
allocate_calloced_block (void) {
  block_sector_t sector;
  /* Allocate a new sector */
  if (!free_map_allocate(1, &sector)) {
    return -1;
  }
  /* Calloc a new sector */
  void *calloced_block = calloc(BLOCK_SECTOR_SIZE, sizeof(void *));
  if (calloced_block == NULL) {
    return -1;
  }
  /* Write the new block sector to memory */
  cache_write_block (sector, calloced_block);
  free(calloced_block);
  return sector;
}

block_sector_t
allocate_indirect_block (block_sector_t first_block) {
  block_sector_t sector;
  /* Allocate a new sector */
  if (!free_map_allocate(1, &sector)) {
    return -1;
  }
  /* Create an indirect_block struct */
  struct indirect_block *indirect_block = calloc(1, sizeof(struct indirect_block));
  if (indirect_block == NULL) {
    return -1;
  }
  /* Set first pointer in indirect block to the first_block and write to memory */
  indirect_block->ptrs[0] = first_block;
  cache_write_block (sector, indirect_block);
  free(indirect_block);
  return sector;
}

block_sector_t
allocate_double_indirect_block (block_sector_t first_block) {
  block_sector_t indirect_sector;
  block_sector_t double_indirect_sector;
  /* Allocate an indirect block, then allocate another indirect block on that indirect block
   to create a double indirect pointer. Return the double_indirect_sector created */
  indirect_sector = allocate_indirect_block(first_block);
  if ((int) indirect_sector == -1) {
    return -1;
  }
  double_indirect_sector = allocate_indirect_block(indirect_sector);
  return double_indirect_sector;
}

/* Functions for extending the size of the disk_inode */
bool
disk_inode_extend_to_len(struct inode_disk *inode_disk, off_t length) {
  /* Find the space needed and the space left in the inode_disk */
  off_t space_needed = length - inode_disk->length;
  off_t space_left = ROUND_UP(inode_disk->length, BLOCK_SECTOR_SIZE) - inode_disk->length;
  /* Case 1: Need to extend disk_inode. Keep adding new sectors until the inode_disk has enough
    space to hold the space_needed */
  if (space_needed > space_left) {
    bool check;
    inode_disk->length = ROUND_UP(inode_disk->length, BLOCK_SECTOR_SIZE);
    while (inode_disk->length < length) {
      check = disk_inode_extend_sector(inode_disk);
      /* Allocation failed, return false */
      if (!check) {
        return false;
      }
    }
    /* Set inode_disk->length to the new length */
    inode_disk->length = length;
    return true;
  } else {
  /* Case 2: No need to extend disk_inode. Set inode_disk->length to the new length */
    inode_disk->length = length;
    return true;
  }
}

bool
disk_inode_extend_sector(struct inode_disk *inode_disk) {
  /* Initialize a new sector to be added. Check for success */
  block_sector_t new_sector = allocate_calloced_block();
  if ((int) new_sector == -1) {
    return false;
  }
  /* Find current size of inode_disk and increment the size by a new block */
  off_t cur_size = inode_disk->length - 1;
  inode_disk->length += BLOCK_SECTOR_SIZE;

  /* Case 1: Add new sector to direct level. */
  if ((cur_size + BLOCK_SECTOR_SIZE) < L0_CAPACITY) {
    /* Find index of direct pointer to insert and set pointer to the new sector */
    off_t index = index_direct(cur_size + BLOCK_SECTOR_SIZE);
    inode_disk->direct[index] = new_sector;
    return true;

  /* Case 2: Add new sector to indirect level. 2 subcases explained below */
  } else if ((cur_size + BLOCK_SECTOR_SIZE) < L0_CAPACITY + L1_CAPACITY) {
    /* Case 2.1: A new indirect block must be allocated. Allocate a new indirect block.
                 Then set the indirect_pointer to that sector. */
    if (cur_size < L0_CAPACITY) {
      block_sector_t new_indirect_sector = allocate_indirect_block(new_sector);
      if ((int) new_indirect_sector == -1) {
        return false;
      }
      inode_disk->indirect = new_indirect_sector;
      return true;

    /* Case 2.2: Indirect block already allocated. Retrieve indirect block from memory
       Find index of indirect pointer to insert and set pointer to the new sector.
       Write the new block to memory */
    } else {
      off_t index = index_indirect(cur_size + BLOCK_SECTOR_SIZE);
      struct indirect_block *indirect_block = get_indirect_block_from_mem(inode_disk->indirect);
      if (indirect_block == NULL) {
        return false;
      }
      indirect_block->ptrs[index] = new_sector;
      cache_write_block (inode_disk->indirect, indirect_block);
      free(indirect_block);
      return true;
    }
  }

  /* Case 3: Add new sector to double indirect level. 3 subcases explained below */
  else if ((cur_size + BLOCK_SECTOR_SIZE) < L0_CAPACITY + L1_CAPACITY + L2_CAPACITY) {
    /* Case 3.1: Need to allocate a new double indirect block. Allocate a new double indirect
                 block. Then set the double_indirect pointer to that sector */
    if (cur_size < L0_CAPACITY + L1_CAPACITY) {
      block_sector_t new_double_indirect_sector = allocate_double_indirect_block(new_sector);
      if ((int) new_double_indirect_sector == -1) {
        return false;
      }
      inode_disk->double_indirect = new_double_indirect_sector;
      return true;

    /* Case 3.2: Double indirect block has already been allocated. Now need to check if we need
                 to allocate a new indirect_block or not. */
    } else {
      off_t index_L1_cur = index_double_indirect_L1(cur_size);
      off_t index_L1_new = index_double_indirect_L1(cur_size + BLOCK_SECTOR_SIZE);

      /* Case 3.2.1: Need to allocate a new indirect block for the double_indirect block. Retrieve
                     double indirect block from memory and allocate a new indirect block with the
                     new sector as the first entry. Then set the new indirect sector in the
                     double indirect block. */
      if (index_L1_new > index_L1_cur) {
        struct indirect_block *double_indirect_block =
                              get_indirect_block_from_mem(inode_disk->double_indirect);
        if (double_indirect_block == NULL) {
          return false;
        }
        block_sector_t new_indirect_sector = allocate_indirect_block(new_sector);
        if ((int) new_indirect_sector == -1) {
          return false;
        }
        double_indirect_block->ptrs[index_L1_new] = new_indirect_sector;
        cache_write_block (inode_disk->double_indirect, double_indirect_block);
        free(double_indirect_block);
        return true;

      /* Case 3.2.2: All blocks (double indirect and indirect) have already been allocated.
                     Lookup the indirect block, then the direct pointer to free memory. Set
                     the direct pointer to the new sector. */
      } else {
        struct indirect_block *double_indirect_block =
                            get_indirect_block_from_mem(inode_disk->double_indirect);
        if (double_indirect_block == NULL) {
          return false;
        }
        struct indirect_block *indirect_block =
                            get_indirect_block_from_mem(double_indirect_block->ptrs[index_L1_cur]);
        if (indirect_block == NULL) {
          return false;
        }
        off_t index_L0 = index_double_indirect_L0(cur_size + BLOCK_SECTOR_SIZE);
        indirect_block->ptrs[index_L0] = new_sector;
        cache_write_block (double_indirect_block->ptrs[index_L1_cur], indirect_block);
        free(double_indirect_block);
        free(indirect_block);
        return true;
      }
    }
  }
  /* Case 4: File is too large, cannot allocate more space */
  return false;
}

/* Function for retreiving inode_disk indirect block from memory */
struct indirect_block *
get_indirect_block_from_mem(block_sector_t indirect_sector) {
  struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
  if (indirect_block == NULL) {
    return NULL;
  }
  cache_read_block (indirect_sector, indirect_block);
  return indirect_block;
}

struct inode_disk *get_inode_disk_from_mem(block_sector_t disk_sector) {
  struct inode_disk *inode_disk = malloc(sizeof(struct inode_disk));
  if (inode_disk == NULL) {
    return NULL;
  }
  cache_read_block (disk_sector, inode_disk);
  return inode_disk;
}

/* Function for removing all allocated sectors from an inode. Called by inode_close function */
void
inode_free_all_sectors(struct inode *inode) {
  /* Retrieve the inode_disk from memory. */
  struct inode_disk *inode_disk = get_inode_disk_from_mem(inode->sector);

  /* Find the current size of the inode in terms of BLOCK_SECTOR_SIZE.
    Free all data sectors associated with the inode (all direct pointers to block_sector_t). */
  off_t cur_size = ROUND_UP(inode->length, BLOCK_SECTOR_SIZE);
  off_t pos;
  block_sector_t sector_to_free;
  for (pos = 0; pos < cur_size; pos += BLOCK_SECTOR_SIZE) {
    sector_to_free = byte_to_sector(inode_disk, pos);
    free_map_release(sector_to_free, 1);
  }

  /* release the indirect block sector */
  if (inode->length >= L0_CAPACITY) {
    free_map_release(inode_disk->indirect, 1);
  }

  /* Release the double indirect block sector and all indirect blocks associated with it */
  if (inode->length >= L0_CAPACITY + L1_CAPACITY) {
    /* Retrieve double_indirect block from memory. */
    struct indirect_block *double_indirect_block = get_indirect_block_from_mem(inode_disk->double_indirect);
    /* First iterate through all allocated indirect blocks and free them */
    off_t index;
    for (index = 0; index < index_double_indirect_L1(inode_disk->length); index ++) {
      sector_to_free = double_indirect_block->ptrs[index];
      free_map_release(sector_to_free, 1);
    }
    /* Then release double indirect block sector */
    free_map_release(inode_disk->double_indirect, 1);
  }

  /* Finally release the malloced inode_disk and release the sector associated with the inode */
  free(inode_disk);
  free_map_release(inode->sector, 1);
}

int
inode_num_open (struct inode *inode)
{
  return inode->open_cnt;
}
/* -------------------------- Helper Functions Inserted Above here ---------------------------- */
