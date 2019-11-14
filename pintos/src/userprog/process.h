#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
/* ------------------------------ Our Code Inserted Below here --------------------------------- */
void extract_fp (const char *cmdline, char *fp);
/* ------------------------------ Our Code Inserted Above here --------------------------------- */

#endif /* userprog/process.h */
