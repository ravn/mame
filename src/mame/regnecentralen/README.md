# regnecentralen

**CURRENT STATE: work in progress taking of from where it was left.**

This folder contains source code and documentation related to emulation of some of the computers and hardware produced by Regnecentralen, a Danish computer manufacturer.

The RC702 is a 8-bit Z80 machine with either 8" external floppy drives or built-in 5,25" 360 Kb floppy drives, 64 Kb RAM, an optional 10 Mb Winchester hard disk and an external keyboard connected with the parallel port.  There is a 2 Kb boot prom ("autoload") which is presented as the bios in MAME.  The successor RC703 had high density 5,25" 1.2 Mb diskettes, larger eproms, and generally improved electronics.  MAME currently only works fully with the RC702, as the RC703 autoload prom triggers a bug in the floppy disk controller emulation.

The RC750 Partner/RC759 Piccoline was their 16-bit machine based on 80186, with the Partner targetting businesses and the Piccoline targetting the Danish school system.  It ran the technically superior CCP/M-86 system which allowed for 4 virtual consoles, but wasn't 100% compatible with the IBM PC.

## RC702: Keyboard

The RC702 keyboard is connected to Z80 PIO port A.  The driver wires the generic keyboard and (optionally) MAME’s natural keyboard into the PIO so that CP/M sees keypresses.

