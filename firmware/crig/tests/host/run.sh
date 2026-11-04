#!/bin/bash
set -e
cd "$(dirname "$0")"
inc=../../components/crig_common/include
src=../../components/crig_common
gcc -std=c11 -Wall -Wextra -Werror -I$inc test_common.c $src/crig_proto.c $src/crig_armdec.c -o /tmp/crig_host_test_$$
/tmp/crig_host_test_$$; r=$?; rm -f /tmp/crig_host_test_$$; exit $r
