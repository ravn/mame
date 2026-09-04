# Regnecentralen

This file contains notes for future contributors to the RC700 Piccolo series, RC750 Partner, and RC759 Piccoline drivers in MAME.  Links were current as of 2026-09-04.

The RC702 is a 8-bit Z80 machine with either 8" external floppy drives or built-in 5,25" 360 Kb floppy drives, 64 Kb RAM, an optional 10 Mb Winchester hard disk and an external keyboard connected with the parallel port.  There is a 2 Kb boot prom ("autoload") which is presented as the bios in MAME. The successor RC703 had high density 5,25" 1.2 Mb diskettes, larger eproms, and generally improved electronics.  

MAME currently only works fully with the RC702, as the RC703 autoload prom triggers a bug in the floppy disk controller emulation.

The RC750 Partner/RC759 Piccoline are their 16-bit machine based on 80186, with the Partner targetting businesses and the Piccoline targetting the Danish school system.  It ran the technically superior CCP/M-86 system which allowed for 4 virtual consoles, but wasn't 100% compatible with the IBM PC.

Note that all machines use a national 8-bit locale with ÆØÅ and æøå replacing the US-ASCII characters `[{]}` and `\|`, and a pre-IBM PC vendor specific keyboard layout.  

# Getting started

You need to build MAME first.  This script is clickable in IntelliJ/CLion - note -j10 requires a modern machine.

```sh
make -C ../../.. SUBTARGET=regnecentralen DEBUG=1 SOURCES=src/mame/regnecentralen/rc759.cpp,src/mame/regnecentralen/rc750.cpp,src/mame/regnecentralen/rc702.cpp,src/mame/regnecentralen/pio_port/pio_port.cpp,src/mame/regnecentralen/pio_port/keyboard.cpp TOOLS=1 SYMLEVEL=3  SYMBOLS=1  OSD=sdl -j 10
```


(add `REGENIE=1` the first time after adding/removing a source file in this
folder, so the generated project picks up the new files).

The fork-only CP/NET PIO host bridge (`pio_port/cpnet_bridge.*`, slot option
`cpnet_bridge`) is parked on branch `parked-rc702-cpnet-bridge` until the
CP/NOS fast-link transport is upstream-ready (issue #50), so the PIO slot
currently only offers the `keyboard` card.


# Running the RC70x Piccolo in MAME

You will need ROM's and disk images.

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


If not running with a floppy image, you should get a yellow screen saying either "** NO PROGRAM OR LINEPROG" which is the ROM saying it cannot find a boot sector on the floppy (and no line program eprom is installed), or "** BAD DISKETTE" which mean that the diskette had read errors.   This is most likely because the disk drive emulated is not compatible with the image.

Full details in the reconstructed ROA375 autoload eprom sources at https://github.com/ravn/rc700-gensmedet/blob/main/roa375/roa375.asm


### 8" distribution


Note: In this and the following the images have been downloaded from https://ddhf.dk/wiki/Bits:Keyword/RC/RC700 - images from https://www.jbox.dk/rc702/disks.shtm have an off-by-one error and are not compatible with the emulated floppy controller.  TODO:  Full scripting of downloads.

```sh
(cd ../../..;./regnecentralend rc702 -bios 0 -window -skip_gameinfo -flop1 ~/Downloads/SW1711-I8.imd )
```

### 5,25" Comal80 distribution

```sh
(cd ../../..;./regnecentralend rc702mini -bios 0 -window -skip_gameinfo -flop1 ~/Downloads/CPM_med_COMAL80.imd)
```

### RC703 distribution 

```sh
(cd ../../..;./regnecentralend rc703 -bios 1 -window -skip_gameinfo -flop1 ~/Downloads/RC703_CPM_v2.2_r1.2.imd)
```
(this currently fails to boot due to a floppy controller bug upstream)

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

and run it similar to:

```sh
(cd ../../..;./regnecentralend rc759 -window -skip_gameinfo -flop1 ../scratch/rc759-pce/images/sw1400_r31a_d1.img)
```
More at https://datamuseum.dk/wiki/Bits:Keyword/RC/RC759

```sh
(cd ../../..;./regnecentralend rc750 -window -skip_gameinfo -flop1 ~/Downloads/SW1500_2.0.imd -flop2 ~/Downloads/SW1542_RcSkak_r3.1.imd)
```
(currently not working, as B: give read errors)

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
instead of the Piccoline's cassette / iSBX slot. It first ran CCP/M-86, which
was later developed into Concurrent DOS.

As of 2026-09-03 the Partner passes the self-test and boots on floppy with CCP/M-86 2.0 (the initial release from RC) and renders text mode correctly.

Only the absolute minimum of hardware has been wired in MAME by Claude Code just to get past the self-test and boot.  The rest of the hardware is not yet emulated.

Interesting things to get up and running:

* Second floppy
* Graphics mode.
* Winchester hard disk (SCSI) support.

