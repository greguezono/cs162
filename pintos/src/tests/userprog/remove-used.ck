# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(remove-used) begin
remove-helper: exit(-1)
(remove-used) end
remove-used: exit(0)
EOF
pass;
