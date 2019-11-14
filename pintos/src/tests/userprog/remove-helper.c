/* a program to test */
#include <syscall.h>
#include <stdio.h>
#include "tests/lib.h"

const char *test_name = "remove-helper";

int
main (void)
{
  int r = remove ("sample.txt");
  if (!r)
    return 81;
  return -1;
}
