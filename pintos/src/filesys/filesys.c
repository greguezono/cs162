#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  flush_cache ();
  free_map_close ();
}
/* ------------------------------ Project 3 Code Inserted Below here --------------------------------- */
/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;
  struct dir *dir;
  char *first_seg = malloc (strlen (name) + 1);
  char *last_seg = malloc (strlen (name) + 1);

/* added support for nested and relative file paths */
  path_splice(name, first_seg, last_seg);
  path_traverse(first_seg, &dir);

  if (strlen (last_seg) > NAME_MAX) 
  {
    free (first_seg);
    free (last_seg);
    return false;
  }

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, last_seg, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);

  dir_close (dir);
  free (first_seg);
  free (last_seg);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */

struct file *
filesys_open (const char *name)
{
  /*struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);*/

  struct dir *dir;
  struct inode *inode = NULL;

  if (strcmp (name, "/"))
  {
  char *first_seg = malloc (strlen (name) + 1);
  char *last_seg = malloc (strlen (name) + 1);

/* added support for nested and relative file paths */
  path_splice(name, first_seg, last_seg);
  bool status = path_traverse(first_seg, &dir);

    if (dir != NULL && status)
    {
      dir_lookup (dir, last_seg, &inode);
      dir_close (dir);
    }
  }
  else
    inode = inode_open (ROOT_DIR_SECTOR);

  return file_open (inode);
}

/* ------------------------------ Project 3 Code Inserted Above here --------------------------------- */
/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct dir *dir;
  bool success = false;

  if (strcmp (name, "/"))
  {
    char *first_seg = malloc (strlen (name) + 1);
    char *last_seg = malloc (strlen (name) + 1);

/* added support for nested and relative file paths */
    path_splice(name, first_seg, last_seg);
    path_traverse(first_seg, &dir);

    if (strcmp (last_seg, ".") && strcmp (last_seg, ".."))
      success = dir != NULL && dir_remove (dir, last_seg);

    dir_close (dir);
    free (first_seg);
    free (last_seg);
  }
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* ------------------------------ Project 3 Code Inserted Below here --------------------------------- */
/* From Project 3 Spec */
int
get_next_part (char part[NAME_MAX + 1], char **srcp) 
{
  char *src = *srcp;
  char *dst = part;
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
    *dst = '\0';
    *srcp = src;
    return 1;
}

/* return 1 if path is absolute, 0 if relative */
int
absolute (char *path) {

  while (*path == ' ') 
    path++;
  
  if (*path == '/') 
    return 1;

  return 0;
}

/* split file path into first parts together and last segment */
bool
path_splice (const char *filepath, char *orig_seg, char *new_seg)
{
  char *empty_seg = "";

  if (!strcmp (filepath, ""))
  {
    /* return empty strings if file path is empty */
    memcpy (new_seg, empty_seg, strlen (empty_seg) + 1);
    memcpy (orig_seg, empty_seg, strlen (empty_seg) + 1);
    return true;
  }

  char *path_buf = malloc (strlen (filepath) + 1);
  memcpy (path_buf, filepath, (strlen (filepath) + 1));

  /* Get last segment of file path */
  char *last_seg = path_buf + strlen (path_buf) - 1;
  while (*last_seg != '/' && last_seg != path_buf)
    last_seg--;
  if (*last_seg == '/')
  {
    last_seg++;
    memcpy (new_seg, last_seg, strlen (last_seg) + 1);

    /* Get file path to directory in which to place new directory */
    int orig_len = strlen (path_buf) - strlen (last_seg);

    /* make sure to include leading slash */
    if (orig_len == 1)
      orig_len++; 

    strlcpy (orig_seg, filepath, orig_len);
    return true;
  }
  else
  {
    /* file path is relative and not nested (only one dir name and no slashes) */
    memcpy (new_seg, last_seg, strlen (last_seg) + 1);
    memcpy (orig_seg, empty_seg, strlen (empty_seg) + 1);
    free (path_buf);
    return true;
  }

}

/* Traverse through the file path to the desired file or directory */
bool 
path_traverse (const char *filepath, struct dir **dir)
{
  if (!strcmp(filepath, ""))
  {
    *dir = dir_open_cwd ();
    return true;
  }
/* make a temporary buffer containing the file path */
  char *path_buf = malloc (strlen (filepath) + 1);
  memcpy (path_buf, filepath, strlen (filepath) + 1);

/* create buffer to hold next dir's name, dir traverse through,
 and pointer to current segment of the path */
  char part[NAME_MAX + 1];
  struct dir *temp_dir;
  char *temp_ptr = path_buf;

/* if path is absolute start at root, else start at the cwd */
  if (absolute (temp_ptr))
  {
    temp_dir = dir_open_root ();
  }
  else
    temp_dir = dir_open_cwd ();

/* iterate through segments of the file path, break if a segment does not exist. */
  while (get_next_part (part, &temp_ptr))
  {
    struct inode *curr_node = NULL;

    bool status = dir_lookup (temp_dir, part, &curr_node);
    dir_close(temp_dir);

    if (status)
    {
      temp_dir = dir_open (curr_node);
    }
    else
    {
      free (path_buf);
      return false;
    }
  }
  free (path_buf);
  *dir = temp_dir;
  return true;

}

/* ------------------------------ Project 3 Code Inserted Above here --------------------------------- */








