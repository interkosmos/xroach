# xroach

The classic `xroach` for X11: a game of skill. Try to find the roaches
under your windows. Cleaned up for modern C compilers. Please be aware
that this version has several bugs and glitches.

## Build
```
$ cmake .
$ make
```

## Run
```
$ ./xroach -speed 0.05 -squish -rc brown -rgc yellowgreen
```

## Copyright
Original copyright 1991 by J.T. Anderson. Squish option contributed by
Rick Petkiewizc. Virtual root code adapted from patch sent by Colin
Rafferty who borrowed it from Tom LaStrange. Several other folks sent
similar fixes.
