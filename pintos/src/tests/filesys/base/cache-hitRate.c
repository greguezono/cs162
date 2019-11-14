#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  const int blocks = 1028;
  char buffer[blocks];
  int i = 0;
  for (; i < blocks; i++)
    buffer[i] = '1';
  // First we make a file using 2 blocks. (KB)
  CHECK (create ("db", blocks), "create \"db\"");
  int fd = open("db");
  CHECK (fd > 1, "open \"db\"");
  if (fd > 0) {
    int num = write(fd, &buffer, blocks);

    // Close the file. Flush the cache.
    CHECK(num == blocks, "wrote all 1s");
    close(fd);
  }
  
  force_clear();
  // Open the file, and read from the file.
  int fd2 = open("db");
  CHECK(fd2 > 1, "open \"db\"");
  if (fd2 > 0) {
    int i = 0;
    char t[1];
    for (; i < blocks;) {
      i += read(fd2, &t, 1);
    }
    CHECK(i == blocks, "read \"db\"");
    // Close the file. 
    close(fd2);
  }
  // Extract cchr := cold cache hit rate
  int cchr = cache_hr();
  // Open the file, and read sequentially from the file.
  int fd3 = open("db");
  CHECK(fd3 > 1, "open \"db\"");
  if (fd3 > 0) {
    int i = 0;
    char t[1];
    for (; i < blocks;) {
      i += read(fd3, &t, 1);
    }
    CHECK(i == blocks, "read \"db\"");
    // Close the file. 
    close(fd3);
  }
  // Extra chr := cache hit rate
  int chr = cache_hr();
  // Verify chr > cchr to pass the test.
  CHECK (chr > cchr, "good cache");

}