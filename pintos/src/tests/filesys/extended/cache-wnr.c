#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  // Write 100KB to a file (200 blocks).
  const int kb = 1028; 
  char buffer[kb];
  int i = 0;
  for (; i < kb; i++) {
    buffer[i] = '1';
  }
  // First we make a file using 200 blocks. (100KB)
  CHECK (create ("dbz", 100 * kb), "create \"dbz\"");
  int fd = open("dbz");

  CHECK (fd > 1, "open \"dbz\"");
  unsigned read_cnt = grab_reads();
  if (fd > 0) {
    int i = 0;
    for (; i < 100; i++) {
        write(fd, &buffer, kb);
    }
    // no changes should have occured in read_cnt after writing...
    CHECK (read_cnt == grab_reads(), "good cache");
  }
  // Close the file. Flush the cache

}