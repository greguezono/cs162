#include <ctype.h>
#include "tests/lib.h"

const char *test_name = "wait-helper";

int
main (int argc UNUSED, char *argv[])
{
	int status = wait (*argv[1]);
	msg("status = %d", status);
	return status;
}
