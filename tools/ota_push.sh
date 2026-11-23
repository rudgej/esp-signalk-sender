#!/bin/sh
# Push one built firmware.bin to ESP32 sensors from the N100.
#
#   ./ota_push.sh firmware.bin <token> 192.168.10.191 192.168.10.192
#
# The N100 is the uploader, and each sensor only serves OTA while it is
# associated to the primary network the N100 hosts. Nothing here is tied to
# the N100's hardware: the token identifies the fleet, and the network is the
# gate. If the N100 is replaced, copy espota.py to the new one and use the same
# token.
#
# espota.py ships with the ESP32 Arduino core. Copy it to the N100 once:
#   scp ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py \
#       pi@bossy.local:~/ota/espota.py
# Override its location with ESPOTA=/path/to/espota.py.

set -eu

if [ "$#" -lt 3 ]; then
  echo "usage: $0 <firmware.bin> <token> <host> [host...]" >&2
  exit 2
fi

BIN=$1
TOKEN=$2
shift 2

ESPOTA=${ESPOTA:-$HOME/ota/espota.py}

[ -f "$BIN" ] || { echo "no such firmware: $BIN" >&2; exit 1; }
[ -f "$ESPOTA" ] || {
  echo "missing OTA client: $ESPOTA" >&2
  echo "copy it from the ESP32 Arduino core (see the comment at the top)" >&2
  exit 1
}

fail=0
for HOST in "$@"; do
  echo "== $HOST"
  if ! python3 "$ESPOTA" -i "$HOST" -a "$TOKEN" -f "$BIN"; then
    echo "!! $HOST failed" >&2
    fail=1
  fi
done

exit "$fail"
