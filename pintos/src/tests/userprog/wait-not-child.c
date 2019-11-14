/* Wait for a process that is not a child of the current process. */
#include "tests/lib.h"
#include "tests/main.h"
#include <string.h>

const char *test_name = "wait-not-child";

static pid_t
spawn_child (int c)
{
  char child_cmd[128];
  snprintf (child_cmd, sizeof child_cmd,
            "wait-helper %d", c);
  pid_t stat = exec (child_cmd);
  return stat;
}

void
test_main (void)
{
  pid_t child = exec("child-loop");
  pid_t child2 = spawn_child(child);
  wait(child2);
}
