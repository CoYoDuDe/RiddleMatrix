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
cat > "$PROFILE_DIR/user.js" <<'EOF'
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.tabs.warnOnClose", false);
user_pref("browser.sessionstore.resume_from_crash", false);
user_pref("browser.cache.disk.enable", false);
user_pref("media.hardware-video-decoding.enabled", false);
user_pref("gfx.webrender.force-disabled", true);
user_pref("layers.acceleration.disabled", true);
EOF

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

if command -v openbox >/dev/null 2>&1; then
  openbox >/tmp/riddlematrix-openbox.log 2>&1 &
fi

export MOZ_DISABLE_RDD_SANDBOX=1
export MOZ_WEBRENDER=0
export LIBGL_ALWAYS_SOFTWARE=1

if command -v dbus-run-session >/dev/null 2>&1; then
  exec dbus-run-session -- /usr/bin/firefox-esr \
    --no-remote \
    --profile "$PROFILE_DIR" \
    --kiosk \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    "$URL"
fi

exec /usr/bin/firefox-esr \
  --no-remote \
  --profile "$PROFILE_DIR" \
  --kiosk \
  --width "$WIDTH" \
  --height "$HEIGHT" \
  "$URL"
