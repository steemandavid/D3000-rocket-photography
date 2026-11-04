#!/bin/bash
# Flash one CRIG unit after checking the board MAC (never flash the wrong board).
set -e
unit=$1
cd "$(dirname "$0")"
case "$unit" in
  node)   PORT=/dev/serial/by-id/usb-1a86_USB_Single_Serial_5CCC062602-if00; MAC=68:ee:8f:47:8e:c8 ;;
  remote) PORT=/dev/serial/by-id/usb-1a86_USB_Single_Serial_5CCC062258-if00; MAC=68:ee:8f:47:96:d4 ;;
  *) echo "usage: $0 node|remote"; exit 1 ;;
esac
. ~/esp/esp-idf/export.sh >/dev/null
got=$(esptool.py -p "$PORT" read_mac 2>/dev/null | awk '/^MAC:/{print tolower($2); exit}')
[ "$got" = "$MAC" ] || { echo "MAC mismatch on $PORT: got '$got', expected $MAC ($unit) — not flashing"; exit 1; }
idf.py -B build_$unit -D SDKCONFIG=build_$unit/sdkconfig -p "$PORT" flash
