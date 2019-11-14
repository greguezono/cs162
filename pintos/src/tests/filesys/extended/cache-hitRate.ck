# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-hitRate) begin
(cache-hitRate) create "db"
(cache-hitRate) open "db"
(cache-hitRate) wrote all 1s
(cache-hitRate) open "db"
(cache-hitRate) read "db"
(cache-hitRate) open "db"
(cache-hitRate) read "db"
(cache-hitRate) good cache
(cache-hitRate) end
EOF
pass;
