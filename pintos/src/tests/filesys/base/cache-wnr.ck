# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-wnr) begin
(cache-wnr) create "dbz"
(cache-wnr) open "dbz"
(cache-wnr) good cache
(cache-wnr) end
EOF
pass;
