#!/bin/bash
# Build one CRIG unit:  ./build.sh node|remote [flash]
set -e
unit=$1
case "$unit" in node|remote) ;; *) echo "usage: $0 node|remote [flash]"; exit 1;; esac
cd "$(dirname "$0")"
[ -f components/crig_common/include/crig_binding.h ] || { echo "missing crig_binding.h: run tools/gen_binding.py first"; exit 1; }
. ~/esp/esp-idf/export.sh >/dev/null
idf.py -B build_$unit -D SDKCONFIG=build_$unit/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.$unit" build
if [ "$2" = flash ]; then ./flash.sh "$unit"; fi
