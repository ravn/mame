#!/bin/sh
# rc702_setup.sh -- fetch ROMs and a CP/M boot disk for the MAME rc702 driver.
#
# ROMs (roa375.ic66, roa296.rom, roa327.rom) are the genuine Regnecentralen
# firmware dumps from Michael Ringgaard's original RC700 emulator project:
#   https://github.com/ringgaard/rc700
#
# Boot disk (SW1711-I8.imd): an 8" DS/DD RC700 CP/M 2.2 system disk image
# with 1-based sector IDs (MAME/real-hardware convention).  Hosted in the
# rc702 firmware workspace:
#   https://github.com/ravn/rc700-gensmedet
#
# Run from the mamedev/mame root:
#   sh src/mame/regnecentralen/rc702_setup.sh
#
# Files written:
#   roms/rc702/roa375.ic66   -- RC702 boot PROM (2 KB)
#   roms/rc702/roa296.rom    -- character generator (2 KB)
#   roms/rc702/roa327.rom    -- semi-graphics ROM (2 KB)
#   rc702_sw/SW1711-I8.imd   -- CP/M 2.2 boot disk

set -e

MAME_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
ROMDIR="$MAME_DIR/roms/rc702"
DISKDIR="$MAME_DIR/rc702_sw"
RINGGAARD="https://raw.githubusercontent.com/ringgaard/rc700/master"
RC700GS="https://raw.githubusercontent.com/ravn/rc700-gensmedet/main"

fetch() {
    local url="$1" dest="$2" sha1="$3"
    if [ ! -f "$dest" ]; then
        printf "  fetching %s ...\n" "$(basename "$dest")"
        curl -fsSL "$url" -o "$dest"
    fi
    actual="$(shasum -a 1 "$dest" | awk '{print $1}')"
    if [ "$actual" != "$sha1" ]; then
        printf "ERROR: %s SHA1 mismatch\n  expected %s\n  got      %s\n" \
            "$(basename "$dest")" "$sha1" "$actual" >&2
        exit 1
    fi
}

mkdir -p "$ROMDIR" "$DISKDIR"

echo "=== RC702 ROMs (from ringgaard/rc700) ==="
fetch "$RINGGAARD/roa375.rom" "$ROMDIR/roa375.ic66" \
    306af9fc779e3d4f51645ba04f8a99b11b5e6084
fetch "$RINGGAARD/roa296.rom" "$ROMDIR/roa296.rom"  \
    efb8b1ece5f9eeca948202a6396865f26134ff2f
fetch "$RINGGAARD/roa327.rom" "$ROMDIR/roa327.rom"  \
    201ae9e4ac3812577244b9c9044fadd04fb2b82f
echo "  ROMs OK"

echo "=== CP/M boot disk ==="
fetch "$RC700GS/autoload-in-c/test-disks/SW1711-I8.imd" \
    "$DISKDIR/SW1711-I8.imd" \
    f6f17e0d93493e07845f04cae2214b884f59408b
echo "  Disk OK"

echo ""
echo "Boot with:"
echo "  ./mame rc702 -rompath roms -flop1 rc702_sw/SW1711-I8.imd"
