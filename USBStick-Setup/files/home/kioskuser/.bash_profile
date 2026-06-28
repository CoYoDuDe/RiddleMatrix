if [ -z "${DISPLAY:-}" ] && [ "$(tty)" = "/dev/tty1" ]; then
  exec startx /home/kioskuser/start_firefox.sh -- :0 vt1 -nolisten tcp
fi
