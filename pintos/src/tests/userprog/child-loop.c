
#include <stdio.h>
#include "tests/lib.h"

const char *test_name = "child-loop";

int
main (void)
{
  msg("running");
  while (1);
  
  return 81;
}
