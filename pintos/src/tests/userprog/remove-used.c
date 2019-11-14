/* This method will write a lot of data to simple.txt */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{

  int handle = open ("sample.txt");
  if (handle > 0) {
    char buff[] = "1";
    int i = 0;
    exec("remove-helper");
    for (; i < 10000; i++) {
        write(handle, buff, sizeof(buff));
    }
    close(handle);
  } else {
    msg("fail");
  }
}
