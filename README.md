# mmdtwm
mmdwm - Martin's Mouse Driven Tiling Window Manager

## Compile
```
make
```

## Binds
* `Mod4-Shift-e` - Exit WM
* `Mod4-Shift-q` - Exit window
* `Mod4-d` - dmenu
* `Mod4-a` - Raise window

## Run for testing
```
Xephyr -br -ac -noreset -screen 1024x768 :1 &
DISPLAY=:1 ./mmdtwm &
```

