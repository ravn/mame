# regnecentralen

**CURRENT STATE: work in progress taking of from where it was left.**

This folder contains source code and documentation related to emulation of some of the computers and hardware produced by Regnecentralen, a Danish computer manufacturer. 

The RC702 is a 8-bit Z80 machine with either 8" external floppy drives or built-in 5,25" 360 Kb floppy drives, 64 Kb RAM, an optional 10 Mb Winchester hard disk and an external keyboard connected with the parallel port.  There is a 2 Kb boot prom ("autoload") which is presented as the bios in MAME.  The successor RC703 had high density 5,25" 1.2 Mb diskettes, a larger bios and slightly changed handling of it.  MAME only emulates RC702.

The RC750 Partner/RC759 Piccoline was their 16-bit machine based on 80186, with the Partner targetting businesses and the Piccoline targetting the Danish school system.  It ran the technically superior CCP/M-86 system which allowed for 4 virtual consoles, but wasn't 100% compatible with the IBM PC.

## Source Files

- [`rc702.cpp`](rc702.cpp): Implements the driver and emulation logic for the Regnecentralen RC700 computer, including Z80 CPU, memory mapping, and device support.
- [`rc759.cpp`](rc759.cpp): Contains the driver for the Regnecentralen RC759 Piccoline, handling system emulation, peripherals, and video output.
- [`rc759_kbd.h`](rc759_kbd.h):  
- [`rc759_kbd.cpp`](rc759_kbd.cpp): Emulation of the keyboard scanning input

## RC702: Getting started

For now, copy '*.rom' from a clone of https://github.com/ringgaard/rc700 to `mame/roms/rc702` and copy `cp roa375.rom roa375.ic66`.  Note that the IMD images in this project are not compatible with MAME due to a different sector offset.

Now build MAME using something like (-j10 requires a modern machine):

```sh
make SUBTARGET=regnecentralen SOURCES=src/mame/regnecentralen/rc702.cpp TOOLS=1 SYMLEVEL=3  SYMBOLS=1 -j 10
```

and run it similar to:

```sh
./regnecentralen rc702 -debug -window -skip_gameinfo -nothrottle -flop1 ../../Downloads/COMAL_v1.07_SYSTEM_RC702.imd
```

You should get a yellow screen saying either "** NO PROGRAM OR LINEPROG" which is the ROM saying it cannot find a boot sector on the floppy, or "** BAD DISKETTE" which mean that the sanity check on the diskette read failed.


## References:

* Variuos materials: https://ddhf.dk/wiki/RC700_Piccolo
* Technical manual, not searchable:  https://ddhf.dk/w/images/5/5b/RC702_Tech_Man.pdf 

