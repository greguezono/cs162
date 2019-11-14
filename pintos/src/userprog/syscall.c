#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "devices/input.h"
#include "filesys/free-map.h"
/* Included libraries */
#include "lib/string.h"
#include "threads/pte.h"
#include "devices/block.h"
#include "filesys/cache.h"

static void syscall_handler (struct intr_frame *);

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
/* Syscall Functions */
int practice(int);
void halt (void);
void exit(int);
int exec(const char *);
int wait(int);
bool create (const char *, unsigned);
bool remove (const char *);
int open (const char *);
int filesize (int);
int read (int, void *, unsigned);
int write (int, void *, unsigned);
void seek (int, unsigned);
unsigned tell (int);
void close (int);
bool chdir (const char *);
bool mkdir (const char *);
bool isdir (int);
int inumber (int);
bool readdir (int, char *);

/* Syscall Helper Functions */
static int get_user (const uint8_t *uaddr);
static bool check_mem (const uint32_t *uaddr, int size);
static void validate_mem (const uint32_t *uaddr, int size);
static void validate_string (const char *uaddr);

/* File System Helper Functions */
int get_new_fd (void);
struct file_descriptor *get_fd_from_fdtable(int);
void close_file_from_fdtable(int);
void close_all_owned (void);

/* ------------------------------ Our Code Inserted Above here --------------------------------- */

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
/* Checks if pointers and args are valid before calling each syscall function */
  validate_mem (args, sizeof (uint32_t));
  int sys_num = args[0];

  switch (sys_num) {
    case SYS_PRACTICE:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) practice((int) args[1]);
      break;

    case SYS_EXIT:
      validate_mem (args + 1, sizeof(uint32_t));
      exit((int) args[1]);
      break;

    case SYS_WAIT:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) wait((int) args[1]);
      break;

    case SYS_EXEC:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) exec ((char *) args[1]);
      break;

    case SYS_HALT:
      halt();
      break;

    case SYS_CREATE:
      validate_mem (args + 1, sizeof(uint32_t) * 2);
      f->eax = (uint32_t) create((char *) args[1], (unsigned) args[2]);
      break;

    case SYS_REMOVE:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) remove((char *) args[1]);
      break;

    case SYS_OPEN:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) open((char *) args[1]);
      break;

    case SYS_FILESIZE:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) filesize((int) args[1]);
      break;

    case SYS_READ:
      validate_mem (args + 1, sizeof(uint32_t) * 3);
      f->eax = (uint32_t) read((int) args[1], (void *) args[2], (unsigned) args[3]);
      break;

    case SYS_WRITE:
      validate_mem (args + 1, sizeof(uint32_t) * 3);
      f->eax = (uint32_t) write((int) args[1], (void *) args[2], (unsigned) args[3]);
      break;

    case SYS_SEEK:
      validate_mem (args + 1, sizeof(uint32_t) * 2);
      seek((int) args[1], (unsigned) args[2]);
      break;

    case SYS_TELL:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) tell((int) args[1]);
      break;

    case SYS_CLOSE:
      validate_mem (args + 1, sizeof(uint32_t));
      close((int) args[1]);
      break;

    case SYS_CHDIR:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) chdir((const char *) args[1]);
      break;

    case SYS_MKDIR:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) mkdir((const char *) args[1]);
      break;

    case SYS_ISDIR:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) isdir((int) args[1]);
      break;

    case SYS_INUMBER:
      validate_mem (args + 1, sizeof(uint32_t));
      f->eax = (uint32_t) inumber((int) args[1]);
      break;

    case SYS_READDIR:
      validate_mem (args + 1, sizeof(uint32_t) * 2);
      f->eax = (uint32_t) readdir((int) args[1], (char *) args[2]);
      break;
  }
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
}

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
int
practice(int p)
{
  return p + 1;
}

void
halt(void)
{
  shutdown_power_off();
}

void
exit(int exit_code)
{
  struct thread *t = thread_current();
  close_all_owned();
  t->exit_status->exit_code = exit_code;
  printf("%s: exit(%d)\n", (char *) t->name, exit_code);
  thread_exit();
}

int
exec(const char *cmdline)
{
  tid_t tid;
  /* Check valid pointer first */
  validate_string (cmdline);
  /* Check if file exists */
  char fp[20];
  extract_fp(cmdline, fp);

  struct file *file = filesys_open (fp);
  if (file == NULL) {
    return -1;
  }
  tid = process_execute(cmdline);
  if (tid == (tid_t) TID_ERROR) {
    return -1;
  }
  return tid;
}

int
wait(int pid)
{
  return process_wait(pid);
}

