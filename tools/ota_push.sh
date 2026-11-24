#!/bin/sh
# Push one built firmware.bin to the fleet from the N100.
#
#   ./ota_push.sh firmware.bin <token> 192.168.10.191 192.168.10.171 192.168.10.50
#
# One command for the whole fleet, two transports underneath. Which one is used
# is discovered per host, not configured:
#
#   Arduino nodes - wind, water, gentemp, CYD   espota over UDP 3232
#   ESP-IDF node  - the SeaKeeper relay         HTTP POST /ota on :8080
#
# The relay cannot take espota (ArduinoOTA does not exist on ESP-IDF) and cannot
# serve /status on port 80 (80 is relayed to the SeaKeeper), so a host that
# answers on :8080 is treated as the relay. That port is the only difference the
# operator sees.
#
# The N100 is the uploader, and each device only serves OTA while it is
# associated to the primary network the N100 hosts. Nothing here is tied to
# the N100's hardware: the token identifies the fleet, and the network is the
# gate. If the N100 is replaced, copy espota.py to the new one and use the same
# token.
#
# espota.py ships with the ESP32 Arduino core. Copy it to the N100 once:
#   scp ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py \
#       pi@bossy.local:~/ota/espota.py
# Override its location with ESPOTA=/path/to/espota.py.
# Override the relay's port with IDF_OTA_PORT=8080.

set -eu

if [ "$#" -lt 3 ]; then
  echo "usage: $0 <firmware.bin> <token> <host> [host...]" >&2
  exit 2
fi

BIN=$1
TOKEN=$2
shift 2

ESPOTA=${ESPOTA:-$HOME/ota/espota.py}
IDF_OTA_PORT=${IDF_OTA_PORT:-8080}

[ -f "$BIN" ] || { echo "no such firmware: $BIN" >&2; exit 1; }

fail=0
for HOST in "$@"; do
  echo "== $HOST"

  # Ask the relay's /status first. Only the ESP-IDF node answers there, so this
  # is a discovery step rather than a setting. A sensor that is off falls
  # through to espota, which reports "No response from the ESP" - the familiar
  # message, and the right one.
  if curl -sS --fail --max-time 4 -o /dev/null \
          "http://$HOST:$IDF_OTA_PORT/status" 2>/dev/null; then
    echo "   esp-idf, POST /ota on :$IDF_OTA_PORT"

    # --data-binary, not -d: the latter strips newlines out of a binary. It also
    # guarantees the Content-Length that the receiver insists on.
    if ! curl -sS --fail --max-time 180 \
            -H "X-OTA-Token: $TOKEN" \
            -H "Content-Type: application/octet-stream" \
            --data-binary "@$BIN" \
            "http://$HOST:$IDF_OTA_PORT/ota" >/dev/null; then
      echo "!! $HOST refused the image (token, or the serial log)" >&2
      fail=1
      continue
    fi

    echo "   written, waiting for the reboot"
    sleep 12
    if ! curl -sS --fail --max-time 5 "http://$HOST:$IDF_OTA_PORT/status"; then
      echo "!! $HOST did not answer after the reboot" >&2
      fail=1
    fi
    echo
    continue
  fi

  if [ ! -f "$ESPOTA" ]; then
    echo "!! missing OTA client: $ESPOTA" >&2
    echo "   copy it from the ESP32 Arduino core (see the comment at the top)" >&2
    fail=1
    continue
  fi

  if ! python3 "$ESPOTA" -i "$HOST" -a "$TOKEN" -f "$BIN"; then
    echo "!! $HOST failed" >&2
    fail=1
  fi
done

exit "$fail"
