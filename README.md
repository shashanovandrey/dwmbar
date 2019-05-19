# dwmbar
Simple statusbar for dwm (dynamic window manager). GNU/Linux

(keyboard layout, sound volume (ALSA), battery capacity, link status, date-time)

    Eng NL  Master:40%M  BAT0:98%  wlp3s0:up  2019-05-19 Sun 20:07

Set your: `IFACE`, `BATTERY`, `SND_CTL_NAME`, `MIXER_PART_NAME`

Dependences (Debian): libxcb1, libxcb1-dev, libxcb-xkb1, libxcb-xkb-dev, libasound2, libasound2-dev

Build: `gcc -O2 -s -lpthread -lxcb -lxcb-xkb -lasound -lm -o dwmbar dwmbar.c`
