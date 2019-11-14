#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/syscall.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
void extract_fp (const char *cmdline, char *fp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *cmdline)
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, cmdline, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  char fp[1024];
  extract_fp (cmdline, fp);

  struct load_status ls;
  ls.loaded = false;
  sema_init(&ls.sema, 0);
  ls.cmdline = fn_copy;
  ls.parent_t = thread_current();

  tid = thread_create (fp, PRI_DEFAULT, start_process, &ls);
  sema_down (&ls.sema);

  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);

  return ls.loaded ? tid : TID_ERROR;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *ls_)
{
  struct load_status *ls = ls_;
  char fp[20];
  extract_fp (ls->cmdline, fp);

  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  success = load (ls->cmdline, &if_.eip, &if_.esp);
  ls->loaded = success;
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  /* If load was successful, add child exit status to parent's list of children */
  if (success) {
    struct thread *cur = thread_current();
    /* First deny any writes to the file that will be executed. Set cur->executable_file to the files
    to allow writes after the thread exits. */
    cur->executable_file = filesys_open (fp);
    file_deny_write (cur->executable_file);

    /* Add child's exit_status struct to parent's list of children. */
    struct exit_status *exit_status = cur->exit_status;
    lock_acquire(&ls->parent_t->children_lock);
    list_push_back(&ls->parent_t->children, &exit_status->exit_elem);
    lock_release(&ls->parent_t->children_lock);
  }
  sema_up (&ls->sema);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
  /* If load failed, quit. */
  palloc_free_page (ls->cmdline);
  if (!success) {
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
    /* Must set ref counter to 1 since load failed */
    thread_current()->exit_status->ref_count = 1;
    thread_exit ();
/* ------------------------------ Our Code Inserted Above here --------------------------------- */
  }


  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED)
{
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  struct thread *cur = thread_current();
  int exit_code;
  /* Iterate over children list and find child_tid. Sema up on the exit_status semaphore so that
  the current thread waits for the child to exit. */
  struct list_elem *e;
  lock_acquire(&cur->children_lock);
  for (e  = list_begin (&cur->children); e != list_end (&cur->children); e  = list_next (e)) {
    struct exit_status *child_exit_status = list_entry(e, struct exit_status, exit_elem);
    if (child_exit_status->tid == child_tid && child_exit_status->ref_count > 0) {
      sema_down(&child_exit_status->sema);
      exit_code = child_exit_status->exit_code;

      /* Remove the thread from the children list. */
      list_remove (&child_exit_status->exit_elem);
      free(child_exit_status);
      lock_release(&cur->children_lock);
      return exit_code;
    }
  }
  /* If iterated over the whole list and no child process with child_tid found, return -1 */
  lock_release(&cur->children_lock);
  return -1;
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

/* ------------------------------ Our Code Inserted Below here --------------------------------- */
  /* Traverse the children and free exit_status of child processes. Go through each child in the
   children list. Decrement the ref count. If ref_count == 0, free the exit_status struct. Must
   acquire both children list lock and ref_count lock to prevent data races */
  struct list_elem *e;
  struct exit_status *child_exit_status;
  bool free_flag;
  lock_acquire(&cur->children_lock);
  for (e  = list_begin (&cur->children); e != list_end (&cur->children); e  = e) {
    free_flag = false;
    child_exit_status = list_entry(e, struct exit_status, exit_elem);
    lock_acquire(&child_exit_status->ref_lock);
    child_exit_status->ref_count--;
    if (child_exit_status->ref_count == 0) {
      list_remove(&child_exit_status->exit_elem);
      free_flag = true;
    }
    lock_release(&child_exit_status->ref_lock);
    e = list_next (e);
    if (free_flag) {
      free(child_exit_status);
    }
  }
  lock_release(&cur->children_lock);

  /* Try to free the current process exit_status struct. */
  struct exit_status *cur_exit_status = cur->exit_status;
  lock_acquire(&cur_exit_status->ref_lock);
  cur_exit_status->ref_count--;
  if (cur_exit_status->ref_count == 0) {
    if (!(cur_exit_status->exit_elem.next == NULL && cur_exit_status->exit_elem.prev == NULL))
      list_remove(&cur_exit_status->exit_elem);
    lock_release(&cur_exit_status->ref_lock);
    free(cur_exit_status);
  } else {
    lock_release(&cur_exit_status->ref_lock);
    sema_up(&cur->exit_status->sema);
  }

  /* reenable writes to the thread's executable file if it exists. Close the file as well */
  if (cur->executable_file) {
    file_allow_write(cur->executable_file);
    file_close(cur->executable_file);
  }
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
bool set_up_stack_args (const char *cmdline, char **esp);
void count_argc_chars (const char *cmdline, int *argc, int *chars);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *cmdline, void (**eip) (void), void **esp)
{
  char fp[20];
  extract_fp (cmdline, fp);

  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (fp);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", fp);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", fp);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp) || !set_up_stack_args (cmdline, (char **)esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

void
extract_fp (const char *cmdline, char *fp)
{
  const char *start = cmdline;
  while (*start == ' ')
    start ++;
  const char *end = start;
  while (*end != '\0' && *end != ' ')
    end ++;
  strlcpy (fp, start, end - start + 1);
}

void
count_argc_chars (const char *cmdline, int *argc, int *chars) {
  const char *start = cmdline;
  while (*start != '\0') {
    while (*start == ' ') {
      start ++;
    }
    if (*start != '\0' && *start != ' ')
     (*argc)++;
    while (*start != '\0' && *start != ' ') {
      (*chars)++;
      start ++;
    }
  }
}

bool
set_up_stack_args (const char *cmdline, char **esp) {
  int argc = 0;
  int chars = 0;
  count_argc_chars (cmdline, &argc, &chars);
  int bytes = chars + argc;
  bytes = ROUND_UP (bytes, sizeof(int));

  /* check mem out of page bounds here */
  if (bytes + (argc + 1) * sizeof (char *) + sizeof (char **) + sizeof (int) + sizeof(void *) > PGSIZE)
    return false;

  *esp -= bytes;
  char *arg_begin = (char *) *esp;

  *esp -= (argc + 1) * sizeof(char*);
  char **arg_pointer_begin = (char **) *esp;

  char *token;
  char *rest = NULL;

  for (token = strtok_r((char *)cmdline, " ", &rest); token != NULL || token != '\0'; token = strtok_r(NULL, " ", &rest)) {
    strlcpy (arg_begin, token, strlen(token) + 1);
    *arg_pointer_begin = arg_begin;
    arg_begin += strlen(token) + 1;
    arg_pointer_begin ++;
  }

  *arg_pointer_begin = 0;
  arg_pointer_begin = (char **) *esp;
  *esp -= sizeof(char**);
  ** (int** )esp = (int) *esp + sizeof(char**);
  *esp -= sizeof(int);
  ** (int**) esp = argc;
  *esp -= sizeof(void*);
  **esp = 0;

  return true;
}