/* File Syscall Functions */
bool
create(const char *filename, unsigned size)
{
  bool ret_status;
  validate_string (filename);

  ret_status = filesys_create(filename, size);
  return ret_status;
}

bool
remove(const char *filename)
{
  bool ret_status;
  validate_string (filename);
  ret_status = filesys_remove(filename);
  return ret_status;
}

int
open(const char *filename)
{
  int ret_fd = -1;
  struct file *file;

  validate_string (filename);

  file = filesys_open(filename);
  if (file == NULL) {
    return -1;
  }
  ret_fd = thread_add_fd(thread_current(), file);
  return ret_fd;
}

int
filesize(int fd_num)
{
  int ret_size = -1;
  struct thread *cur_thread = thread_current();

  /* Error handling */
  if (fd_num == STDIN_FILENO || fd_num == STDOUT_FILENO) {
    exit(-1);
  }
  if (!thread_valid_fd(cur_thread, fd_num)) {
    exit(-1);
  }
  ret_size = file_length(cur_thread->fd_table[fd_num]);
  return ret_size;
}

int
read(int fd_num, void *buffer, unsigned size)
{
  int bytes_read = 0;
  struct thread *t = thread_current();

  validate_mem (buffer, size);

  if (fd_num == STDOUT_FILENO) {
    exit(-1);
  } else if (fd_num == STDIN_FILENO) {
    /* Count number of bytes in stdin and inserts stdin into buffer. Null
    terminates the buffer. */
    unsigned int i;
    uint8_t *buf = buffer;
    for (i = 0; i < size; i++) {
      *buf = input_getc();
      bytes_read++;
      buf++;
    }
    *(uint8_t *)buffer = '\0';
  } else if (thread_valid_fd(t, fd_num)) {
    /* Get file from thread's fd table. Calls file_read */
    struct file *f = thread_get_file(t, fd_num);
    if (f == NULL) {
      exit(-1);
    }
    bytes_read = file_read(f, buffer, size);
  }
  return bytes_read;
}

int
write(int fd_num, void *buffer, unsigned size)
{
  int bytes_written = 0;
  struct thread *t = thread_current();

  validate_mem (buffer, size);

  if (size <= 0) {
    return 0;
  }

  if (fd_num == STDIN_FILENO) {
    exit(-1);
  } else if (fd_num == STDOUT_FILENO) {
    putbuf(buffer, size);
    bytes_written = size;
  } else if (thread_valid_fd(t, fd_num)) {
    /* Get file from thread's fd table. Calls file_write */
    struct file *f = thread_get_file(t, fd_num);
    if (f == NULL) {
      exit(-1);
    }

    if (isdir (fd_num))
    {
      return -1;
    }

    bytes_written = file_write(f, buffer, size);
  }
  return bytes_written;
}

void
seek(int fd_num, unsigned position)
{
  struct thread *t = thread_current();
  struct file *f;

  f = thread_get_file(t, fd_num);
  if (f == NULL) {
    return;
  }
  file_seek(f, position);
  return;
}

unsigned
tell(int fd_num)
{
  unsigned pos = 0;
  struct thread *t = thread_current();
  struct file *f;

  f = thread_get_file(t, fd_num);
  if (f == NULL) {
    return pos;
  }
  pos = file_tell(f);
  return pos;
}

void
close(int fd_num)
{
  struct thread *t = thread_current();
  struct file *f;

  /* Check special STDIN or STDOUT cases. Check if valid fd */
  if (fd_num == STDIN_FILENO || fd_num == STDOUT_FILENO || !thread_valid_fd(t, fd_num)) {
    exit(-1);
  }
  /* Get fd from table. If fd == NULL, doesn't exist */
  f = thread_get_file(t, fd_num);
  if (f == NULL) {
    exit(-1);
  }
  file_close(f);
  thread_remove_fd(t, fd_num);
  return;
}

/* File Syscall Helper Functions */
void
close_all_owned (void)
{
  int i;
  struct thread *t = thread_current();
  struct file *f;
  for (i = 2; i < t->fd_table_allocated_size; i++) {
    f = thread_get_file(t, i);
    if (f != NULL || f != 0) {
      file_close(f);
    }
  }
  free(t->fd_table);
}
/* ------------------------------ Project 3 Inserted Below here --------------------------------- */

/* Change the current thread's cwd to dir */
bool
chdir (const char *dir)
{
  validate_string (dir);
  struct thread *current_thread = thread_current ();

  if (strcmp (dir, "/"))
  {
    struct dir *temp_dir;
    bool status = path_traverse (dir, &temp_dir);
    if (status)
    {
      dir_close (current_thread->curr_dir);
      current_thread->curr_dir = dir_open (dir_get_inode (temp_dir));
      return true;
    }
    else
      return false;
  }
  else
  {
    current_thread->curr_dir = dir_open_root ();
    return true;
  }

}

