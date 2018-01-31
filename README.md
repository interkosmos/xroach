# xroach
The classic `xroach` game for X11: displays disgusting cockroaches on your root
window. These creepy crawlies scamper around until they find a window to
hide under. Whenever you move or iconify a window, the exposed beetles again
scamper for cover.

Cleaned up for modern C compilers. Please be aware that this version still has
some bugs and glitches.

## Build
```
$ cmake .
$ make
```
In case you do not have an C++ compiler installed, instead run:
```
$ CXX=gcc cmake .
```

## Run
```
$ ./xroach -speed 2 -squish -rc brown -rgc yellowgreen
```

## Copyright
Original copyright 1991 by J. T. Anderson. Squish option contributed by
Rick Petkiewizc. Virtual root code adapted from patch sent by Colin
Rafferty who borrowed it from Tom LaStrange. Several other folks sent
similar fixes. Some glitches removed by patch from Guus Sliepen.
