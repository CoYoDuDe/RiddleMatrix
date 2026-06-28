#!/bin/bash
set -u

URL="http://127.0.0.1:8080"
PROFILE_DIR="/home/kioskuser/.mozilla/riddlematrix-kiosk"

# Keep kiosk startup tolerant across graphics drivers.
xset -dpms >/dev/null 2>&1 || true
xset s off >/dev/null 2>&1 || true
xset s noblank >/dev/null 2>&1 || true
xsetroot -solid black >/dev/null 2>&1 || true

mkdir -p "$PROFILE_DIR"

# The webserver starts in parallel. Wait for it so Firefox does not open a
# blank error page during boot on slower USB sticks.
for _ in $(seq 1 120); do
  if /usr/bin/curl -fsS "$URL/api/hello" >/dev/null 2>&1 || /usr/bin/curl -fsS "$URL/" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done

RESOLUTION=$(xrandr 2>/dev/null | grep '*' | head -n1 | awk '{print $1}')
WIDTH=$(echo "$RESOLUTION" | cut -d'x' -f1)
HEIGHT=$(echo "$RESOLUTION" | cut -d'x' -f2)

if [ -z "$WIDTH" ] || [ -z "$HEIGHT" ]; then
  WIDTH=1280
  HEIGHT=720
fi

exec /usr/bin/firefox-esr \
  --no-remote \
  --profile "$PROFILE_DIR" \
  --kiosk \
  --width "$WIDTH" \
  --height "$HEIGHT" \
  "$URL"
