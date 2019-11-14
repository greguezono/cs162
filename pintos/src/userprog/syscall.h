#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>

void syscall_init (void);
void exit(int);
/* Test helpers */
unsigned grab_reads(void);
void force_clear(void);
int cache_hitrate(void);


#endif /* userprog/syscall.h */
