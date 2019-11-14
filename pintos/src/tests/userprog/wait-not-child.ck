# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(wait-not-child) begin
(child-loop) running
(wait-helper) status = -1
wait-helper: exit(-1)
(wait-not-child) end
wait-not-child: exit(0)
EOF
pass;
