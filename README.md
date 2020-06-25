# martwm
martwm - Martin's Window Manager for X made in C99 and xcb

## Dependencies
* [xcb](https://xcb.freedesktop.org/) - Usually part of any systems with Xorg/X11 installed
  * xcb-randr
  * xcb-keysyms
  * xcb-ewmh

## Compile
To compile the WM (as release build):
```
make
```

To compile the WM as debug build:
```
make debug
```

Install the WM:
```
make clean install
```

Uninstall:
```
make uninstall
```

## Usages

### Mouse
* `Mod4-Mouse1` - Move window
* `Mod4-Mouse3` - Resize window

### Binds
* `Mod4-Shift-e` - Exit WM
* `Mod4-Shift-q` - Exit window
* `Mod4-d` - dmenu
* `Mod4-a` - Raise window
* `Mod4-b` - Toggle bar

## Run for testing
```
Xephyr -br -ac -noreset -screen 1024x768 :1 &
DISPLAY=:1 ./martwm
```