/* make a new directory */
bool
mkdir (const char *dir)
{
  validate_string (dir);

  bool traversed = false;
  struct dir *temp_dir;
  char *first_seg = malloc (strlen (dir) + 1);
  char *new_name = malloc (strlen (dir) + 1);

/* separate the path into all but last and last parts, 
traverse to the second to last part. check that all parts exist in between */

  path_splice(dir, first_seg, new_name);
  traversed = path_traverse(first_seg, &temp_dir);

  if (traversed)
  {
    block_sector_t sector = 0;
    if (free_map_allocate (1, &sector) && inode_create (sector, 0, true))
    {
      /* add new directory to directory we traversed to */
      bool added = dir_add(temp_dir, new_name, sector);
      if (added)
      {
        /* add "." and ".." directories and verify that they were added correctly */
        struct dir *made = dir_open (inode_open (sector));
        bool one_dot = dir_add (made, ".", sector);

        block_sector_t last_sector = inode_get_inumber (dir_get_inode (temp_dir));
        bool two_dots = dir_add (made, "..", last_sector);

        dir_close (made);

        if (one_dot && two_dots)
        {
          dir_close (temp_dir);
          free (first_seg);
          free (new_name);
          return true;
        }
        /* if anything went wrong, remove the new directory */
        dir_remove (temp_dir, new_name);
      }
    }
  }
  dir_close (temp_dir);
  free (first_seg);
  free (new_name);
  return false;
}

/* returns true if fd corresponds to directory, false if basic file */
bool
isdir (int fd)
{
  struct thread *t = thread_current ();
  return inode_is_dir (file_get_inode (thread_get_file(t, fd)));
}

/* get current thread's inode's inumber */
int
inumber (int fd)
{
  struct thread *t = thread_current ();
  return inode_get_inumber (file_get_inode (thread_get_file(t, fd)));
}

bool
readdir (int fd, char *name)
{
  validate_string (name);

  if (!isdir (fd))
    return false;

/* get inode from file's fd and open it */
  struct file *f = thread_get_file (thread_current (), fd);
  struct inode *inode = file_get_inode (f);
  struct dir *dir = dir_open (inode);

/* set dir offset, read, then set file's new position */
  dir->pos = file_tell (f);
  bool status = dir_readdir (dir, name);
  file_seek (f, dir->pos);
  free (dir);
  return status;


}

/* ------------------------------ Project 3 Inserted Above here --------------------------------- */
/* Reads a byte at user virtual address UADDR.
UADDR must be below PHYS_BASE.
Returns the byte value if successful, -1 if a segfault
occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:" : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* checks uaddr to uaddr + (size - 1) to ensure it is in valid user memory:
  1. First check if uaddr is NULL, if so return false
  2. Starting at uaddr (byte address) up to uaddr + (size - 1), call this curr_addr
    - Ensure curr_addr is in valid user memory space (below PHYS_BASE)
    - Ensure curr_addr is on a valid allocated user memory page
    - If either of the above fail, return false
    * We could possibly save time by just checking the start and end, but for large spaces
      we may 'skip' over a missing non-allocated page
  3. If the end is reached, return true.

  This function and the above get_user assume that we have modified page_fault() so that a page
  fault in the kernel merely sets %eax to 0xffffffff and copies its former value into eip.*/

static bool
check_mem (const uint32_t *uaddr, int size) {
  int i;
  for (i = 0; i < size; i++) {
    uint8_t *curr_addr = ((uint8_t *) uaddr) + i;
    if (!curr_addr || !is_user_vaddr (curr_addr) || (get_user (curr_addr) == -1))
      return false;
  }
  return true;
}

static void
validate_mem (const uint32_t *uaddr, int size) {
  if (!check_mem (uaddr, size))
    exit (-1);
}

static void
validate_string (const char *uaddr) {
  if (!uaddr)
    exit (-1);

  const char *curr = uaddr;
  char curr_letter;
  while (is_user_vaddr (curr) && (curr_letter = get_user ((uint8_t *) curr)) != -1) {
    if (curr_letter == '\0')
      return;
    curr ++;
  }
  exit (-1);
}

unsigned grab_reads(void) {
  return filesys_reads();
}
void force_clear(void) {
  flush_cache();
}
int cache_hitrate(void) {
  return get_cache_hit_rate();
}
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