- **Emulated keyboard**: US-ASCII layout with typematic repeat (configurable delay and rate via MAME's Machine Configuration menu).
- **Natural keyboard**: In the MAME menu, choose **Keyboard Selection** → **Keyboard Mode** → **Natural** to use the host OS layout (e.g. Danish: Shift+2 → `"`, Æ/Ø keys correct).  Characters are sent as Latin-1 (0–255).

## Source Files

- [`rc702.cpp`](rc702.cpp): Implements the driver and emulation logic for the Regnecentralen RC702 Piccolo, including Z80 CPU, memory mapping, PIO keyboard path, and device support.
- [`rc759.cpp`](rc759.cpp): Contains the driver for the Regnecentralen RC759 Piccoline, handling system emulation, peripherals, and video output.
- [`rc759_kbd.h`](rc759_kbd.h):
- [`rc759_kbd.cpp`](rc759_kbd.cpp): Emulation of the keyboard scanning input

## RC702: Getting started

You need to build MAME first.  This script is clickable in IntelliJ/CLion - note -j10 requires a modern machine.

```sh
make -C ../../.. SUBTARGET=regnecentralen DEBUG=1 SOURCES=src/mame/regnecentralen/rc759.cpp,src/mame/regnecentralen/rc750.cpp,src/mame/regnecentralen/rc702.cpp,src/mame/regnecentralen/pio_port/pio_port.cpp,src/mame/regnecentralen/pio_port/keyboard.cpp,src/mame/regnecentralen/pio_port/cpnet_bridge.cpp REGENIE=1 TOOLS=1 SYMLEVEL=3  SYMBOLS=1  OSD=sdl -j 10
```

`SOURCES` is comma-separated (no spaces). Since upstream #15805 the PIO-port
slot lives in the driver folder (`pio_port/`), so its source files must be
listed explicitly alongside `rc702.cpp` — `cpnet_bridge.cpp` is the fork-only
CP/NET host-socket card. `OSD=sdl` builds against SDL2.


# Running the Piccolo in MAME

First download all known original ROMs to the correct location:

```sh
OUTPUT_DIR=../../../roms/rc702
mkdir -p $OUTPUT_DIR
curl --output-dir $OUTPUT_DIR -L -O https://github.com/ravn/rc700/raw/refs/heads/master/roa296.rom
curl --output-dir $OUTPUT_DIR -L -O https://github.com/ravn/rc700/raw/refs/heads/master/roa327.rom
curl --output-dir $OUTPUT_DIR -L -O https://github.com/ravn/rc700/raw/refs/heads/master/rob357.rom
curl --output-dir $OUTPUT_DIR -L -O https://github.com/ravn/rc700/raw/refs/heads/master/rob358.rom
curl --output-dir $OUTPUT_DIR -L -o roa375.ic66 https://github.com/ravn/rc700/raw/refs/heads/master/roa375.rom
echo "*** All ROMS should be 2048 bytes ***"
ls -l $OUTPUT_DIR
```

Note: In the following the images have been downloaded from https://ddhf.dk/wiki/Bits:Keyword/RC/RC700 - images from https://www.jbox.dk/rc702/disks.shtm have an off-by-one error and are not compatible with the emulated floppy controller.  TODO:  Full scripting of downloads.


## 8" distribution

```sh
(cd ../../..;./regnecentralend rc702 -bios 0 -window -skip_gameinfo -flop1 ~/Downloads/SW1711-I8.imd )
```

## 5,25" Comal80 distribution

```sh
(cd ../../..;./regnecentralend rc702mini -bios 0 -window -skip_gameinfo -flop1 ~/Downloads/CPM_med_COMAL80.imd)
```

## RC703 distribution (fails due to floppy controller bug upstream)

```sh
(cd ../../..;./regnecentralend rc703 -bios 1 -window -skip_gameinfo -flop1 ~/Downloads/RC703_CPM_v2.2_r1.2.imd)
```

If not running with a floppy image, you should get a yellow screen saying either "** NO PROGRAM OR LINEPROG" which is the ROM saying it cannot find a boot sector on the floppy (and no line program eprom is installed), or "** BAD DISKETTE" which mean that the diskette had read errors.   This is most likely because the disk drive emulated is not compatible with the image.

Full details in the reconstructed ROA375 autoload eprom sources at https://github.com/ravn/rc700-gensmedet/blob/main/roa375/roa375.asm

## Piccoline and Partner - Getting started

See https://rc700.dk/emulator.php for details about previous Piccoline emulator work.

Get the necessary ROMs:

```sh
OUTPUT_DIR=../../../roms/rc759
mkdir -p $OUTPUT_DIR
curl --output-dir $OUTPUT_DIR -L -O http://www.hampa.ch/pce/rom/rc759/rc759-1-2.1.rom
curl --output-dir $OUTPUT_DIR -L -O http://www.hampa.ch/pce/rom/rc759/rc759-1-5.1.rom
curl --output-dir $OUTPUT_DIR -L -O http://www.hampa.ch/pce/rom/rc759/rc759-2-4.0.rom
curl --output-dir $OUTPUT_DIR -L -O http://www.hampa.ch/pce/rom/rc759/rc759-2-5.1.rom
echo "*** All ROMS should be 32768 bytes ***"
ls -l $OUTPUT_DIR

OUTPUT_DIR=../../../roms/rc750
mkdir -p $OUTPUT_DIR
curl --output-dir $OUTPUT_DIR -L -o  ROD398.bin https://datamuseum.dk/bits/30010244
curl --output-dir $OUTPUT_DIR -L -o  ROD399.bin https://datamuseum.dk/bits/30010245
echo "*** All ROMS should be 16 kilobytes ***"
ls -l $OUTPUT_DIR


```

Now build MAME using something like (-j10 requires a modern machine):

```sh
make -C ../../.. SUBTARGET=regnecentralen DEBUG=1 SOURCES="src/mame/regnecentralen/rc759.cpp,src/mame/regnecentralen/rc750.cpp" TOOLS=1 SYMLEVEL=3  SYMBOLS=1  OSD=sdl -j 10
```

(add `REGENIE=1` the first time after adding/removing a source file in this
folder, so the generated project picks up the new files).

and run it similar to:

```sh
(cd ../../..;./regnecentralend rc759 -window -skip_gameinfo -flop1 ~/Downloads/SW1500_2.0.imd)
```
More at https://datamuseum.dk/wiki/Bits:Keyword/RC/RC759

```sh
(cd ../../..;./regnecentralend rc750 -window -skip_gameinfo -flop1 ~/Downloads/SW1500_2.0.imd -flop2 ~/Downloads/SW1542_RcSkak_r3.1.imd)
```
More at https://datamuseum.dk/wiki/Bits:Keyword/RC/RC750

## RC750 Partner hardware

The RC750 Partner is the sibling of the RC759 Piccoline. Both are driven
from the shared base class `rc75x_state` (`rc75x.h` / `rc75x.cpp`): the
common Intel 80186 + 8259A + 8255 + 82730 + MM58167 + NVM + SN76489A +
keyboard core lives in the base, and each model's `.cpp` adds only its own
floppy/serial/expansion side. `rc750.cpp` is NOT a subclass of `rc759.cpp`
- both derive independently from `rc75x_state`.

Partner-specific hardware (from the PARTNER Programmer's Guide v3, jun 1986,
saved in `rc700-gensmedet/docs/`): WD1797 FDC (modelled as `FD1797`), an
Intel 8274 dual serial controller, a SCSI host adapter and an optional 8087,
instead of the Piccoline's cassette / iSBX slot. It runs Concurrent DOS.

As of 2026-09-03 the Partner passes the self-test and boots on floppy with CCP/M-86 2.0 (the initial release from RC) and renders text mode correctly.

Only the absolute minimum of hardware has been wired in MAME by Claude Code just to get past the self-test.  This is a minimum viable product (MVP) to get the machine to boot.  The rest of the hardware is not yet emulated.

Interesting things to get up and running:

* Graphics mode.
* Winchester hard disk (SCSI) support.
* 

## References:

* Variuos materials: https://ddhf.dk/wiki/RC700_Piccolo
* Technical manual, not searchable:  https://ddhf.dk/w/images/5/5b/RC702_Tech_Man.pdf

