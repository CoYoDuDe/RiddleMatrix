#!/bin/bash
# Deaktiviere Energiesparmodi
xset -dpms
xset s off
xset s noblank

# Ermittle die aktuelle Auflösung des Bildschirms
RESOLUTION=$(xrandr | grep '*' | head -n1 | awk '{print $1}')
WIDTH=$(echo $RESOLUTION | cut -d'x' -f1)
HEIGHT=$(echo $RESOLUTION | cut -d'x' -f2)

# Falls die Auflösung nicht ermittelt werden kann, setze eine Standardauflösung
if [ -z "$WIDTH" ] || [ -z "$HEIGHT" ]; then
  WIDTH=1280
  HEIGHT=720
fi

# Starte Firefox mit der ermittelten Auflösung
/usr/bin/firefox-esr --kiosk --width $WIDTH --height $HEIGHT http://localhost:8080