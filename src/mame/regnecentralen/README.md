# regnecentralen

**CURRENT STATE: work in progress taking of from where it was left.**

This folder contains source code and documentation related to emulation of some of the computers and hardware produced by Regnecentralen, a Danish computer manufacturer. 

The RC700 is a 8-bit Z80 machine with either 8" external floppy drives or built-in 5,25" floppy drives, 64 Kb RAM and an external keyboard connected with the parallel port.

The RC750 Partner/RC759 Piccoline was their 16-bit machine based on 80186, with the Partner targetting businesses and the Piccoline targetting the Danish school system.  It ran the technically superior CCP/M-86 system which allowed for 4 virtual consoles, but wasn't 100% compatible with the IBM PC.

## Source Files

- [`rc702.cpp`](rc702.cpp): Implements the driver and emulation logic for the Regnecentralen RC700 computer, including Z80 CPU, memory mapping, and device support.
- [`rc759.cpp`](rc759.cpp): Contains the driver for the Regnecentralen RC759 Piccoline, handling system emulation, peripherals, and video output.
- [`rc759_kbd.h`](rc759_kbd.h):  
- [`rc759_kbd.cpp`](rc759_kbd.cpp): Emulation of the keyboard scanning input

## RC702: Getting started

For now, copy '*.rom' from a clone of https://github.com/ringgaard/rc700 to `mame/roms/rc702` and copy `cp roa375.rom roa375.ic66`.

Now build MAME and run it similar to:

```sh
./mame rc702 -window -skip_gameinfo 
```

You should get a yellow screen saying "***NO PROGRAM OR LINEPROG" which is the ROM saying it cannot find a boot sector on the floppy.


## References:

* https://ddhf.dk/wiki/RC700_Piccolo
* 